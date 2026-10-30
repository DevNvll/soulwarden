#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "gamedata/encounter_data.h"
#include "gamedata/interaction_data.h"
#include "world/entity.h"
#include "world/ids.h"
#include "world/load_meter.h"
#include "world/world_clock.h"

// The encounter system (stage QG5c2, docs/systems/encounters.md, protocol notes 4.62): the quest encounters of the world. Only this system
// makes, changes, and removes an encounter root entity (EntityKind::encounter); it makes and removes the members through the creature system
// (Creatures::spawn_member, remove_member) and the start objects through the interaction system (a server child of a template). World thread
// only.
//
// A placement is a recipe of the game data (gamedata::EncounterData) that the runner takes: its members have a kind (creatures.json), a
// template (interaction-data.json), and a row (gathering-data.json). The world makes the instance of a placement when a character is
// kSpawnEnter or less from it (the distances of the spawn points), and removes it when no character is kSpawnLeave or less from it (a
// dormant placement: it comes back at once when a character comes back). An instance has a generation: each due entry, member, and start
// object keeps the generation of its instance, and an entry of an old generation changes nothing.
//
// The run of an instance (the data of the recipe): the manager starts it by itself (m_autoActivate, after m_autoActivateDelayMS); the stages
// of the manager order run one after the other. A stage starts (status 2 in its state 2133), makes its spawns at their delays (a creature
// member at its spawn location, a start object of a GatherOption), and ends with its objective: a timed stage at the end of its time
// (InvasionWaveObjective, succeedOnTimeout), a destroyed stage at m_requiredCount destroyed spawns (the deaths of its members and the
// gather of its start object; its tracked slice, else each spawn of the stage). The end is status 3 (success) or 4 (failure: the end of a
// time limit with no succeedOnTimeout). The last stage ends the encounter: the manager status 3, the event of the recipe for each
// participant (the credit), and the removal of the root, its members, and its objects (m_destroySpawnsOnDestroyed of the root spawner); the
// parent makes a new instance after its respawn time (the respawn range of its area spawner).
//
// The participants are the characters in the detector sphere of the root (m_Radius, OnlyPlayerTarget) while the instance runs; a character
// that leaves it stays a participant for m_gracePeriodSec. Each participant has the link of the root in its player Objectives state (field 5:
// the client shows the encounter objective on its HUD). The credit at the success: a snapshot of the participants and the generation; one
// Quests::event (gameEvent) for each of them; a store failure keeps the recipient in the ledger and tries again after kPayoutRetry. A fight
// stage with no participant fails the encounter (a reset, a rule of this project).
//
// Stage QG5c3 (the summoned opponents, the ambushes, the kill waves with a cause of the data): the child stages of a stage run one after
// the other (the first one at the start of the stage), and a stage counts the spawns of its child stages; a stage whose child stages are
// not optional ends when it has its count and its last child stage ended. A SliceSpawnedObjective ends at the spawns of its tracked slice;
// a destroyed stage with no tracked slice and no spawn below it counts each death of a member of its instance. A proximity object (the
// start of an ambush; an area trigger with no Interact has no entity) is found at a scan by each character in its sphere with an accepted
// position (InteractOnlyOnQuest: an Active task with its tag): the game event and the tag of its row, then its stage counts it. A member
// that ends itself (its lifetime: the smoke burst, the powder keg) has no entity and counts at its end; a member that no stage needs and
// that the runner cannot make (a visual, a creature member that the client does not show) is not made. The detector is a sphere, a
// cylinder, or an axis box (the capsule of a character in one of its shapes). Code review of stage QG5c2: the participants are refreshed
// at the success (R1), and a count above the members that the runner makes refuses the recipe (R3).
//
// Stage QG5c4 (the interaction sequences, the defenses, the world events of the open world): an EntityActiveStateChangedObjective ends its
// stage when a character is in a shape of its trigger (the detection volume of the slice that changes the target entity: the Trigger_Start
// at the top of a tarot tower); each objective of a stage counts its own tracked slice, and the stage meets its count when each one is met
// (an objective of creature members that the client does not show is met at its start when an other objective of the stage counts members
// that the runner makes). A recipe with no creature member is taken. Code review of stage QG5c3: the activation finds its instance again
// after the first stage (R1), each finder of a proximity object keeps its write until the store took it (R2), and a spawn due at or after the
// time limit of its stage gives no count (R3).
//
// Stage QG5c5 (the Darkness events, the boat reinforcements): a group member makes one creature for each part (each creature spawner of
// its slice: its kind "SLICE#N", its place in the slice, its patrol path); a part that the client does not show is not made, and an
// objective that the made parts cannot reach counts the missing parts at the start of its stage. A child stage with a
// WaitForParentObjective ends with its parent, and its parent does not wait for it. A DestroySliceAction removes the members of a slice or
// an alias. A destroyed objective with a tracked slice counts a member of that slice of an earlier stage too (the spyglass). A spawn of an
// alias counts for an objective that tracks the alias. The season rule (kDarknessAtAllTimes): a Darkness event is available at all times.
// Code review of stage QG5c4: a missing gathering file is no fault of the load (R1), a trigger meets only its own objective (R2), a retry
// of a find credits only the tasks of its first attempt (R3), the count check takes the time limits of the stages above a spawn (R4), and
// a spawned objective publishes its count (R5).
//
// The cost depends on the characters, the instances, and the due entries, not on the size of the world (MP-8): the placements are in a grid
// of cells (a scan looks at the cells near each character), the participants of an instance are the characters near its root (the position
// index), the proximity objects are the ones of the running instances (the characters near each one), and the timers are one due list.

namespace soulwarden::backend::world {

class World;
struct WorldCapsule;

namespace encounter_rules {
/** The interval of the placement rule and of the participant rule on the world clock. */
inline constexpr std::chrono::milliseconds kScanInterval{500};
/** The distances of the placement rule (the spawn points of the creatures, a rule of this project; WorldOptions). */
inline constexpr float kSpawnEnter = 150.0F;
inline constexpr float kSpawnLeave = 200.0F;
/** The edge of a cell of the grid of the placements (meters). */
inline constexpr float kCellSize = 64.0F;
/** Stage QG9c: a creature member of an instance recipe whose spawn location is higher than this above the floor stands on the floor. */
inline constexpr float kAboveFloor = 2.0F;
/**
 * Code review of stage QG9c, finding QG9CR-R1: a spawn of a creature member of an instance recipe whose navigation tile is not in the world
 * yet (it loads) tries again after this time, at most kFloorTries times; then the floor rule takes the floor that it has.
 */
inline constexpr std::chrono::milliseconds kFloorWait{100};
inline constexpr std::size_t kFloorTries = 50;
/** The entries of the stage state 2133 that the client takes (client 0x26F7550: at most 10). */
inline constexpr std::size_t kHistory = 10;
/** The manager status (state 6786; the Lua names eEncounterStatus_*: static, protocol notes 4.62). */
inline constexpr std::uint32_t kNotStarted = 1;
inline constexpr std::uint32_t kInProgress = 2;
inline constexpr std::uint32_t kSucceeded = 3;
inline constexpr std::uint32_t kFailed = 4;
/** The stage status (state 2133 and message 1278; client 0x26F64A0: 2 the start actions, 3 and 4 the end actions). */
inline constexpr std::uint32_t kStageStarted = 2;
inline constexpr std::uint32_t kStageSucceeded = 3;
inline constexpr std::uint32_t kStageFailed = 4;
/** A credit that the store refused tries again after this time, at most kPayoutAttempts times (a rule of this project). */
inline constexpr std::chrono::milliseconds kPayoutRetry{2000};
inline constexpr std::uint32_t kPayoutAttempts = 5;
/**
 * Code review of stage QG5c3, finding QG5C3R-R2: the looks (one each kScanInterval) that try again a quest write of the find of a proximity
 * object that the store refused: 20 looks, the 10 s of the credit ledger (kPayoutAttempts times kPayoutRetry; a rule of this project).
 */
inline constexpr std::uint32_t kFindAttempts = 20;
/**
 * Stage QG5c5, the season rule (a project rule of the orchestrator of stage QG5c5, docs/systems/encounters.md; the user can change it): the
 * backend has no season schedule, so a Darkness event (a recipe with the season "darkness") is available at all times. It starts only by
 * the start rule of its recipe (its placement near a character, its manager, its detector), as each other encounter; it never starts by
 * itself across the world. False: the runner refuses each Darkness recipe (one cause), and no Darkness event runs.
 */
inline constexpr bool kDarknessAtAllTimes = true;
/**
 * Stage QG9b, the setting of the mode QuestMedusa (a pick of the orchestrator; the user can change it). Stage QG9c: on. The stop of the client
 * at the kill of Medusa by a hit of a player had its cause in the pending reaction that the client predicts on a slice with no reaction
 * action; the death rule of Creatures::set_health corrects it (docs/systems/creatures.md). False: the runner refuses each recipe of
 * QuestMedusa with kQuestMedusaOff (one cause), so its entrance refuses the entry (result 10) and the support manifest counts the rows of the
 * mode as not supported with that reason. True: the mode runs with the rules of stage QG9b. The world option WorldOptions::questMedusaRuns has
 * this value (a test sets it; no start option).
 */
inline constexpr bool kQuestMedusaRuns = true;
inline constexpr std::string_view kQuestMedusaMode = "QuestMedusa";
inline constexpr std::string_view kQuestMedusaOff = "the mode QuestMedusa is off by its setting (encounter_rules::kQuestMedusaRuns)";
/** @return The reason of the setting for a mode that the setting turns off (`medusaRuns`: the world option), or an empty text. */
[[nodiscard]] inline std::string_view mode_off(std::string_view mode, bool medusaRuns) {
    const bool medusa = mode.size() == kQuestMedusaMode.size() && std::equal(mode.begin(), mode.end(), kQuestMedusaMode.begin(), [](char a, char b) {
                            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                        });
    return medusa && !medusaRuns ? kQuestMedusaOff : std::string_view{};
}
/** The bounds of one instance (load bounds: the data has at most 32 for each spawn entry). */
inline constexpr std::size_t kMaxMembers = 256;
inline constexpr std::size_t kMaxParticipants = 64;
} // namespace encounter_rules

enum class EncounterPhase : std::uint8_t {
    /** The instance waits for its start (m_autoActivateDelayMS). */
    waiting,
    running,
    succeeded,
    failed,
};

[[nodiscard]] const char* encounter_phase_text(EncounterPhase phase);

/** Why an instance ended (the log, the view, the tests). */
enum class EncounterEnd : std::uint8_t {
    succeeded,
    failed,
    /** No character near the placement: the instance goes, and the placement is free again at once. */
    dormant,
    /** A command (tools/world encounter reset). */
    command,
};

[[nodiscard]] const char* encounter_end_text(EncounterEnd end);

/** One member of an instance: a creature or a start object. */
struct EncounterMemberRun {
    std::uint32_t stage{0};
    std::uint32_t spawn{0};
    bool object{false};
    bool done{false};
    /**
     * Stage QG5c3: a proximity object (a character in its sphere finds it: the ambush starts), and a member with no entity (an area trigger
     * with no Interact component, a member that ends itself): its id is an id of the allocator that no entity has.
     */
    bool proximity{false};
    bool noEntity{false};
    /** Stage QG5c5: the part of a group member (EncounterMember::parts); -1: not a part. */
    std::int32_t part{-1};
    /**
     * Stage QG9b: the member died or was gathered or found (a count of its stage), not removed by an action, a command, or the end of the
     * instance (code review of stage QG9a, finding QG9AR-R4: a stage that starts later counts the death of a tracked boss of an earlier stage).
     */
    bool destroyed{false};
    /** Stage QG9b: the phase of a boss (its index in EncounterMember::phases); -1: a member with no boss phases. */
    std::int32_t phase{-1};
};

/** The run of one stage of an instance. */
struct EncounterStageRun {
    bool started{false};
    bool ended{false};
    /** The first objective: its destroyed spawns (a destroyed objective) or the spawns of its tracked slice (a spawned objective). */
    std::uint32_t destroyed{0};
    std::uint32_t spawned{0};
    /**
     * Stage QG5c3: the spawns of the tracked slice (a spawned objective), the next child stage to start, the end of the last child stage,
     * and the count of the objective (a stage whose child stages are not optional ends when both came).
     */
    std::uint32_t spawnedTracked{0};
    std::uint32_t nextChild{0};
    bool childrenDone{false};
    bool objectiveMet{false};
    /**
     * Stage QG5c4: the count and the end of each objective of the stage (index 0: the first objective, `destroyed` or `spawnedTracked`;
     * then the objectives of EncounterStage::more). The stage meets its count when each one is met.
     */
    std::vector<std::uint32_t> counts;
    std::vector<bool> met;
};

struct EncounterParticipant {
    WorldClock::time_point joined{};
    /** The end of the grace of a participant out of the sphere (m_gracePeriodSec). No value: in the sphere. */
    std::optional<WorldClock::time_point> graceUntil;
};

struct EncounterInstance {
    std::uint32_t placement{0};
    std::uint64_t generation{0};
    EntityId root{0};
    /** Stage QG9a: the space of the instance (an instance recipe runs in the space of an instance of its mode; kMainSpace: the main world). */
    SpaceId space{kMainSpace};
    EncounterPhase phase{EncounterPhase::waiting};
    /** The position in the stage order of the manager of the stage that runs. */
    std::uint32_t current{0};
    std::vector<EncounterStageRun> stages;
    std::map<EntityId, EncounterMemberRun> members;
    std::map<std::string, EncounterParticipant> participants;
    WorldClock::time_point made{};
    /** Stage QG9b: the user waves (EncounterStage::userIndex) that ended: the end of a WaveEnded boss phase. */
    std::set<std::int32_t> wavesEnded;
};

/** One placement of a recipe, taken or refused. */
struct EncounterPlacement {
    const gamedata::EncounterRecipe* recipe{nullptr};
    /** The causes of a refusal (the tool and the other game data files). Empty: the runner takes it. */
    std::vector<std::string> refused;
    /**
     * The kind of each creature member and the start object data of each start object (its template and its variation), by the index of
     * the member in the recipe data. Null: not that member kind, or a refused placement.
     */
    std::vector<const gamedata::CreatureKind*> kinds;
    std::vector<const gamedata::EncounterObject*> templates;
    /** Stage QG5c5: the kind of each part of a group member (null: the client does not show its slice), by the index of the member. */
    std::vector<std::vector<const gamedata::CreatureKind*>> partKinds;
    /**
     * Stage QG5c5: for each stage, the count that each objective gets at its start for the parts that the runner does not make (a part that
     * the client does not show: the swarmers of the portal waves): the parts that its count needs beyond the parts that the runner makes.
     */
    std::vector<std::vector<std::uint32_t>> fills;
    /**
     * Stage QG5c3: the stages that count each death of a member of their instance (a destroyed stage with no tracked slice and no spawn of
     * its own or of its child stages: Kill Helvan Boss), by the stage index.
     */
    std::vector<bool> anyMember;
    /**
     * Stage QG5c4: for each stage, the objectives (index 0 the first, then EncounterStage::more) that are met at the start of the stage: in
     * a stage with more than one objective, an objective whose tracked member slices are only creature members that the client does not
     * show (no kind: the swarmers of the Yonas retreat) when an other objective of the stage counts members that the runner makes.
     */
    std::vector<std::vector<bool>> voided;
    /**
     * Stage QG9b: an instance recipe of a mode with more than one recipe that is not a daily alternative (QuestSeraphim: the four seraphs;
     * QuestYonas: the hallway, the Isabella intro, the boss) starts only for a character with an active task of the mode that names it
     * (Quests::mode_task_active): its success event ("e:") and the kill keys of the creature members that its destroyed objectives track
     * ("k:") [a rule of this project from the quest data: the mode script that chooses and orders them is not in the client].
     */
    bool gated{false};
    std::vector<std::string> taskKeys;
    /** The instance that runs (0: none), the last generation, the respawn of a new instance (no value: at once). */
    bool live{false};
    std::uint64_t generation{0};
    std::optional<WorldClock::time_point> respawnAt;
};

/** One credit at a success: the recipients that the ledger did not pay yet. */
struct EncounterPayout {
    std::uint32_t placement{0};
    std::uint64_t generation{0};
    std::string event;
    std::uint64_t result{0};
    std::vector<std::string> pending;
    std::map<std::string, std::vector<std::pair<std::uint64_t, std::uint8_t>>> matched;
    std::uint32_t attempts{0};
};

struct EncounterCounts {
    std::uint64_t placements{0};
    std::uint64_t taken{0};
    std::uint64_t made{0};
    std::uint64_t succeeded{0};
    std::uint64_t failed{0};
    std::uint64_t dormant{0};
    std::uint64_t membersMade{0};
    /** Stage QG9c: the creature members of an instance recipe put on the floor below their spawn location (kAboveFloor). */
    std::uint64_t membersLowered{0};
    /** Code review of stage QG9c, finding QG9CR-R1: the spawns of an instance recipe that waited for a navigation tile (kFloorWait). */
    std::uint64_t spawnsWaited{0};
    std::uint64_t membersRemoved{0};
    std::uint64_t memberDeaths{0};
    std::uint64_t objectsMade{0};
    std::uint64_t objectsGathered{0};
    /** Stage QG5c3: the finds of the proximity objects, the looks of their spheres, the members that ended themselves, the spawns that the
     *  runner did not make (a visual of its stage, a creature member that the client does not show in a stage that does not need it). */
    std::uint64_t objectsFound{0};
    std::uint64_t proximityLooks{0};
    std::uint64_t timedEnds{0};
    std::uint64_t spawnsSkipped{0};
    /** Stage QG5c4: the entity-state stages that a character in their trigger ended, the looks of the triggers, the objectives met at their
     *  start (EncounterPlacement::voided), and the finder writes that the store refused (finding QG5C3R-R2: tried again at the next look). */
    std::uint64_t triggersFound{0};
    std::uint64_t triggerLooks{0};
    std::uint64_t objectivesVoided{0};
    std::uint64_t findRetries{0};
    /** Stage QG5c5: the parts of the group members that the runner made, the parts that it did not make (the client does not show their
     *  slice), the counts given at a stage start for such parts (EncounterPlacement::fills), and the members that a DestroySliceAction
     *  removed. */
    std::uint64_t partsMade{0};
    std::uint64_t partsHidden{0};
    std::uint64_t partsFilled{0};
    std::uint64_t slicesDestroyed{0};
    /** Events of an old generation (a late death, an old timer, an old gather): no change. */
    std::uint64_t oldGeneration{0};
    std::uint64_t participantsAdded{0};
    std::uint64_t participantsLeft{0};
    std::uint64_t credits{0};
    std::uint64_t creditRetries{0};
    std::uint64_t creditsDropped{0};
    /** The work of the scans: the placements and the entities that they looked at, and their number. */
    std::uint64_t scans{0};
    std::uint64_t placementsLooked{0};
    std::uint64_t entitiesLooked{0};
    std::uint64_t dueTaken{0};
};

/** A view of an instance for the development routes and the tests. */
struct EncounterView {
    std::string key;
    std::string slice;
    EntityId root{0};
    std::uint64_t generation{0};
    EncounterPhase phase{EncounterPhase::waiting};
    std::string stage;
    std::uint32_t destroyed{0};
    std::uint32_t required{0};
    std::vector<std::string> participants;
    std::vector<EntityId> members;
    float distance{0.0F};
    /** Stage QG5c5: each stage that runs (also a child stage) with the count of each objective: "NAME: N/M, N/M". */
    std::vector<std::string> running;
};

class Encounters {
public:
    Encounters(World& world, const gamedata::EncounterData* data);

    [[nodiscard]] const gamedata::EncounterData* data() const {
        return m_data != nullptr && m_data->loaded() ? m_data : nullptr;
    }

    /** The world calls it in each tick: the queued events, the due list, and (each kScanInterval) the placement and participant rules. */
    void tick();
    /** The next tick runs the placement rule, also before its interval (a travel). */
    void scan_soon() {
        m_lastScan.reset();
    }

    // The events of the other systems. Each is queued and runs in the next tick of this system (an event in the middle of a hit or a
    // gather removes no entity).

    /** The creature system: a member died (Creatures::set_health). */
    void member_died(const Entity& creature);
    /** The gathering system: the gather of a start object committed (its game event went to the character). */
    void object_gathered(EntityId object, const Entity& character);
    /** The world removed an entity: a member, a start object, or a root that went away not by this system. */
    void entity_removed(const Entity& entity);
    /** A character left the world: it is no participant, and its link goes with its entity. */
    void character_left(const Entity& character);

    // Stage QG9a (docs/systems/instances.md): the instance recipes, the encounters of the mode root slice of a game mode. They run only in
    // the space of an instance of their mode, never at a placement of the main world.

    /**
     * @return True when the runner runs the content of a mode: the mode has an instance recipe that is not a daily alternative, and the
     *         runner takes each such recipe of the mode (the instance system refuses an entry otherwise: an empty copy of the world).
     */
    [[nodiscard]] bool runs_mode(std::string_view mode);
    /**
     * The instance system made an instance of a mode in a space: each taken instance recipe of the mode (not a daily alternative) waits in
     * that space. Its encounter starts when a character of the space is in its detector [agent: the mode script and the AreaBoss trigger of
     * the mode root that would start it are not in the client]; a failed fight starts again the same way; a success ends the recipe in that
     * space. @return The number of the recipes of the space.
     */
    std::size_t start_space(SpaceId space, std::string_view mode);
    /** The instance of a space ended: each encounter of the space ends (its members and its root go), and its recipes go. */
    void end_space(SpaceId space);
    /** @return True when each instance recipe of a space succeeded (the content of the instance is done). */
    [[nodiscard]] bool space_done(SpaceId space) const;
    /**
     * Stage QG9d: the recipes of a space wait (no look at their detector) while the space is held: the instance system holds the space of
     * an instance until the quest objects of its mode root are gathered (Instances::look_quest_objects).
     */
    void hold_space(SpaceId space, bool held);
    /**
     * Stage QG9d: @return True when the character has an active task of the mode of a recipe of the space that names that recipe (its
     * success event, a kill category of a tracked creature member, the event of a tracked start object: EncounterPlacement::taskKeys).
     */
    [[nodiscard]] bool mode_recipe_task(const Entity& character, SpaceId space);
    /**
     * Stage QG9a: the cutscene `crc` of a running cutscene stage of an instance of the space of the character (6703 of its client): the
     * session grants it (6707). @return True when such a stage runs.
     */
    [[nodiscard]] bool cutscene_requested(Entity& character, std::uint32_t crc);
    /**
     * Stage QG9a: the client of the character ended its cutscene (6705, no id): the running cutscene stage of the granted cutscene of the
     * character (Entity::encounterCutscene) in its instance succeeds. @return True when a stage ended.
     */
    bool cutscene_ended(Entity& character);

    /** @return The slice of a start object of a running stage (the gathering system: its row and its tag), or null. */
    [[nodiscard]] const std::string* start_object(EntityId object) const;

    // Commands (the development routes) and views.

    /** Ends the instance of a placement (the key of its recipe) and makes the placement free at once. @return False: no such instance. */
    bool reset(const std::string& key);
    /**
     * DEVELOPMENT AID (the development routes only): the start object of a running stage 10 m or less from the character counts as gathered
     * by it (the game event of its row, then the stage). @return The object, or no value.
     */
    std::optional<EntityId> use_start_object(Entity& character);
    /** @return The instances whose root is `radius` or less from a place (horizontal), the nearest first. */
    [[nodiscard]] std::vector<EncounterView> view(const state::WorldPosition& place, float radius) const;
    [[nodiscard]] const std::vector<EncounterPlacement>& placements() const {
        return m_placements;
    }
    /** @return The placements, loaded now when no tick loaded them yet (the support manifest of the quests). */
    [[nodiscard]] const std::vector<EncounterPlacement>& loaded_placements() {
        if (!m_loaded && data() != nullptr) {
            load();
        }
        return m_placements;
    }
    [[nodiscard]] const std::map<EntityId, EncounterInstance>& instances() const {
        return m_instances;
    }
    [[nodiscard]] const std::vector<EncounterPayout>& payouts() const {
        return m_payouts;
    }
    [[nodiscard]] const EncounterCounts& counts() const {
        return m_counts;
    }
    [[nodiscard]] std::size_t due_count() const {
        return m_due.size();
    }
    /** @return The real time of the scans (a measurement: the load case of the runner). */
    [[nodiscard]] const LoadMeter& scan_load() const {
        return m_scanLoad;
    }
    /** @return The placement index of a recipe key, or no value. */
    [[nodiscard]] std::optional<std::uint32_t> placement_of(const std::string& key) const;

private:
    enum class DueKind : std::uint8_t {
        activate,
        stageTime,
        spawn,
        payout,
        /** The aggro delay of a member (its entity in `payout`). */
        aggro,
        /** Stage QG5c3: the end of a member that ends itself (its id in `payout`). */
        memberEnd,
        /** Stage QG9a: an EncounterEventObjective of an instance recipe is met (the stage in `stage`). */
        eventMet,
        /**
         * Stage QG9b: the end rule of a stage of an instance recipe (gamedata::StageEnd): a stage with no objective or a destroyed stage with
         * nothing to count at its start, an entity-state wait after its last spawn (and then its child stages).
         */
        endRule,
        /** Stage QG9b: a look at the boss phases of an instance (after the end of a user wave, a death, or the start of a phase stage). */
        phases,
    };
    /**
     * Code review of stage QG5c4, finding QG5C4R-R3: a finder whose quest write the store refused: its attempts, and the tasks that each event
     * of its find matched at the first attempt (a retry credits only these tasks, when they are still active: a quest that became active
     * after the find gets nothing).
     */
    struct FindRetry {
        std::uint32_t attempts{0};
        std::vector<std::vector<std::pair<std::uint64_t, std::uint8_t>>> matched;
    };
    /** Stage QG5c3: a proximity object of a running stage (with an entity or not): its place and its member data. */
    struct Proximity {
        std::uint32_t placement{0};
        std::uint64_t generation{0};
        state::WorldPosition place;
        const gamedata::EncounterMember* member{nullptr};
        /**
         * Code review of stage QG5c3, finding QG5C3R-R2: the finders whose quest write the store took, and the finders whose write the store
         * refused (the attempts). The object stays until each finder of the set has its write or ran out of attempts; then it goes, one time.
         */
        std::set<std::string> paid;
        std::map<std::string, FindRetry> failures;
    };
    /** Stage QG5c4: a running entity-state stage (its triggers are in the objective of the stage of the recipe). */
    struct Armed {
        std::uint32_t placement{0};
        std::uint64_t generation{0};
        std::uint32_t stage{0};
    };
    struct Due {
        DueKind kind{DueKind::activate};
        std::uint32_t placement{0};
        std::uint64_t generation{0};
        std::uint32_t stage{0};
        std::uint32_t spawn{0};
        std::size_t payout{0};
    };
    struct Event {
        bool death{false};
        EntityId entity{0};
        std::uint32_t placement{0};
        std::uint64_t generation{0};
        std::string character;
    };

    void load();
    void resolve(EncounterPlacement& placement);
    void scan();
    void take_due();
    void take_events();
    void update_participants(EncounterInstance& instance);
    void make_instance(std::uint32_t placement, SpaceId space = kMainSpace);
    /** Stage QG9a: the instance recipes of the spaces wait for a character of their space in their detector. */
    void scan_spaces();
    void activate(EncounterInstance& instance);
    void start_stage(EncounterInstance& instance, std::uint32_t stage);
    /** @param tries The waits of the spawn for a navigation tile before (finding QG9CR-R1: encounter_rules::kFloorWait). */
    void spawn(EncounterInstance& instance, std::uint32_t stage, std::uint32_t spawn, std::size_t tries = 0);
    void count_destroyed(EncounterInstance& instance, EntityId member);
    /** Stage QG5c3: the count of one stage reached its required count (or a spawned objective its spawns): it ends, or it waits for its child
     *  stages that are not optional. */
    void objective_met(EncounterInstance& instance, std::uint32_t stage);
    /**
     * Stage QG9b: the boss phases of the members of an instance: a current phase ends at the end of its user wave (WaveEnded) or at its health
     * percent (HealthThreshold), and the next one starts; a running BossPhaseObjective stage whose phase ended is met.
     */
    void advance_phases(EncounterInstance& instance);
    /** Stage QG9b: the health phases of the bosses of the running instances (each scan). */
    void look_phases();
    /**
     * Code review of stage QG9a, finding QG9AR-R4: a destroyed stage of an instance recipe that starts counts the tracked members of the other
     * stages that died before its start (a boss of the background setup killed during the intro).
     */
    void count_earlier_deaths(EncounterInstance& instance, std::uint32_t stage);
    /** Stage QG5c3: the characters in the spheres of the proximity objects find them (an accepted position; InteractOnlyOnQuest). */
    void look_proximity();
    /** Stage QG5c4: a character in a shape of the trigger of a running entity-state stage ends that stage. */
    void look_triggers();
    /** Stage QG5c3: a character is in the detector of a recipe (one of its shapes). */
    [[nodiscard]] static bool in_detector(const gamedata::EncounterRecipe& recipe, const state::WorldPosition& root, const WorldCapsule& body);
    /** Stage QG5c4: the body of a character overlaps one of the shapes at their offsets from a place. */
    [[nodiscard]] static bool in_shapes(const std::vector<gamedata::EncounterShape>& shapes, const state::WorldPosition& place,
                                        const WorldCapsule& body);
    /** A new member takes the nearest participant that lives as its target (the aggro of the encounter). */
    void provoke(EncounterInstance& instance, Entity& member);
    void end_stage(EncounterInstance& instance, std::uint32_t stage, bool success);
    void succeed(EncounterInstance& instance);
    void end_instance(EntityId root, EncounterEnd end);
    /** Removes the members of an instance (no count, no reward): the creatures, and the objects (`objectsToo`), or (stage QG5c4, a
     *  KillSpawnsAction) the objects of the stages that ended (`endedObjects`). */
    void remove_members(EncounterInstance& instance, bool objectsToo, bool endedObjects = false);
    void pay(std::size_t payout);
    void stage_status(EncounterInstance& instance, std::uint32_t stage, std::uint32_t status);
    void manager_status(EncounterInstance& instance, std::uint32_t status);
    void publish_objectives(EncounterInstance& instance);
    void link(Entity& character, EntityId root, bool on);
    /**
     * @return True when an objective counts a spawn: it tracks no slice, its slice, its alias (stage QG5c5), or the body slice or an alias
     *         slice of the member. Stage QG5c5: `part` is a part of a group member (its body); -1: the member, or any part of a group.
     */
    [[nodiscard]] bool counts_toward(const gamedata::EncounterObjective& objective, const gamedata::EncounterSpawn& spawn,
                                     std::int32_t part = -1) const;
    /** Stage QG5c5: the members of the instance whose slice, alias, or body a DestroySliceAction names go (no count, no reward). */
    void destroy_slices(EncounterInstance& instance, const std::vector<gamedata::EncounterDestroy>& actions);
    /** Stage QG5c5: the child stages of a stage that it waits for (not optional) ended: each one that is not a WaitForParentObjective stage. */
    [[nodiscard]] bool children_done(const EncounterInstance& instance, std::uint32_t stage) const;
    /**
     * Stage QG5c3: the stage of a spawn or a stage above it counts the spawn (a destroyed or a spawned objective that tracks it or nothing).
     * Stage QG5c4: each objective of those stages, not one that is met at its start (EncounterPlacement::voided).
     */
    [[nodiscard]] bool spawn_needed(const EncounterPlacement& placement, std::uint32_t stage, const gamedata::EncounterSpawn& spawn) const;
    /** Stage QG5c4: the objective `index` of a stage (0 the first, then EncounterStage::more). */
    [[nodiscard]] static const gamedata::EncounterObjective& objective_of(const gamedata::EncounterStage& stage, std::size_t index) {
        return index == 0 ? stage.objective : stage.more[index - 1];
    }
    /** Stage QG5c4: one more count of the objective `index` of a stage; the stage meets its count when each objective is met. */
    void count_objective(EncounterInstance& instance, std::uint32_t stage, std::size_t index, std::uint32_t added);
    [[nodiscard]] EncounterInstance* instance_of(std::uint32_t placement, std::uint64_t generation);
    [[nodiscard]] static std::int64_t cell_of(float value);
    void schedule(WorldClock::time_point at, const Due& due);

    World& m_world;
    const gamedata::EncounterData* m_data;
    bool m_loaded{false};
    std::vector<EncounterPlacement> m_placements;
    std::map<std::pair<std::int64_t, std::int64_t>, std::vector<std::uint32_t>> m_cells;
    /** The instances by their root entity, and the root of the instance of each live placement. */
    std::map<EntityId, EncounterInstance> m_instances;
    std::unordered_map<std::uint32_t, EntityId> m_live;
    /**
     * Stage QG9a: the instance recipes of each space of an instance (their placement indexes), the instance of each (placement, space) that
     * runs, the (placement, space) pairs whose encounter succeeded (no new instance), and the root of each (placement, generation).
     */
    std::map<SpaceId, std::vector<std::uint32_t>> m_spaces;
    std::map<std::pair<std::uint32_t, SpaceId>, EntityId> m_spaceLive;
    std::set<std::pair<std::uint32_t, SpaceId>> m_spaceDone;
    /** Stage QG9d: the spaces whose recipes wait for the quest objects of the instance (hold_space). */
    std::set<SpaceId> m_heldSpaces;
    std::map<std::pair<std::uint32_t, std::uint64_t>, EntityId> m_byGeneration;
    /** The members and the start objects of the instances: entity to its placement and generation. */
    std::unordered_map<EntityId, std::pair<std::uint32_t, std::uint64_t>> m_memberOf;
    /** The start objects of the running stages: entity to its member slice. */
    std::unordered_map<EntityId, std::string> m_objects;
    /** Stage QG5c3: the proximity objects of the running stages (an entity id, or an id with no entity). */
    std::map<EntityId, Proximity> m_proximity;
    /** Stage QG5c4: the running entity-state stages (their triggers). */
    std::vector<Armed> m_armed;
    std::multimap<WorldClock::time_point, Due> m_due;
    std::vector<Event> m_events;
    std::vector<EncounterPayout> m_payouts;
    std::optional<WorldClock::time_point> m_lastScan;
    EncounterCounts m_counts;
    LoadMeter m_scanLoad;
};

} // namespace soulwarden::backend::world

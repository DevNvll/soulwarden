#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
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
};

/** The run of one stage of an instance. */
struct EncounterStageRun {
    bool started{false};
    bool ended{false};
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
    EncounterPhase phase{EncounterPhase::waiting};
    /** The position in the stage order of the manager of the stage that runs. */
    std::uint32_t current{0};
    std::vector<EncounterStageRun> stages;
    std::map<EntityId, EncounterMemberRun> members;
    std::map<std::string, EncounterParticipant> participants;
    WorldClock::time_point made{};
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
    /**
     * Stage QG5c3: the stages that count each death of a member of their instance (a destroyed stage with no tracked slice and no spawn of
     * its own or of its child stages: Kill Helvan Boss), by the stage index.
     */
    std::vector<bool> anyMember;
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
    };
    /** Stage QG5c3: a proximity object of a running stage (with an entity or not): its place and its member data. */
    struct Proximity {
        std::uint32_t placement{0};
        std::uint64_t generation{0};
        state::WorldPosition place;
        const gamedata::EncounterMember* member{nullptr};
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
    void make_instance(std::uint32_t placement);
    void activate(EncounterInstance& instance);
    void start_stage(EncounterInstance& instance, std::uint32_t stage);
    void spawn(EncounterInstance& instance, std::uint32_t stage, std::uint32_t spawn);
    void count_destroyed(EncounterInstance& instance, EntityId member);
    /** Stage QG5c3: the count of one stage reached its required count (or a spawned objective its spawns): it ends, or it waits for its child
     *  stages that are not optional. */
    void objective_met(EncounterInstance& instance, std::uint32_t stage);
    /** Stage QG5c3: the characters in the spheres of the proximity objects find them (an accepted position; InteractOnlyOnQuest). */
    void look_proximity();
    /** Stage QG5c3: a character is in the detector of a recipe (one of its shapes). */
    [[nodiscard]] static bool in_detector(const gamedata::EncounterRecipe& recipe, const state::WorldPosition& root, const WorldCapsule& body);
    /** A new member takes the nearest participant that lives as its target (the aggro of the encounter). */
    void provoke(EncounterInstance& instance, Entity& member);
    void end_stage(EncounterInstance& instance, std::uint32_t stage, bool success);
    void succeed(EncounterInstance& instance);
    void end_instance(EntityId root, EncounterEnd end);
    void remove_members(EncounterInstance& instance, bool objectsToo);
    void pay(std::size_t payout);
    void stage_status(EncounterInstance& instance, std::uint32_t stage, std::uint32_t status);
    void manager_status(EncounterInstance& instance, std::uint32_t status);
    void publish_objectives(EncounterInstance& instance);
    void link(Entity& character, EntityId root, bool on);
    [[nodiscard]] bool counts_toward(const gamedata::EncounterRecipe& recipe, const gamedata::EncounterStage& stage,
                                     const gamedata::EncounterSpawn& spawn) const;
    /** Stage QG5c3: the stage of a spawn or a stage above it counts the spawn (a destroyed or a spawned objective that tracks it or nothing). */
    [[nodiscard]] bool spawn_needed(const gamedata::EncounterRecipe& recipe, std::uint32_t stage, const gamedata::EncounterSpawn& spawn) const;
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
    /** The members and the start objects of the instances: entity to its placement and generation. */
    std::unordered_map<EntityId, std::pair<std::uint32_t, std::uint64_t>> m_memberOf;
    /** The start objects of the running stages: entity to its member slice. */
    std::unordered_map<EntityId, std::string> m_objects;
    /** Stage QG5c3: the proximity objects of the running stages (an entity id, or an id with no entity). */
    std::map<EntityId, Proximity> m_proximity;
    std::multimap<WorldClock::time_point, Due> m_due;
    std::vector<Event> m_events;
    std::vector<EncounterPayout> m_payouts;
    std::optional<WorldClock::time_point> m_lastScan;
    EncounterCounts m_counts;
    LoadMeter m_scanLoad;
};

} // namespace soulwarden::backend::world

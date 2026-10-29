#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gamedata/creature_data.h"
#include "state/store.h"

// The encounter recipes of the quests (stage QG5c2, docs/systems/encounters.md): the file encounter-data.json of
// tools/gamedata/encounters.sh. Each recipe is a quest encounter of a placement of the main world: its root slice (the record of the root
// entity), its manager, its participant tracker and detector, its stages in the order of the manager (each with its spawns, its objective,
// its actions), and its parent spawner (the respawn of a new instance). The members are the slices of the spawns: a creature member (its
// kind is in creatures.json, "members") or a start object (its template is in interaction-data.json and its row in gathering-data.json,
// "encounterObjects"). Without the file the world has no encounter, and the rest of the backend works. Stage QG5c3 (still format 1; a
// backend of before ignores the new members): the child stages, the spawned objective, the detector shapes, the proximity objects (their
// radius; an area trigger with no Interact component), the members that end themselves (lifetime), and the direct creature members.
// Stage QG5c5 (still format 1): the child stages that wait for their parent (WaitForParentObjective), the DestroySliceAction, the group
// members (one part for each creature spawner of the member slice, with its place and its patrol path), the spawns of an alias (the slice
// that the tool chose and the alias), the slices of a tracked alias, the direct stationary members (the core shells), and the season of a
// recipe. A backend of before ignores these members and must not take this file: it would run a group as one creature.

namespace soulwarden::backend::gamedata {

/** The file name of the encounter recipes in the game data folder. */
inline constexpr const char* kEncounterDataFile = "encounter-data.json";

enum class EncounterObjectiveKind : std::uint8_t {
    /** InvasionWaveObjective with a time limit and succeedOnTimeout: the stage succeeds at the end of its time. */
    timed,
    /** SliceDestroyedObjective: m_requiredCount destroyed spawns of the stage (its tracked slice, else each spawn). */
    destroyed,
    /** Stage QG5c3: SliceSpawnedObjective: m_requiredCount spawns of its tracked slice were made (the spawn of Helvan). */
    spawned,
    /**
     * Stage QG5c4: EntityActiveStateChangedObjective: the change of the active state of an entity of the slice that a detection volume
     * sends when a player enters it (the trigger: the Trigger_Start at the top of a tarot tower). The stage ends when a character is in a
     * shape of its trigger.
     */
    entityState,
    /**
     * Stage QG5c5: WaitForParentObjective: a child stage that ends with its parent (the sustain waves of the Darkness monoliths). The client
     * makes no task for it (static read D1 of stage QG5c4, 0x7C7DF0); a parent does not wait for it.
     */
    waitParent,
    /**
     * Stage QG9a: CutsceneFinishedObjective of an instance recipe (the IGC of a story instance: m_cutsceneName, m_timeLimit, m_succeedOnTimeout):
     * the stage ends at the end of the cutscene of a participant (6705 after its grant) or at its time limit.
     */
    cutscene,
    /**
     * Stage QG9a: EncounterEventObjective of an instance recipe (m_eventId Intro_Complete): no slice or table of the client sends the event;
     * the runner meets it at the start of its stage (a rule of this project).
     */
    event,
    /** A class that the runner does not run (the recipe is refused by the tool). */
    other,
};

enum class EncounterMemberKind : std::uint8_t {
    creature,
    object,
    unknown,
};

/** One place of a spawn: a spawn location of the slice in the world (the placement times the spawner times the location). */
struct EncounterPlace {
    state::WorldPosition position;
    float yaw{0.0F};
};

/** One entry of the spawn timeline of a stage. */
struct EncounterSpawn {
    /** The member slice (the key of EncounterData::members) and its index. */
    std::string member;
    std::uint32_t memberIndex{0};
    /** m_spawnDelay (seconds of the data) after the start of the stage. */
    std::chrono::milliseconds delay{0};
    /** m_count: the members of this entry. */
    std::uint32_t count{1};
    /** The spawn locations; empty: the place of the root. A count above the locations takes them in turn. */
    std::vector<EncounterPlace> locations;
    /** Stage QG5c5: the alias of a spawn of an alias (lower case; `member` is the slice of it that the tool chose). Empty: no alias. */
    std::string alias;
    /**
     * Stage QG9a ("skip" of the tool): a start object of an instance recipe that no objective tracks (the crystal of Medea: its role is in the
     * boss tree, which the client does not have): the runner does not make it.
     */
    bool skip{false};
};

/**
 * Stage QG5c5: a DestroySliceAction of a stage: the members of the instance whose slice is `slice`, `alias`, or one of `aliasSlices` go (no
 * count, no reward). The monoliths remove the corruption hazard of their setup (alias_darkness_corruption_hazard_major).
 */
struct EncounterDestroy {
    std::string slice;
    std::string alias;
    std::vector<std::string> aliasSlices;
};

/** Stage QG5c3: one shape of the detector of the participants (a DetectionVolumeComponent of the entity of the ParticipantTracker). */
struct EncounterShape {
    enum class Kind : std::uint8_t { sphere, cylinder, aabb };
    Kind kind{Kind::sphere};
    /** The radius of a sphere or of a cylinder, the height of a cylinder (centered on `center`), the half sizes of an axis box. */
    float radius{0.0F};
    float height{0.0F};
    std::array<float, 3> extents{0.0F, 0.0F, 0.0F};
    /** m_center: the offset from the root (world axes). */
    std::array<float, 3> center{0.0F, 0.0F, 0.0F};
};

/**
 * Stage QG5c4: the trigger of an EntityActiveStateChangedObjective: a detection volume of the slice (its shapes at their offset from its
 * place, in world axes) whose event component changes the target entity when a player enters it.
 */
struct EncounterTrigger {
    std::string entity;
    state::WorldPosition position;
    std::vector<EncounterShape> shapes;
    /** m_detectDeadTargets of the event component: a dead character in the volume counts too. */
    bool deadTargets{false};
    /** The distance from the place that holds each shape (the query of the position index). */
    float reach{0.0F};
};

struct EncounterObjective {
    EncounterObjectiveKind kind{EncounterObjectiveKind::other};
    std::string type;
    /** m_timeLimit (seconds); 0: none. */
    float timeLimit{0.0F};
    bool succeedOnTimeout{false};
    std::uint32_t requiredCount{0};
    /** m_trackedSlice or m_trackedAlias (lower case); empty: each spawn of the stage counts. */
    std::string tracked;
    /** Stage QG5c5: the slices of a tracked alias (lower case): a part of a group counts when its body is one of them. */
    std::vector<std::string> trackedSlices;
    std::string title;
    std::string description;
    /** Stage QG9a: the cutscene of a cutscene objective (its text and the CRC-32 of its lower-case text: the u32 of 6703 and 6705). */
    std::string cutscene;
    std::uint32_t cutsceneCrc{0};
    /** Stage QG9a: the event of an event objective (m_eventId). */
    std::string eventId;
    /** Stage QG5c4: the triggers of an entity-state objective (a character in one of their shapes ends the stage). */
    std::vector<EncounterTrigger> triggers;
    /**
     * Stage QG5c4 (static read D of stage QG5c4, 0x44650F0, 0x26F1FD0): the index of the leaf task of the objective in the task tree that
     * the client makes for the root (its wrapper is leaf + 1; -1: no task), and the target of the leaf (m_requiredCount of a destroyed or
     * a spawned objective; 1 for a timed or an entity-state objective).
     */
    std::int32_t leaf{-1};
    std::uint32_t target{1};
};

struct EncounterStage {
    /** The stage entity of the slice and the id of its EncounterComponent (the target of message 1278). */
    std::uint64_t entity{0};
    std::uint64_t component{0};
    std::string name;
    /** The slot of its EncounterComponent: the stage state 2133. */
    std::uint32_t slot{0};
    bool userWave{false};
    /** The collection index of a user-wave stage (client 0x26F1FD0: 0, 1, ... in the stage order), -1 for a stage with no user wave. */
    std::int32_t userIndex{-1};
    EncounterObjective objective;
    /**
     * Stage QG5c4: the other objectives of the stage (m_objectives after the first one: a destroyed or a spawned objective with no time
     * limit, one for each tracked member slice). The stage meets its count when `objective` and each of these are met.
     */
    std::vector<EncounterObjective> more;
    std::vector<EncounterSpawn> spawns;
    /** A KillSpawnsAction in m_startActions or m_endActions: the members of the encounter go (no reward). */
    bool killSpawnsAtStart{false};
    bool killSpawnsAtEnd{false};
    /** Stage QG5c5: the DestroySliceActions of m_startActions and of m_endActions. */
    std::vector<EncounterDestroy> destroyAtStart;
    std::vector<EncounterDestroy> destroyAtEnd;
    /**
     * Stage QG5c3: the child stages (m_stages of the stage: indexes into the stages of the recipe; they run one after the other), the stage
     * whose m_stages names this one (-1: a stage of the manager), and m_childStagesOptional (the stage can end before its child stages).
     */
    std::vector<std::uint32_t> children;
    std::int32_t parent{-1};
    bool childrenOptional{false};
};

struct EncounterParent {
    /** "area", "point", or "prefab". */
    std::string kind;
    bool maintainPopulation{false};
    bool spawnOnEnable{false};
    std::uint32_t liveCount{0};
    /** The respawn of a new instance after the end of one (seconds): the respawn range of an area spawner, the cooldowns of a point. */
    float respawnMin{0.0F};
    float respawnMax{0.0F};
};

struct EncounterRecipe {
    /** The durable key: the placement UUID and the spawner entity of the parent ("UUID/ENTITY"). */
    std::string key;
    std::string slice;
    std::array<std::uint8_t, 16> asset{};
    std::uint32_t sub{2};
    std::uint64_t root{0};
    std::string rootName;
    std::string layer;
    state::WorldPosition position;
    std::array<float, 4> rotation{0.0F, 0.0F, 0.0F, 1.0F};
    float yaw{0.0F};
    /** The slots of the manager state 6786, the PositionInTheWorld state, and the root Objectives state. */
    std::uint32_t managerSlot{0};
    std::uint32_t placeSlot{0};
    std::uint32_t objectivesSlot{0};
    bool autoActivate{false};
    std::chrono::milliseconds activateDelay{0};
    std::uint32_t managerGrace{0};
    bool destroyOnCompletion{false};
    bool autoPerceive{false};
    /** m_overrideAggroDelay and m_aggroDelayOverride (seconds): a member takes a participant as its target after this time from its spawn. */
    bool overrideAggroDelay{false};
    float aggroDelay{0.0F};
    /** The ParticipantTracker component (its id), m_gracePeriodSec, and m_participantAddedGameEventId. */
    std::uint64_t trackerComponent{0};
    std::uint32_t participantGrace{0};
    std::string participantEvent;
    /** The detector sphere of the participants: m_Radius and m_strFilter. */
    float detectorRadius{0.0F};
    std::string detectorFilter;
    /**
     * Stage QG5c3: the shapes of the detector (a character in one of them is in the detector; a file of before has one sphere of
     * `detectorRadius`), and the distance from the root that holds each shape (the query of the position index).
     */
    std::vector<EncounterShape> detectorShapes;
    float detectorReach{0.0F};
    /** m_universalSuccessGameEventId (empty: none) and the quest events of the recipe (lower case). */
    std::string successEvent;
    std::vector<std::string> questEvents;
    EncounterParent parent;
    /** The top-level stages (indexes into `stages`) in the order of the manager. */
    std::vector<std::uint32_t> stageOrder;
    std::vector<EncounterStage> stages;
    /** The number of the user-wave stages (the encounter objectives of the client). */
    std::uint32_t userWaves{0};
    /** Stage QG5c4: the leaf tasks of the task tree of the client (the outer container has the index 2 * taskLeaves + 1). */
    std::uint32_t taskLeaves{0};
    std::string pattern;
    /**
     * Stage QG5c5: the season of a seasonal event ("darkness": the Darkness events of slices/darkness/). Empty: none. The runner takes such a
     * recipe only with its season rule (encounter_rules::kDarknessAtAllTimes).
     */
    std::string season;
    /**
     * Stage QG9a (format 2): an instance recipe: the encounter of the mode root slice of a game mode (its GameModeId and its map). It runs only
     * in an instance of its mode (Encounters::start_space), never at a placement of the main world. Empty: a recipe of the main world.
     */
    std::string mode;
    std::string map;
    /** Stage QG9a: the daily alternative of the encounter of a mode (the tool refuses it). */
    bool daily{false};
    /** The causes of the data that the runner does not run (tools/gamedata/encounters.py). Empty: no cause of the data. */
    std::vector<std::string> refused;
};

/**
 * Stage QG9a (format 2, the member instanceObjects): an object of the mode root slice of a game mode that is not an encounter (the exit
 * portal, the fog wall, the checkpoint, the quest gatherable): its mode, its map, its key ("ROOT_PLACEMENT/SPAWNER_ENTITY"), its slice, its
 * world place and yaw, and m_spawnOnInitialize of its spawner.
 */
struct InstanceObject {
    std::string mode;
    std::string map;
    std::string key;
    std::string slice;
    std::string spawner;
    state::WorldPosition position;
    float yaw{0.0F};
    bool onInit{false};
};

/**
 * Stage QG5c5: one part of a group member: one creature spawner of the member slice (its kind in creatures.json is "SLICE#N"), its body, its
 * place in the member slice (the offset in the axes of the slice and its yaw), and its patrol path in the coordinates of the member slice
 * (no points: none).
 */
struct EncounterPart {
    std::string name;
    std::string vitals;
    std::string category;
    std::string body;
    std::vector<std::string> bodySlices;
    std::array<float, 3> offset{0.0F, 0.0F, 0.0F};
    float yaw{0.0F};
    PatrolRoute route;
};

struct EncounterMember {
    std::string slice;
    EncounterMemberKind kind{EncounterMemberKind::unknown};
    std::string vitals;
    std::string category;
    std::string body;
    std::vector<std::string> bodySlices;
    std::string variant;
    std::string row;
    std::string event;
    std::string tag;
    bool onlyOnQuest{false};
    bool proximity{false};
    std::string cause;
    /**
     * Stage QG5c3: the sphere of a proximity object (DetectionVolumeSphereRadius of its row, else the sphere of its slice), and false for an
     * object with no Interact component (the area triggers: the backend makes no record of it); the seconds after which a member of the
     * kind unknown ends itself (the smoke burst, the powder keg: 0 for none); a creature member that is the creature slice itself.
     */
    float radius{0.0F};
    bool interact{true};
    float lifetime{0.0F};
    bool direct{false};
    /** Stage QG5c5: a direct stationary member (a core shell: a Vitals component and no movement; a stationary target of the creature data). */
    bool stationary{false};
    /** Stage QG5c5: the parts of a group member (empty: one creature, the member itself). */
    std::vector<EncounterPart> parts;
};

class EncounterData {
public:
    /** The largest number of recipes, of stages of a recipe, of spawns of a stage, and of members of a spawn entry (load bounds). */
    static constexpr std::size_t kMaxRecipes = 4096;
    static constexpr std::size_t kMaxStages = 64;
    static constexpr std::size_t kMaxSpawns = 64;
    static constexpr std::uint32_t kMaxCount = 32;
    /** Stage QG5c5: the largest number of the parts of a group member and of the waypoints of the path of a part. */
    static constexpr std::size_t kMaxParts = 32;
    static constexpr std::size_t kMaxWaypoints = 64;
    static constexpr float kMaxSeconds = 3600.0F;

    /** Reads encounter-data.json. A file that is not complete is not used, as a whole. @return False with `error()`. */
    bool load(const std::string& path);
    bool load_text(const std::string& text);

    [[nodiscard]] bool loaded() const {
        return m_loaded;
    }
    [[nodiscard]] const std::string& error() const {
        return m_error;
    }
    [[nodiscard]] const std::vector<EncounterRecipe>& recipes() const {
        return m_recipes;
    }
    [[nodiscard]] const std::vector<EncounterMember>& members() const {
        return m_members;
    }
    [[nodiscard]] const EncounterMember* member(std::string_view slice) const;
    /** Stage QG9a: the objects of the mode root slices (format 2; a file of format 1 has none). */
    [[nodiscard]] const std::vector<InstanceObject>& instance_objects() const {
        return m_instanceObjects;
    }

private:
    bool fail(std::string cause);

    bool m_loaded{false};
    std::string m_error;
    std::vector<EncounterRecipe> m_recipes;
    std::vector<EncounterMember> m_members;
    std::vector<InstanceObject> m_instanceObjects;
    std::map<std::string, std::uint32_t, std::less<>> m_memberIndex;
};

} // namespace soulwarden::backend::gamedata

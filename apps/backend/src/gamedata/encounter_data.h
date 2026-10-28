#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "state/store.h"

// The encounter recipes of the quests (stage QG5c2, docs/systems/encounters.md): the file encounter-data.json of
// tools/gamedata/encounters.sh. Each recipe is a quest encounter of a placement of the main world: its root slice (the record of the root
// entity), its manager, its participant tracker and detector, its stages in the order of the manager (each with its spawns, its objective,
// its actions), and its parent spawner (the respawn of a new instance). The members are the slices of the spawns: a creature member (its
// kind is in creatures.json, "members") or a start object (its template is in interaction-data.json and its row in gathering-data.json,
// "encounterObjects"). Without the file the world has no encounter, and the rest of the backend works.

namespace soulwarden::backend::gamedata {

/** The file name of the encounter recipes in the game data folder. */
inline constexpr const char* kEncounterDataFile = "encounter-data.json";

enum class EncounterObjectiveKind : std::uint8_t {
    /** InvasionWaveObjective with a time limit and succeedOnTimeout: the stage succeeds at the end of its time. */
    timed,
    /** SliceDestroyedObjective: m_requiredCount destroyed spawns of the stage (its tracked slice, else each spawn). */
    destroyed,
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
    std::string title;
    std::string description;
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
    std::vector<EncounterSpawn> spawns;
    /** A KillSpawnsAction in m_startActions or m_endActions: the members of the encounter go (no reward). */
    bool killSpawnsAtStart{false};
    bool killSpawnsAtEnd{false};
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
    /** m_universalSuccessGameEventId (empty: none) and the quest events of the recipe (lower case). */
    std::string successEvent;
    std::vector<std::string> questEvents;
    EncounterParent parent;
    /** The top-level stages (indexes into `stages`) in the order of the manager. */
    std::vector<std::uint32_t> stageOrder;
    std::vector<EncounterStage> stages;
    /** The number of the user-wave stages (the encounter objectives of the client). */
    std::uint32_t userWaves{0};
    std::string pattern;
    /** The causes of the data that the runner does not run (tools/gamedata/encounters.py). Empty: no cause of the data. */
    std::vector<std::string> refused;
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
};

class EncounterData {
public:
    /** The largest number of recipes, of stages of a recipe, of spawns of a stage, and of members of a spawn entry (load bounds). */
    static constexpr std::size_t kMaxRecipes = 4096;
    static constexpr std::size_t kMaxStages = 64;
    static constexpr std::size_t kMaxSpawns = 64;
    static constexpr std::uint32_t kMaxCount = 32;
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

private:
    bool fail(std::string cause);

    bool m_loaded{false};
    std::string m_error;
    std::vector<EncounterRecipe> m_recipes;
    std::vector<EncounterMember> m_members;
    std::map<std::string, std::uint32_t, std::less<>> m_memberIndex;
};

} // namespace soulwarden::backend::gamedata

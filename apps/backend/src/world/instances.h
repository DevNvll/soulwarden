#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "gamedata/game_modes.h"
#include "state/store.h"
#include "world/entity.h"
#include "world/ids.h"
#include "world/world_clock.h"

// The instances (stage DG1, docs/systems/instances.md): the story instances of the quests and the expeditions. An instance is a space of
// the one world (decision D8 of research dungeons.md: model I1, an orchestrator pick that the user can change): one thread, one clock, one
// store. Each entity has its space (Entity::space; 0 is the main world); the position index and the interest of a session use the space
// with the position. The instance is a world object (rule MP-1): its id, its map row of the game modes, its level, its members by
// character id, the return place of each member, and the start time. A session stores no instance state.
//
// One path moves a character between two spaces: World::move_to_space (rule MP-2). The session of the mover gets the level change
// (SessionSink::space_changed: RepSession::change_level), and each session that observed the mover in the old space gets the stop of its
// interest scan (the mover is not in the space of that session).
//
// The rows (rule MP-7): one transaction at the entry (the position row of the character gets the return place, and the member row), and
// one at the exit (the position row gets the return place, and the member row goes). While the character is in an instance its position
// row keeps the main world position: the world writes no position of an instance space. A backend stop ends each instance: the first tick
// (or the first world entry) of the next start puts each character of a member row at its return place and removes the row, and a session
// that ends in an instance does the exit (decision D9 of research dungeons.md, an orchestrator pick: a disconnect in an instance brings the
// character back to its return place).
//
// Stage DG1 had no content in an instance; stage DG2 gives an own level its level data, stage QG9a the entrance of a story mode, and
// stage DG3 the entrance of an expedition (its checks, its checkpoints, the objects of its level set). The development command
// "instance enter" (the development routes, with the harness gate) stays.

namespace soulwarden::backend::gamedata {
struct DungeonEntrance;
} // namespace soulwarden::backend::gamedata

namespace soulwarden::backend::world {

class World;
struct Entity;
class InteractionHandler;

/**
 * Stage QG9a: the result of an entry request at the entrance of a game mode: the value of eDungeonActivateResponse, the u32 of 3094 (enum
 * registration 0x54D473F to 0x54D484D; research dungeons.md 3.3). The backend gives Success, and EntranceActivationFailure for each refusal
 * of a check that has no value of its own [agent].
 */
namespace dungeon_result {
inline constexpr std::uint32_t kSuccess = 0;
/** Stage DG3: the group is smaller than the minimum group size of the entry (decision D7: 1). */
inline constexpr std::uint32_t kNotEnoughMembers = 1;
/** Stage DG3: the mode needs an item of RequiredConsumableItemClasses. */
inline constexpr std::uint32_t kMissingReqItem = 3;
/** Stage DG3: the group has more members than TeamCapacity of the mode. */
inline constexpr std::uint32_t kTooManyMembers = 5;
inline constexpr std::uint32_t kSoloGroupFailure = 7;
inline constexpr std::uint32_t kPendingActivation = 8;
inline constexpr std::uint32_t kEntranceActivationFailure = 10;
inline constexpr std::uint32_t kGroupAlreadyInDungeon = 11;
/** Stage DG3: a mutation tier (the difficulty 1 to 3 of the request) that the mode or the character does not have. */
inline constexpr std::uint32_t kMutationDifficultyReq = 14;
} // namespace dungeon_result

/**
 * Stage DG3 (decision D7 of research dungeons.md, an orchestrator pick that the user can change): the minimum group size of an entry is 1
 * for each mode; MinGroupSize of the data is 3 for each expedition, and the client checks it only while its remote setting
 * javelin.enable-dungeon-group-restriction is true (the backend sends false: server/http/routes.h).
 */
inline constexpr std::uint32_t kMinimumGroupSize = 1;

/** Stage QG9a: why a member left its instance (the log and the counts). */
enum class InstanceLeave : std::uint8_t {
    /** The development command "instance return" (and the admin command). */
    command,
    /** The exit portal of the instance (ExitDungeon, action 74). */
    exitPortal,
    /** The request 3309 of the client (RequestLeaveGameMode). */
    request,
};

[[nodiscard]] const char* instance_leave_text(InstanceLeave cause);

/** The level of the main world (the wire constants of the level information, protocol notes 4.1). */
inline constexpr const char* kMainLevel = "newworld_vitaeeterna";
/**
 * The terrain project of the main world (game data folder sharedassets/coatlicue/<name>). The client starts its terrain system only when
 * this text is not empty (client code at image RVA 0x6448CD0). Without the terrain the loading screen does not end.
 */
inline constexpr const char* kMainTerrain = "coatlicue/newworld_vitaeeterna";

/** The level of a space: the texts of the level information (message 1635) of a session in it. */
struct SpaceLevel {
    std::string level;
    std::string terrain;
};

struct InstanceMember {
    std::string characterId;
    /** The place in the main world where the character goes at the exit: its position at the entry. */
    state::WorldPosition returnPlace;
    WorldClock::time_point entered{};
    /** Stage DG3: the end of the cooldown of each checkpoint that the member used for a respawn (the id of the checkpoint). */
    std::map<std::string, WorldClock::time_point> checkpointCooldowns;
};

/**
 * Stage DG3 (docs/systems/instances.md, "The checkpoints of an expedition"): a dungeon checkpoint of the instance: a checkpoint of the mode
 * root of its map in the level set (gamedata::ModeCheckpoint), its server child, its home point (the reference, the name, the respawn place,
 * the cooldown, the id), and its activation. A member that comes into its activation sphere activates it for the instance [data: the
 * detection volume of the server facet of its HomeComponent]; each member then has it in its home point list.
 */
struct InstanceCheckpoint {
    /** The index of the checkpoint in the checkpoints of the level set. */
    std::uint32_t index{0};
    /** The server child of the checkpoint (0: not made). */
    EntityId entity{0};
    /** The home point (cooldownEndNs 0: the list of a member sets it). */
    HomePoint point;
    state::WorldPosition center;
    float radius{0.0F};
    std::chrono::nanoseconds cooldown{0};
    bool active{false};
};

/**
 * Stage QG9d: a quest object of the mode root of an instance (the heartgems of QuestCaerDun): an object of the mode root slice with a
 * gatherable row whose game event a task of the mode names. The instance system makes it while a member has that task active, and it goes
 * at its gather [agent: the mode script that makes it is not in the client; data: the objects and the order of the tasks].
 */
struct InstanceQuestObject {
    /** The index of the object in EncounterData::instance_objects(). */
    std::uint32_t object{0};
    /** The key of the task of its event ("e:" and the lower case event: Quests::mode_task_active). */
    std::string key;
    /** The server child of the object; 0: not made. */
    EntityId entity{0};
    bool gathered{false};
};

struct Instance {
    /** The id of the instance: 1, 2, ... in one backend process (the allocator of the instance system). */
    std::uint64_t id{0};
    /** The space of the instance (Entity::space of its members). */
    SpaceId space{kMainSpace};
    /** The map row of the game modes (GameModeMapId) and its mode (GameModeId). */
    std::string map;
    std::string mode;
    SpaceLevel level;
    /** The start place of the map (team 0) and its heading in degrees. */
    state::WorldPosition start;
    float heading{0.0F};
    /** The largest number of members: TeamCapacity of the mode, 1 for a mode with the capacity 0 (a solo trial). */
    std::uint32_t capacity{1};
    WorldClock::time_point started{};
    /** The members by character id. */
    std::map<std::string, InstanceMember> members;
    /**
     * Stage QG9a: an instance with content (the encounters of its mode, world/encounters.h; its exit portal) and no member: it ends at this
     * time (TimerThresholdDefault of the mode on the world clock). No value: a member is in it, or it has no content (it ends at once).
     */
    std::optional<WorldClock::time_point> emptyUntil;
    /** Stage QG9a: the characters that left the instance while it has content: an entry of one of them at the entrance joins it again. */
    std::set<std::string> former;
    /** Stage QG9a: the content of the mode started in the space (the encounters and the objects of the instance). */
    bool content{false};
    /** Stage QG9a: the content of the mode succeeded (each encounter of the mode in the space): no join again of a former member. */
    bool succeeded{false};
    /** Stage QG9a: the objects of the instance that the instance system made (the exit portal: server children of the interaction system). */
    std::vector<EntityId> objects;
    /**
     * Stage QG9a: the game mode entity of the instance in the GameModeParticipant state of its members (an actor of the id allocator and G of
     * it; the backend makes no record of it [agent: the client has no GameMode slice record of the original server to compare]).
     */
    std::array<std::uint8_t, 16> modeActor{};
    EntityId modeEntity{0};
    /** Stage QG9d: the quest objects of the mode root (InstanceQuestObject). */
    std::vector<InstanceQuestObject> questObjects;
    /** Stage DG3: the dungeon checkpoints of the mode root of its map (an own level with a level set). */
    std::vector<InstanceCheckpoint> checkpoints;
    /**
     * Stage QG9d: the encounters of the mode wait (Encounters::hold_space) until each quest object is gathered or a member has an active
     * task of the mode that names a recipe of the space (a later step of the quest: an entry after the objects).
     */
    bool held{false};
};

enum class InstanceError {
    none,
    /** The backend has no game modes (game-modes.json). */
    noData,
    /** The game modes have no map with this id (or the mode has more than one map). */
    unknownMap,
    notInWorld,
    /** The character is in an instance: it returns first. */
    inInstance,
    /** The character is in no instance. */
    notInInstance,
    /** The instance to join is not there. */
    unknownInstance,
    /** The instance to join is of a different map. */
    wrongMap,
    /** The instance to join has its largest number of members. */
    full,
    /** The database did not accept the rows. Nothing changed. */
    storage,
    /**
     * Code review of stage DG1, finding DG1R-R4: the place of the character is not a place of a travel (travel_position: a height below
     * -99 m, for example): it cannot be a return place. Nothing changed.
     */
    badReturnPlace,
};

[[nodiscard]] const char* instance_error_text(InstanceError error);

struct InstanceResult {
    InstanceError error{InstanceError::none};
    /** The instance of the entry, or of the exit (a copy: the instance can end at the exit). */
    std::optional<Instance> instance;
    /** The place of the character after the change: the start of the map, or the return place. */
    state::WorldPosition place;
    /** True when the exit ended the instance (no member stays). */
    bool ended{false};
};

struct InstanceCounts {
    std::uint64_t made{0};
    std::uint64_t ended{0};
    std::uint64_t entries{0};
    std::uint64_t exits{0};
    /** Members that left with their entity (a session that ended, a removal): decision D9. */
    std::uint64_t departures{0};
    /** Member rows of an earlier start that the recovery put at their return place. */
    std::uint64_t recovered{0};
    std::uint64_t refused{0};
    /** Stage QG9a: the entry requests at an entrance (accepted and refused), the joins again, the leaves by the portal and by 3309, the
     *  empty instances that their timer ended. */
    std::uint64_t entranceRequests{0};
    std::uint64_t entranceRefused{0};
    std::uint64_t rejoined{0};
    std::uint64_t portalLeaves{0};
    std::uint64_t requestLeaves{0};
    std::uint64_t emptyEnded{0};
    /** Stage QG9d: the quest objects of the mode roots that the instances made and that a member gathered. */
    std::uint64_t questObjectsMade{0};
    std::uint64_t questObjectsGathered{0};
    /** Code review of stage QG9d, finding QG9DR-R3: the gathered quest objects whose task a member has again (a reset): made again. */
    std::uint64_t questObjectsAgain{0};
    /** Stage DG3: the checkpoints that the instances made, that a member activated, and the respawns at a checkpoint. */
    std::uint64_t checkpointsMade{0};
    std::uint64_t checkpointsActivated{0};
    std::uint64_t checkpointRespawns{0};
};

class Instances {
public:
    Instances(World& world, state::Store& store, const gamedata::GameModes* data);
    ~Instances();
    Instances(const Instances&) = delete;
    Instances& operator=(const Instances&) = delete;

    /** @return The game modes, or null when the backend has none. */
    [[nodiscard]] const gamedata::GameModes* data() const {
        return m_data != nullptr && m_data->loaded() ? m_data : nullptr;
    }

    /**
     * A character enters an instance of a map (the development command "instance enter"). With `join` 0 the system makes a new instance
     * for the character; with the id of an instance the character joins it (the same map, a free member place). The checks use the
     * character of the request (rule MP-5): it is in the world and in the main world. The rows of the entry are one transaction; with no
     * commit nothing changes. Then the system makes or changes the instance, and World::move_to_space moves the character to the start
     * of the map.
     */
    [[nodiscard]] InstanceResult enter(const std::string& characterId, const std::string& mapId, std::uint64_t join = 0,
                                       const std::function<void()>& committed = {});

    /**
     * A character leaves its instance and goes to its return place (the development command "instance return"). The rows of the exit
     * are one transaction; with no commit nothing changes. An instance with no member ends (this stage: an instance has no content).
     */
    [[nodiscard]] InstanceResult leave(const std::string& characterId, InstanceLeave cause = InstanceLeave::command);

    /**
     * Stage QG9a: the checks of an entry request at the entrance of a game mode (the handler of the Enter option, action 72): the backend has
     * the game modes, the mode of the entrance (m_gameMode) has one map, the encounter system runs the content of the mode
     * (Encounters::runs_mode), the character is in the main world and in no instance, and its level is RequiredLevel of the mode or more.
     * @return The result of 3094 (dungeon_result).
     */
    [[nodiscard]] std::uint32_t entrance_result(const Entity& character, const gamedata::DungeonEntrance& entrance,
                                                std::uint32_t difficulty = 0) const;
    /** Stage DG3: @return The number of the group of a character for an entry (the backend has no group: 1, the character). */
    [[nodiscard]] std::uint32_t group_size(const Entity& character) const;
    /**
     * Stage DG3: @return True when the backend runs the content of a mode (the one rule of the entrance and of the support manifest): the
     * game modes have one map of the mode, and an expedition (IsDungeon) of an own level has an accepted data set of its level
     * (Levels::available, the first read of the set), each other mode the content of the encounter system (Encounters::runs_mode). The
     * setting of QuestMedusa is a check of its own.
     */
    [[nodiscard]] bool mode_runs(std::string_view mode) const;
    /**
     * Stage QG9a: the entry of a character at the entrance of a mode: it joins again the empty instance of the mode that it left (the timer
     * of the instance runs), else it enters a new instance (enter). The checks of entrance_result first.
     */
    [[nodiscard]] InstanceResult enter_mode(const std::string& characterId, const std::string& mode, const std::function<void()>& committed = {});
    /**
     * Stage QG9a (the evaluator of TaskGameMode, docs/systems/quests.md): @return True while the character is a member of an instance of the
     * mode (IsInGameMode 0x32BA3B0 compares the mode CRC of each active game mode; the comparison of the text ignores the case).
     */
    [[nodiscard]] bool in_mode(const Entity& character, std::string_view mode) const;
    /** Stage QG9a: the encounter system reports the success of the content of the instance of a space (the log; the counts). */
    void content_succeeded(SpaceId space);
    /**
     * Stage QG9d: @return The slice of a quest object of an instance (InstanceQuestObject) that is there, or null: the gathering system takes
     * its GatherOption with the row of the slice (gathering-data.json, member encounterObjects).
     */
    [[nodiscard]] const std::string* quest_object(EntityId object) const;
    /** Stage QG9d: the gather of a quest object completed (its events went to the gatherer): the object goes in the next tick. */
    void quest_object_gathered(EntityId object);
    /**
     * Stage DG3: the respawn of a dead member at the checkpoint of its instance with this id (the request 1938 with random 0 and the id of a
     * home point). @return The respawn place of the checkpoint, or no value: no such active checkpoint of the instance of the character, or
     * its cooldown for this member runs (the caller then takes the start of the map). A respawn at a checkpoint starts its cooldown for the
     * member (m_respawnCooldown of its HomeComponent), and the home point list of the member shows it.
     */
    [[nodiscard]] std::optional<state::WorldPosition> use_checkpoint(const Entity& character, const std::string& id);

    /**
     * The entity of a member leaves the world (World::remove_entity: a session that ended, a removal; decision D9): the exit rows, the
     * end of its membership, and the end of an instance with no member. The entity does not move: it goes away.
     */
    void character_left(const Entity& character);

    /**
     * The member rows of an earlier start (a backend stop ends each instance): each character goes to its return place, and its row goes,
     * in one transaction. Each tick and each world entry call it; after a commit later calls do nothing (code review of stage DG1, finding
     * DG1R-R5: a refused transaction tries again). The exit rows of a departure that the database refused try again here too.
     */
    void recover();
    /** Code review of stage DG1, finding DG1R-R5: @return True while the exit rows of a departure of the character wait for the database. */
    [[nodiscard]] bool exit_pending(const std::string& characterId) const {
        // Code review of stage QG9a, finding QG9AR-R1: also a member row of an earlier start that the database did not let the recovery
        // remove (its return place would undo a later save of the character).
        return m_pendingExits.contains(characterId) || m_unrecovered.contains(characterId);
    }

    void tick();

    [[nodiscard]] bool has_space(SpaceId space) const;
    /** @return The level of a space: the main world for kMainSpace; null for a space that is not there. */
    [[nodiscard]] const SpaceLevel* level_of(SpaceId space) const;
    [[nodiscard]] const Instance* find(std::uint64_t id) const;
    [[nodiscard]] const Instance* instance_of_space(SpaceId space) const;
    /** @return The instance of a member, or null. */
    [[nodiscard]] const Instance* instance_of(const std::string& characterId) const;
    [[nodiscard]] const std::map<std::uint64_t, Instance>& instances() const {
        return m_instances;
    }
    [[nodiscard]] const InstanceCounts& counts() const {
        return m_counts;
    }

private:
    void end_instance(std::uint64_t id, const char* cause);
    /** Stage QG9a: a member went from an instance (leave, character_left): the instance ends now, or waits empty for its timer. */
    void member_gone(Instance& instance, const std::string& characterId, const char* cause);
    /** Stage QG9a: the content of the mode of a new instance: the encounters of its mode and its objects (the exit portal). */
    void start_content(Instance& instance);
    /** Stage QG9d: the quest objects of an instance: make one whose task a member has, remove a gathered one, end the wait of the encounters. */
    void look_quest_objects(Instance& instance);
    /** Stage DG3: the checkpoints of the mode root of a new instance of an own level (their server children and their home points). */
    std::size_t make_checkpoints(Instance& instance);
    /** Stage DG3: a member in the activation sphere of a checkpoint activates it; then the list of each member. */
    void look_checkpoints(Instance& instance);
    /** Stage DG3: the home point list of a character: the active checkpoints of its instance (none after its leave). */
    void home_points_changed(const std::string& characterId);
    /**
     * Stage QG9a: the membership of a character changed: its GameModeParticipant state (the entry of its instance, or none) and its
     * TaskGameMode tasks.
     */
    void membership_changed(const std::string& characterId);

    /** The exit rows of a member in one transaction: the position row gets the return place, and the member row goes. */
    [[nodiscard]] bool write_exit(const InstanceMember& member);

    World& m_world;
    state::Store& m_store;
    const gamedata::GameModes* m_data;
    SpaceLevel m_mainLevel{kMainLevel, kMainTerrain};
    std::uint64_t m_nextInstance{1};
    SpaceId m_nextSpace{1};
    std::map<std::uint64_t, Instance> m_instances;
    std::map<SpaceId, std::uint64_t> m_bySpace;
    std::map<std::string, std::uint64_t> m_byCharacter;
    bool m_recovered{false};
    /** Code review of stage DG1, finding DG1R-R5: the departures whose exit rows the database refused (they try again in recover). */
    std::map<std::string, InstanceMember> m_pendingExits;
    /** Code review of stage QG9a, finding QG9AR-R1: the characters of the member rows of an earlier start that the recovery did not remove. */
    std::set<std::string> m_unrecovered;
    InstanceCounts m_counts;
    /** Stage QG9a: the handlers of the entrance (OpenDungeonMenu, the Enter options) and of the exit portal (ExitDungeon). */
    std::unique_ptr<InteractionHandler> m_entranceHandler;
    std::unique_ptr<InteractionHandler> m_exitHandler;
};

} // namespace soulwarden::backend::world

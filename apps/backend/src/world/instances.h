#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "gamedata/game_modes.h"
#include "state/store.h"
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
// This stage has no content in an instance (no creature, no object, no level data: stage DG2). The entry is the development command
// "instance enter" (the development routes, with the harness gate) until stage DG3 gives the entrance of the game.

namespace soulwarden::backend::world {

class World;
struct Entity;

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
};

class Instances {
public:
    Instances(World& world, state::Store& store, const gamedata::GameModes* data);

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
    [[nodiscard]] InstanceResult enter(const std::string& characterId, const std::string& mapId, std::uint64_t join = 0);

    /**
     * A character leaves its instance and goes to its return place (the development command "instance return"). The rows of the exit
     * are one transaction; with no commit nothing changes. An instance with no member ends (this stage: an instance has no content).
     */
    [[nodiscard]] InstanceResult leave(const std::string& characterId);

    /**
     * The entity of a member leaves the world (World::remove_entity: a session that ended, a removal; decision D9): the exit rows, the
     * end of its membership, and the end of an instance with no member. The entity does not move: it goes away.
     */
    void character_left(const Entity& character);

    /**
     * The member rows of an earlier start (a backend stop ends each instance): each character goes to its return place, and its row goes,
     * in one transaction. The first tick and the first world entry call it; later calls do nothing.
     */
    void recover();

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
    InstanceCounts m_counts;
};

} // namespace soulwarden::backend::world

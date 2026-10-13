#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "gamedata/creature_data.h"
#include "world/entity.h"
#include "world/ids.h"
#include "world/position_index.h"
#include "world/world_clock.h"

// The creature system: the creatures of the world. A creature is an entity with no character
// and no session (world/entity.h). Only this system makes a creature, changes its health, and
// removes it. It changes the entity and marks the component state (World::touch): each session
// that observes the creature then sends the change (roadmap, rule MP-2).
//
// The spawn points (a rule of this project, protocol notes 4.14). The creature data has the
// places where the game has a creature. The world does not hold the creature of each place at
// all times: it makes the creature of a spawn point when a player is `spawnEnter` or less from
// the point, and it removes the creature when no player is `spawnLeave` or less from the
// creature. A spawn point has one creature at most.
//
// A creature of a spawn point that died, or that a command removed, does not come back by the
// distance rule: the spawn point waits. In this stage only a command makes it free again
// (reset_spawn_points). The respawn time of a later stage has its place here: a spawn point
// that waits has a time of the world clock (`respawnAt`) at which it is free again. The body of
// the dead creature can be in the world at that time (a player stayed near it): the point
// removes it before it makes its new creature.
//
// World thread only.

namespace soulwarden::backend::world {

class World;

enum class CreatureError {
    none,
    /** The backend has no creature data (the file of tools/gamedata/creatures.sh). */
    noData,
    unknownKind,
    /** The world has no creature with this id. */
    unknownCreature,
    /** The position is not a position of the world. */
    badPosition,
    /** The level table of the game has no row for the level. */
    badLevel,
    /** The health is not a number from 0 to the maximum health of the creature. */
    badHealth,
    /** The creature is dead: its health does not change. */
    dead,
};

struct CreatureResult {
    CreatureError error{CreatureError::none};
    /** The creature of the command. Null when `error` is not `none`, and after a removal. */
    Entity* entity{nullptr};
};

/** The condition of one spawn point. */
struct SpawnPointState {
    /** The creature of the point that is in the world. 0: none. */
    EntityId entity{0};
    /** True: the creature of the point died or a command removed it. The point makes no creature. */
    bool waiting{false};
    /**
     * The time at which a point that waits is free again. No value: only a command makes it
     * free. No code sets a time in this stage (the respawn time is stage 3.5a).
     */
    std::optional<WorldClock::time_point> respawnAt;
    /** Number of creatures that the point made. It selects the slice of an alias with more than one. */
    std::uint32_t spawns{0};
};

/** Counts of the creature system, for the log and for the tests. */
struct CreatureCounts {
    /** Creatures that the spawn points made, and that a command made. */
    std::uint64_t spawnedByPoint{0};
    std::uint64_t spawnedByCommand{0};
    /** Creatures that left the world: no player was near, or a command removed them. */
    std::uint64_t removedByDistance{0};
    std::uint64_t removedByCommand{0};
    /** Bodies of dead creatures that left the world at the respawn time of their spawn point. */
    std::uint64_t removedByRespawn{0};
    std::uint64_t deaths{0};
    /** The largest number of creatures in the world at one time. */
    std::size_t largest{0};
};

class Creatures {
public:
    /** @param data The creature data (game data). Null or not loaded: the world has no creature. */
    Creatures(World& world, const gamedata::CreatureData* data);

    [[nodiscard]] bool has_data() const {
        return m_data != nullptr && m_data->loaded();
    }
    [[nodiscard]] const gamedata::CreatureData* data() const {
        return m_data;
    }

    /**
     * Makes a creature of a kind at a position (a command). It has no spawn point: the distance
     * rule does not remove it.
     * @param kindId The id of a kind of the creature data.
     * @param level The level of the creature. No value: the level of the kind.
     * @param yaw Direction of the creature, in degrees.
     */
    CreatureResult spawn(const std::string& kindId,
                         const state::WorldPosition& position,
                         float yaw,
                         std::optional<std::uint32_t> level = std::nullopt);

    /**
     * Removes a creature from the world (a command). Its spawn point, when it has one, waits:
     * the distance rule does not make the creature again.
     */
    CreatureResult remove(EntityId id);

    /**
     * Sets the health of a creature. The health 0 is the death of the creature: it gets the
     * dead mark, and its spawn point waits. The entity stays in the world (the body).
     */
    CreatureResult set_health(EntityId id, float health);

    /** Sets the health of a creature to 0. */
    CreatureResult kill(EntityId id) {
        return set_health(id, 0.0F);
    }

    /**
     * A creature takes damage (the combat system, world/combat.h): its health goes down by the
     * damage, through set_health. A creature that lives after it plays the reaction action of
     * its slice. A damage that is not less than its health is its death.
     * @param damage The damage: a finite number that is not negative.
     */
    CreatureResult hurt(EntityId id, float damage);

    /**
     * Makes each spawn point that waits free again (a command): the distance rule makes its
     * creature again when a player is near. A dead creature of a point leaves the world.
     * @return The number of spawn points that waited.
     */
    std::size_t reset_spawn_points();

    /**
     * Sets the time of the world clock at which a spawn point that waits is free again: the
     * place of the respawn time (stage 3.5a calls it at the death of a creature). No effect for
     * a point that does not wait. From that time on, the point makes its creature again when a
     * player is near it (the distance rule): the body of the dead creature then leaves the
     * world first.
     * @return False when the creature data has no spawn point with this index.
     */
    bool respawn_at(std::uint32_t spawnPoint, WorldClock::time_point time);

    /**
     * The spawn point rule. The world calls it in each tick: it does its work one time for
     * each `kScanInterval` of the world clock.
     */
    void tick();

    /** The world removed an entity: the spawn point of a creature has no creature now. */
    void entity_removed(const Entity& entity);

    /** @return The creature with this id, or null (also for the entity of a character). */
    [[nodiscard]] Entity* find(EntityId id);

    [[nodiscard]] const std::vector<SpawnPointState>& spawn_points() const {
        return m_points;
    }
    [[nodiscard]] const CreatureCounts& counts() const {
        return m_counts;
    }
    /** @return The number of creatures that are in the world now. */
    [[nodiscard]] std::size_t count() const {
        return m_creatures.size();
    }
    /** @return The ids of the creatures that are in the world now. */
    [[nodiscard]] const std::set<EntityId>& ids() const {
        return m_creatures;
    }

    /** The interval of the spawn point rule on the world clock. */
    static constexpr std::chrono::milliseconds kScanInterval{500};
    /**
     * The time of the reaction of a creature to a hit (a rule of this project: the game files
     * have the length of each animation, and the tool does not read it). After it the
     * creature has no action of the server: its client plays the idle action again itself.
     */
    static constexpr std::chrono::milliseconds kReactionTime{1500};

private:
    Entity* make(const gamedata::CreatureKind& kind,
                 std::uint32_t level,
                 const state::WorldPosition& position,
                 float yaw,
                 std::optional<std::uint32_t> spawnPoint,
                 std::uint32_t selector);
    void scan();
    /** Makes the table of the spawn points for the creature data, when it does not have its size. */
    void load_points();
    /**
     * Starts an action of the action table of the slice of a creature, and marks its movement
     * state. No effect for an index that the table does not have, or for an action with no fragment.
     * @param length The time of an action that has an end. No value: the action stays.
     */
    void start_action(Entity& entity, std::uint32_t action, std::optional<std::chrono::milliseconds> length = std::nullopt);
    /** Removes the action of each creature whose action has an end that came. */
    void end_actions();

    World& m_world;
    const gamedata::CreatureData* m_data;
    /** One state for each spawn point of the creature data, with the same index. */
    std::vector<SpawnPointState> m_points;
    /** The spawn points by their place: the id of an entry is the index of the spawn point. */
    PositionIndex m_pointIndex;
    std::set<EntityId> m_creatures;
    /** The creatures whose action has an end. */
    std::set<EntityId> m_acting;
    CreatureCounts m_counts;
    std::optional<WorldClock::time_point> m_lastScan;
    /** Number of creatures that commands made. It selects the slice of an alias with more than one. */
    std::uint32_t m_commandSpawns{0};
};

} // namespace soulwarden::backend::world

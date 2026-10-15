#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gamedata/creature_data.h"
#include "world/behavior.h"
#include "world/entity.h"
#include "world/ids.h"
#include "world/load_meter.h"
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
// point. A spawn point has one creature at most. The two distances are distances of the spawn
// point, not of the creature: a creature that goes to a player (world/behavior.h) stays in the
// world while a player is near its point.
//
// The cost of the rule depends on the players and on what is near them, not on the size of the
// world (the creature data of the whole world has 33,000 spawn points and more). One scan
// looks only at the spawn points in the grid cells that the circle of a player touches
// (SpawnGrid), and at the creatures that are in the world, which are the creatures of the
// points near a player. It never walks the list of all spawn points. The counts of a scan are in
// CreatureCounts, and a test case fails for a scan that looks at each point.
//
// A creature of a spawn point that died, or that a command removed, does not come back by the
// distance rule: the spawn point waits. A point whose creature died gets its respawn time from
// the data of its spawner (world/rewards.h, respawn_time): a time of the world clock
// (`respawnAt`) at which it is free again. A point whose creature a command removed waits for the
// command `reset` (reset_spawn_points), which makes each point that waits free at once. The body
// of the dead creature can be in the world at the respawn time (a player stayed near it): the
// point removes it before it makes its new creature. The respawn time costs nothing in a scan: a
// scan looks only at the points near a player.
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
    /** The action table of the slice of the creature has no action with this name that has a fragment. */
    unknownAction,
    /** The creature is on its way back (the reset): it takes no damage. */
    immune,
};

/** The locomotion of a change of the place of a creature (Creatures::move). */
enum class Gait : std::uint8_t {
    /** A place with no walk: the arrival at the end of the way back. */
    place,
    /** A step of a way in a fight or of the way back: the action Move_Combat of the slice (CreatureSlice::moveAction). */
    run,
    /**
     * A step of a walk with no target (a patrol, a wander): the action Move of the slice
     * (CreatureSlice::walkAction), or Move_Combat for a slice with no such action.
     */
    walk,
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
     * The time at which a point that waits is free again: the death of its creature plus the
     * respawn time of its spawner. No value: only a command makes it free (a removal by a command).
     */
    std::optional<WorldClock::time_point> respawnAt;
    /** Number of creatures that the point made. It selects the slice of an alias with more than one. */
    std::uint32_t spawns{0};
};

/**
 * The spawn points by their place: a grid of square cells in the horizontal plane, with the
 * indexes of the spawn points of each cell. The spawn points do not change after the load. A
 * lookup costs the cells that a circle touches, for each size of the world.
 */
class SpawnGrid {
public:
    /** Makes the grid for the spawn points of the creature data. The id of a point is its index. */
    void build(const std::vector<gamedata::SpawnPoint>& points);

    /**
     * Calls `visit(index)` for each spawn point in a cell that the circle touches: the columns
     * from west to east, the cells of a column from south to north, and the points of a cell
     * from the lowest index. A point of the result can be farther than `range`: the caller
     * compares the exact distance.
     */
    template <class Visit>
    void for_each_candidate(const state::WorldPosition& center, float range, Visit&& visit) const {
        const std::int64_t lowX = cell(center.x - range);
        const std::int64_t highX = cell(center.x + range);
        const std::int64_t lowY = cell(center.y - range);
        const std::int64_t highY = cell(center.y + range);
        if ((highX - lowX + 1) * (highY - lowY + 1) > static_cast<std::int64_t>(m_sorted.size())) {
            // A very large circle: each cell of the grid one time, in the same sequence.
            for (const auto& [key, points] : m_sorted) {
                if (key.first >= lowX && key.first <= highX && key.second >= lowY && key.second <= highY) {
                    for (const std::uint32_t point : *points) {
                        visit(point);
                    }
                }
            }
            return;
        }
        for (std::int64_t x = lowX; x <= highX; ++x) {
            for (std::int64_t y = lowY; y <= highY; ++y) {
                const auto found = m_cells.find(key_of(x, y));
                if (found != m_cells.end()) {
                    for (const std::uint32_t point : found->second) {
                        visit(point);
                    }
                }
            }
        }
    }

    /** @return The number of cells that have a spawn point. */
    [[nodiscard]] std::size_t cell_count() const {
        return m_cells.size();
    }

private:
    /** Edge of one cell, in meters. */
    static constexpr float kCellSize = 64.0F;

    [[nodiscard]] static std::int64_t cell(float value) {
        return static_cast<std::int64_t>(std::floor(value / kCellSize));
    }
    [[nodiscard]] static std::uint64_t key_of(std::int64_t x, std::int64_t y) {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) | static_cast<std::uint32_t>(y);
    }

    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> m_cells;
    /** The cells in the sequence of a lookup, for a circle that is larger than the grid. */
    std::vector<std::pair<std::pair<std::int64_t, std::int64_t>, const std::vector<std::uint32_t>*>> m_sorted;
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
    /** Number of scans of the spawn point rule. */
    std::uint64_t scans{0};
    /**
     * Number of spawn points and of creatures that the scans looked at, and the same two
     * numbers for the last scan: the work of a scan, which depends on the players and on what
     * is near them.
     */
    std::uint64_t pointsLooked{0};
    std::uint64_t creaturesLooked{0};
    std::size_t lastScanPoints{0};
    std::size_t lastScanCreatures{0};
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
     * dead mark, and its spawn point waits. The entity stays in the world (the body). A creature
     * on its way back is immune: a lower health gives the error `immune` and changes nothing
     * (the same rule as a hit, for the health command and the kill command).
     */
    CreatureResult set_health(EntityId id, float health);

    /** Sets the health of a creature to 0. */
    CreatureResult kill(EntityId id) {
        return set_health(id, 0.0F);
    }

    /**
     * A creature takes damage (the combat system, world/combat.h): its health goes down by the
     * damage, through set_health. A creature that lives after it plays the reaction action of
     * its slice, when it plays no attack. A damage that is not less than its health is its
     * death. A creature that lives gets the character that did the damage as its target, when
     * it has none (world/behavior.h). A creature on its way back takes no damage (the error
     * `immune`, Creature::immune).
     * @param damage The damage: a finite number that is not negative.
     * @param attacker The entity of the character that did the damage. 0: a command.
     */
    CreatureResult hurt(EntityId id, float damage, EntityId attacker = 0);

    /**
     * Gives a creature a status effect of an ability (stage 3.3a, world/abilities.h) until its
     * end on the world clock. A stun stops the movement and the attack of the creature (its
     * attack that plays stops, and it plays the action "Stun" of its slice), a root stops its
     * movement, a slow lowers its speed, and a weaken lowers its damage (world/behavior.h,
     * Combat::creature_damage). The same status again gives the effect the later end. Each
     * observer gets the icon of an effect that shows at the name plate (the status state). A
     * dead creature and a creature on its way back take no effect.
     * @return False: the creature took no effect.
     */
    bool apply_effect(Entity& entity, const CreatureEffect& effect);
    /**
     * A knockdown or a stagger of an ability: the creature plays the reaction of the power (its
     * fragment of the slice; the fragment of the action "Reaction" for a slice with no such
     * fragment), and it does not move and does not attack for the time. Its attack that plays
     * stops. A dead creature and a creature on its way back take none.
     * @return False: the creature took none.
     */
    bool knock(Entity& entity, unsigned power, std::chrono::milliseconds time);

    /**
     * Starts an action of the action table of the slice of a creature, by its name (a
     * development command, and the place where a behavior of a later stage starts an action).
     * Each session that observes the creature gets the action with its movement state.
     * @param seconds The time of an action that has an end: after it the creature has no action
     *        of the server. No value: the action stays until a different one starts.
     * @param tags Development aid: the tag state of the fragments for this action, in the
     *        place of the tags of the slice (a live check of the tags of a slice).
     */
    CreatureResult play(EntityId id, const std::string& action, std::optional<float> seconds = std::nullopt,
                        std::optional<gamedata::FragmentTags> tags = std::nullopt);

    /**
     * Makes each spawn point that waits free again (a command): the distance rule makes its
     * creature again when a player is near. A dead creature of a point leaves the world.
     * @return The number of spawn points that waited.
     */
    std::size_t reset_spawn_points();

    /**
     * Sets the time of the world clock at which a spawn point that waits is free again (the death
     * of a creature sets the respawn time of its spawner; a test or a command can set a different
     * one). No effect for a point that does not wait. From that time on, the point makes its
     * creature again when a player is near it (the distance rule): the body of the dead creature
     * then leaves the world first.
     * @return False when the creature data has no spawn point with this index.
     */
    bool respawn_at(std::uint32_t spawnPoint, WorldClock::time_point time);

    /**
     * The spawn point rule. The world calls it in each tick: it does its work one time for
     * each `kScanInterval` of the world clock.
     */
    void tick();

    /**
     * The next tick does the spawn point rule, also before its interval: a player came to a
     * different place of the world (World::travel).
     */
    void scan_soon() {
        m_lastScan.reset();
    }

    /** The world removed an entity: the spawn point of a creature has no creature now. */
    void entity_removed(const Entity& entity);

    /** @return The creature with this id, or null (also for the entity of a character). */
    [[nodiscard]] Entity* find(EntityId id);

    [[nodiscard]] const std::vector<SpawnPointState>& spawn_points() const {
        return m_points;
    }
    /** @return The number of spawn points that wait. */
    [[nodiscard]] std::size_t waiting_count() const {
        return m_waiting.size();
    }
    /** @return The behavior of the creatures: the targets, the attacks, and the counts of its steps. */
    [[nodiscard]] Behavior& behavior() {
        return m_behavior;
    }
    [[nodiscard]] const Behavior& behavior() const {
        return m_behavior;
    }

    // For the behavior of the system (world/behavior.h): it is the one caller of these functions.

    /**
     * Starts an action of the action table of the slice of a creature, and marks its movement
     * state. No effect for an index that the table does not have, or for an action with no fragment.
     * @param length The time of an action that has an end. No value: the action stays.
     * @return True when the action started.
     */
    bool start_action(Entity& entity, std::uint32_t action, std::optional<std::chrono::milliseconds> length = std::nullopt);
    /**
     * Puts a creature at a different place: the position index of the world follows it, and
     * each session that observes the creature gets its movement state.
     * @param gait For a step of a way (`run`, `walk`): the creature plays the locomotion action
     *             of its gait (Creature::moving) from its first step, in one record for the way
     *             (a new record only when the gait changes), and the step replaces an action of
     *             the server that plays (a reaction), also for a slice with no locomotion action.
     *             `place`: a place with no walk (the arrival at the end of the way back).
     */
    void move(Entity& entity, const state::WorldPosition& position, Gait gait = Gait::run);
    /**
     * The immune mark of a creature changed (Creature::immune: its way back started or ended):
     * each session that observes it gets the vitals state with the fields of the mark.
     */
    void immune_changed(Entity& entity);
    /** Ends the walk of a creature (Creature::moving): its client plays the action that it selects (the idle action). */
    void stop(Entity& entity);
    /** Marks the movement state of a creature that changed its direction at its place. */
    void moved(Entity& entity);
    /** Gives a creature that lives its full health: a creature that is at its home again. */
    void restore(Entity& entity);

    /** @return The real time of the scans of the spawn point rule (a measurement). */
    [[nodiscard]] const LoadMeter& scan_load() const {
        return m_scanLoad;
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
    /** Sets or clears the mark "the point waits" of a spawn point. */
    void set_waiting(std::uint32_t point, bool waiting);
    /** Removes the action of each creature whose action has an end that came. */
    void end_actions();
    /** Removes each effect whose end came, from the creatures that have effects (m_affected). */
    void end_effects();
    /** The stun of a creature ended: its action "Stun" stops, and each observer gets its idle action. */
    void end_stun(Entity& entity);

    World& m_world;
    const gamedata::CreatureData* m_data;
    /** One state for each spawn point of the creature data, with the same index. */
    std::vector<SpawnPointState> m_points;
    /** The spawn points by their place. */
    SpawnGrid m_grid;
    /** The spawn points that wait: the reset command walks only these. */
    std::set<std::uint32_t> m_waiting;
    /**
     * The creatures whose spawn point a player is near in the scan that runs: a list of the
     * scan, with no value between two scans.
     */
    std::unordered_set<EntityId> m_near;
    LoadMeter m_scanLoad;
    std::set<EntityId> m_creatures;
    /** The creatures whose action has an end. */
    std::set<EntityId> m_acting;
    /** The creatures with an effect or a knockdown of an ability: the end of effects looks only at these. */
    std::set<EntityId> m_affected;
    CreatureCounts m_counts;
    std::optional<WorldClock::time_point> m_lastScan;
    /** Number of creatures that commands made. It selects the slice of an alias with more than one. */
    std::uint32_t m_commandSpawns{0};
    /** The behavior of the creatures: a part of this system. */
    Behavior m_behavior;
};

} // namespace soulwarden::backend::world

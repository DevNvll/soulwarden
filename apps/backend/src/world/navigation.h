#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gamedata/height_maps.h"
#include "gamedata/navigation_data.h"
#include "state/store.h"
#include "world/ids.h"
#include "world/load_meter.h"
#include "world/world_clock.h"

// The navigation of a creature: where the ground is, where the next step to a goal is, and
// what a creature sees (protocol notes 4.19).
//
// The behavior of a creature (world/behavior.h) gets each place from two functions of this
// class: `ground_height` and `next_step`. Do not compute a place of a creature in a different
// function.
//
// The data. The navigation data is game data (gamedata/navigation_data.h): a grid of the whole
// world with cells of 0.25 m, each cell with its floors, in tiles of 64 m. The world keeps
// only the tiles near the characters and near the creatures that move. A thread of its own
// reads a tile (the world thread does not wait for a file), and the world thread takes the
// tiles that are ready at the start of a tick. A creature whose tile is not there yet waits.
//
// The path. A creature goes on a path of cells of the data. The search is A* on the cells,
// with these limits: the steps of all searches of one tick have a limit, a search that needs
// more continues in the next ticks, and the searches that wait come in their sequence. One
// search has a limit too. Before a search, the areas of the tiles say if a way is there: a
// goal with no way costs no search. A creature keeps its path, and it searches again when its
// goal moved.
//
// With no navigation data the two functions use the terrain heights (gamedata/height_maps.h)
// and the straight line, as in stage 3.4b, and no wall stops the sight.
//
// World thread only, but the thread that reads the tiles: it has no access to the world.

namespace soulwarden::backend::world {

class World;

/** The values of the navigation that the game data does not give (rules of this project). */
namespace navigation_rules {
/** The world keeps the tiles that are this near to a character, and to a creature that moves (meters). */
inline constexpr float kKeepDistance = 96.0F;
inline constexpr float kMoverKeepDistance = 32.0F;
/** The world looks for the tiles that it needs one time in this interval of the world clock. */
inline constexpr std::chrono::milliseconds kWantInterval{500};
/** A tile that no character and no creature needs stays this long. */
inline constexpr std::chrono::milliseconds kDropAfter{10000};
/** The steps of all path searches of one tick of the world, and of one search. */
inline constexpr std::uint32_t kTickSteps = 4000;
inline constexpr std::uint32_t kSearchSteps = 60000;
/** A creature searches again when its goal is this far from the goal of its path (meters). */
inline constexpr float kRepathDistance = 1.0F;
/** A place is on a floor when a free floor is this near: in the horizontal plane, and in the height (meters). */
inline constexpr float kSnapDistance = 1.5F;
inline constexpr float kSnapHeight = 1.5F;
/** A creature stands on a floor when a free floor is this near: in the horizontal plane, and in the height (meters). */
inline constexpr float kFootingReach = 0.5F;
inline constexpr float kFootingHeight = 0.6F;
/** The sight goes from this height above the ground of a creature to this height above the ground of its target (meters). */
inline constexpr float kEyeHeight = 1.5F;
/** A path in shallow water is this much longer for the search than the same path on land. */
inline constexpr float kWaterCost = 2.0F;
/** A goal with no way stays a goal with no way for this time, when it does not move. */
inline constexpr std::chrono::milliseconds kNoWayMemory{3000};
/** The search of a creature that follows a path starts at the place that the creature has this number of cells later. */
inline constexpr std::size_t kSpliceCells = 12;
/** A creature that is this far from its path has no path (a command moved it). */
inline constexpr float kOffPath = 1.0F;
/**
 * A creature that is more than this from the place of its last step (and not more than kOffPath) was moved by a force (a push): its
 * path stays only with a free line from the new place (code review of user test 2, finding U2-R1; a decision of an agent: 1 cm, above
 * the error of a float position).
 */
inline constexpr float kMovedOffPath = 0.01F;
/**
 * A creature on a path to the place nearest to a goal with no way (`next_step` with `nearest`)
 * searches again when the goal is this far from the goal of that path (meters): a search for
 * such a goal can use the steps of many ticks.
 */
inline constexpr float kNearestRepathDistance = 3.0F;
} // namespace navigation_rules

/** What a step of a creature gave. */
enum class StepKind {
    /** The creature has a new place. */
    moved,
    /** The creature is at the end of its path: no new place. */
    arrived,
    /** No new place in this step: the tile of the creature is not there yet, or its path search waits. */
    waiting,
    /** The goal has no way from the place of the creature. */
    noWay,
    /** The creature is on no floor of the data: it cannot walk. */
    noGround,
};

struct NavStep {
    StepKind kind{StepKind::noGround};
    state::WorldPosition position;
    /**
     * True for a step on a path to the place nearest to a goal that has no way, and for the
     * arrival at that place (`next_step` with `nearest`): the creature does not get to the goal.
     */
    bool partial{false};
};

/** What a forced movement of a creature gave (Navigation::slide, stage 3.3b-S4). */
struct Slide {
    /** The place of the creature after the movement: the goal, or the last place of the line before a wall or a drop. */
    state::WorldPosition position;
    /** True: a wall, a drop, or a place with no floor stopped the movement before its goal. */
    bool blocked{false};
    /** True: the navigation data of the line is not there yet; the creature does not move in this step. */
    bool waiting{false};
};

/** If a creature at a place can walk. */
enum class Footing {
    /** The data of the place is not there yet. */
    unknown,
    walks,
    /** No floor for a creature at the place. */
    rooted,
};

/** Counts of the navigation, for the log, for the development tools, and for the tests. */
struct NavigationCounts {
    /** Tiles that the world has now, and their bytes. */
    std::size_t tilesHeld{0};
    std::size_t tileBytes{0};
    std::size_t largestTilesHeld{0};
    /** Tiles that came from the files, that the world removed, and that have no data (no file, or a tile that is not correct). */
    std::uint64_t tilesLoaded{0};
    std::uint64_t tilesDropped{0};
    std::uint64_t tilesMissing{0};
    /** Paths with no search (a straight line that is free), and path searches. */
    std::uint64_t pathsDirect{0};
    std::uint64_t searches{0};
    std::uint64_t searchSteps{0};
    /** Searches that found no path in their limit, and goals with no way by the areas (no search). */
    std::uint64_t searchesFailed{0};
    std::uint64_t noWay{0};
    /** Paths to the place nearest to a goal with no way (a fight, `next_step` with `nearest`). */
    std::uint64_t partialPaths{0};
    /** The largest number of steps of the searches in one tick, and the largest number of searches that waited. */
    std::uint32_t largestTickSteps{0};
    std::size_t largestQueue{0};
    /** Steps of creatures that waited for a tile or for a search. */
    std::uint64_t waits{0};
    /** Paths that a push ended: the line from the new place to the next cell of the path was not free (finding U2-R1). */
    std::uint64_t pushedOffPath{0};
    /** Sight tests, and the ones that a solid stopped. */
    std::uint64_t sightTests{0};
    std::uint64_t sightStopped{0};
    /** Line tests of a projectile (stage 3.3b-S3), the cells that they looked at, and the lines that a solid stopped. */
    std::uint64_t lineTests{0};
    std::uint64_t lineCells{0};
    std::uint64_t lineStopped{0};
};

/** One corner of a path, for a development command. */
struct PathReport {
    bool found{false};
    /** Why no path was found: an empty text for a path. */
    std::string cause;
    std::vector<state::WorldPosition> corners;
    float length{0.0F};
    std::size_t cells{0};
    std::uint32_t steps{0};
    bool direct{false};
    std::chrono::nanoseconds time{0};
};

class Navigation {
public:
    /**
     * @param data The navigation files (game data). Null, or a folder with no file: the navigation uses the terrain.
     * @param terrain The terrain heights (game data): the ground when the navigation data has no tile for a place. Null: none.
     * @param loaderThread True: a thread of its own reads the tiles. False: the world thread reads a tile when it needs it (a test).
     */
    /**
     * @param level Stage DG2: the own level of this navigation (the data of its level set, world/levels.h): it serves the spaces of that
     *        level. Empty: the navigation of the main world level (the main world and the story instances of its level).
     */
    Navigation(World& world, const gamedata::NavigationData* data, gamedata::Terrain* terrain, bool loaderThread, std::string level = {});
    ~Navigation();
    Navigation(const Navigation&) = delete;
    Navigation& operator=(const Navigation&) = delete;

    /** @return True when the navigation has a source for the ground: the navigation data, or the terrain heights. */
    [[nodiscard]] bool available() const {
        return has_data() || m_terrain != nullptr;
    }
    /** @return True when the world has navigation data: paths, floors, and walls. */
    [[nodiscard]] bool has_data() const {
        return m_data != nullptr && m_data->available();
    }

    /**
     * @return The height of the ground at a position: the surface on which a creature at this
     *         position stands. One place can have more than one floor: the result is the floor
     *         that is nearest to the height of the position. With no tile of the navigation
     *         data for the place, the result is the terrain height. No value: no ground.
     */
    [[nodiscard]] std::optional<float> ground_height(const state::WorldPosition& position) const;

    /**
     * One step of a creature to a goal: a place on a floor that is `distance` meters nearer to
     * the goal on the path of the creature. The navigation keeps the path of each creature.
     * @param mover The entity of the creature: the key of its path.
     * @param distance The length of the step, in meters (the speed times the time).
     * @param nearest True: a goal with no way gives a path to the place nearest to the goal that
     *        the creature can get to (the search goes to the cells nearest to the goal first, in
     *        the limits of a search), and the steps on it have `NavStep::partial` (a fight with a
     *        target that has no way to it). False: a goal with no way gives `noWay`.
     */
    [[nodiscard]] NavStep next_step(EntityId mover,
                                    const state::WorldPosition& from,
                                    const state::WorldPosition& goal,
                                    float distance,
                                    bool nearest = false);

    /** A creature needs its path no more (it is at its goal, it has a different goal, or it left the world). */
    void forget(EntityId mover);

    /**
     * A creature did not take the place of its last step (`next_step` gave `moved`, and a
     * different creature stands there): its path is at the place before that step again.
     */
    void take_back(EntityId mover);

    /**
     * A forced movement of a creature (stage 3.3b-S4: the pull or the push of a hit, world/forced_motion.h): the creature
     * goes on the straight line from `from` to `to` on the free floors of the data, cell by cell, with the rule of a step
     * of a path (the next floor is not more than one step higher or lower, and no corner of a wall is cut). A wall, a
     * drop, or a cell with no free floor stops it at the last place of the line that it stands on. The creature keeps
     * no path: this is not a step of a way. With no navigation data for the place: the line on the terrain heights, and
     * nothing stops it (the rule of stage 3.4b). The world asks for the tiles of a line that it does not have
     * (`Slide::waiting`).
     */
    [[nodiscard]] Slide slide(const state::WorldPosition& from, const state::WorldPosition& to);
    /** @return If a creature at a place can walk: it stands on a free floor of the data (or, with no data, on the terrain). */
    [[nodiscard]] Footing footing(const state::WorldPosition& position) const;

    /**
     * @return True when no solid is between the eyes of a creature at `eye` and a character at
     *         `target` (the two positions are the places of their feet). An object that is
     *         lower than the line does not stop the sight. With no tile of the navigation data
     *         on the line, nothing stops the sight.
     * @param space The space of the two positions (stage QG9a, finding DG1R-R6 of the review of stage DG1): the data is the data of the
     *        level of the main world; in a space of a different level nothing stops the sight.
     */
    [[nodiscard]] bool sees(const state::WorldPosition& eye, const state::WorldPosition& target, SpaceId space = kMainSpace) const;
    /**
     * The line of a projectile (stage 3.3b-S3): the straight segment between two points in the air (the exact heights, no
     * eye height), cell by cell, with the same free intervals of the floors as `sees`. A part of the line below the
     * lowest floor of a cell is in the ground.
     * @return The part of the segment (0 to 1) at which it enters the first cell where a solid or the ground stops it. No
     *         value: nothing stops it (also with no tile of the navigation data on the line, with no data, and in a space of a
     *         different level: the rule of `sees`).
     */
    [[nodiscard]] std::optional<float> line_stop(const state::WorldPosition& from,
                                                 const state::WorldPosition& to,
                                                 SpaceId space = kMainSpace) const;
    /**
     * Stage 1.8f (code review of stage 1.8e, finding QR8E-R2): @return True when the world has each tile of the navigation data that a
     * segment needs for `line_stop`. A tile that it does not have, or that is on its way, gets a request, and the result is false (the
     * caller tries again later). No navigation data, or a space of a different level: true.
     */
    [[nodiscard]] bool line_ready(const state::WorldPosition& from, const state::WorldPosition& to, SpaceId space = kMainSpace);

    /** The world calls it in each tick: the tiles that are ready, the tiles that the world needs, and the path searches. */
    void tick();

    /**
     * Development aid: the path between two places, searched now with no limit of a tick.
     * The world thread reads the tiles that the path needs itself.
     */
    [[nodiscard]] PathReport find_path(const state::WorldPosition& from, const state::WorldPosition& to);

    [[nodiscard]] const NavigationCounts& counts() const {
        return m_counts;
    }
    /** @return The real time of the path searches of a tick, and of the reads of the tiles (measurements). */
    [[nodiscard]] const LoadMeter& search_load() const {
        return m_searchLoad;
    }
    [[nodiscard]] const LoadMeter& tile_load() const {
        return m_tileLoad;
    }
    /** @return The number of path searches that wait now. */
    [[nodiscard]] std::size_t queued() const {
        return m_queue.size();
    }

private:
    /** A cell of the world with one of its floors. */
    struct Place {
        std::int32_t x{0};
        std::int32_t y{0};
        std::uint8_t floor{0};
        float height{0.0F};
        std::uint16_t area{0};
        /** The floor is below shallow water. */
        bool water{false};
        bool operator==(const Place& other) const {
            return x == other.x && y == other.y && floor == other.floor;
        }
    };
    struct PathCell {
        std::int32_t x{0};
        std::int32_t y{0};
        float height{0.0F};
    };
    enum class TileState { loading, ready, missing };
    /** Two areas of two tiles that have a step between them over the edge of the tiles. */
    using AreaLink = std::pair<std::uint64_t, std::uint64_t>;
    struct Held {
        TileState state{TileState::loading};
        std::shared_ptr<const gamedata::NavTile> tile;
        WorldClock::time_point wanted{};
        /** The links over the edge to the tile to the east and to the tile to the north: made one time, when the second tile of the edge comes. */
        std::vector<AreaLink> east;
        std::vector<AreaLink> north;
    };
    /** The path of one creature. */
    struct Route {
        /** The cells of the path, and the cell that the creature goes to now. */
        std::vector<PathCell> cells;
        std::size_t reached{0};
        std::size_t corner{0};
        /** The goal of the path, and the place of the creature after its last step. */
        state::WorldPosition goal;
        state::WorldPosition position;
        bool hasPosition{false};
        /** The values before the last step (`take_back`). */
        std::size_t stepReached{0};
        std::size_t stepCorner{0};
        state::WorldPosition stepPosition;
        bool hasStep{false};
        /**
         * A search for this creature waits or runs: its goal. `splice`: the search started at the
         * cell `junction` of `cells`, and the creature goes on to that cell while the search runs.
         */
        bool searching{false};
        bool splice{false};
        state::WorldPosition searchGoal;
        std::size_t junction{0};
        /** True: the path goes to the place nearest to a goal with no way (NavStep::partial), for a mover that asked for it (`nearest`). */
        bool nearest{false};
        bool partial{false};
        /** The goal that had no way, and until when the navigation does not look again. */
        bool noWay{false};
        state::WorldPosition noWayGoal;
        WorldClock::time_point noWayUntil{};
        WorldClock::time_point used{};
    };
    /** The search that runs: A* on the cells. */
    struct Search;
    struct Loaded {
        std::uint64_t key{0};
        std::shared_ptr<const gamedata::NavTile> tile;
        std::chrono::nanoseconds time{0};
    };

    [[nodiscard]] static std::uint64_t tile_key(std::int32_t tileX, std::int32_t tileY) {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(tileX)) << 32) | static_cast<std::uint32_t>(tileY);
    }
    /** @return The tile of a cell of the world, or null when the world does not have it now. */
    [[nodiscard]] const gamedata::NavTile* tile_of(std::int32_t cellX, std::int32_t cellY) const;
    /** @return The number of floors of a cell, or -1 when the world does not have its tile. */
    [[nodiscard]] int spots(std::int32_t cellX, std::int32_t cellY, gamedata::NavSpot* out) const;
    /**
     * @return True when the heights `low` to `high` of a line of sight in the cell of one of its
     *         ends are in the free interval of the floor of the body there (the highest floor
     *         below the eye `eyeZ`). No tile, no floor, or a body in a solid: true.
     */
    [[nodiscard]] bool end_cell_free(std::int32_t cellX, std::int32_t cellY, float eyeZ, float low, float high) const;
    /** @return True when the heights `low` to `high` of a line of sight in a cell between its two ends are in the free interval of the floor below them. No tile: true. */
    [[nodiscard]] bool line_free(std::int32_t cellX, std::int32_t cellY, float low, float high) const;
    /**
     * @return True when the world has each tile with navigation data in the rectangle of two
     *         places, with `margin` meters around it. A tile that it does not have, or that is
     *         on its way, gets a request (want), and the result is false: a creature waits.
     */
    bool tiles_ready(const state::WorldPosition& from, const state::WorldPosition& to, float margin);
    /** @return True when the cell has a floor that a creature at `height` steps to: `out` gets it. */
    [[nodiscard]] bool step_to(std::int32_t cellX, std::int32_t cellY, float height, Place& out) const;
    /** @return True when a creature can go on the straight line from one place to a different one. */
    [[nodiscard]] bool walk_line(const Place& from, const Place& to) const;
    /**
     * @return True when a creature can go on the real segment from a place (its floor `start`) to the floor `end` of a cell: the samples
     *         of half a cell of `slide` (each new cell a step from the cell before it, no cut of the corner of a wall), and the last floor
     *         is the floor of `end` (code review of stage B0, finding B0-R1: the line between the centers of the cells can be free while
     *         the real line from a pushed creature goes through a wall).
     */
    [[nodiscard]] bool walk_segment(const Place& start, const state::WorldPosition& from, const state::WorldPosition& to, const Place& end) const;
    /** @return The free floor that is nearest to a position: not farther than `reach` in the plane and `heightReach` in the height. */
    [[nodiscard]] std::optional<Place> snap(const state::WorldPosition& position, float reach, float heightReach) const;
    [[nodiscard]] state::WorldPosition center_of(const PathCell& cell) const;
    /** @return The height of the floor at an exact place: the floor of the cell that is nearest to `height`. */
    [[nodiscard]] std::optional<float> floor_at(float x, float y, float height, bool walkableOnly) const;

    void want(std::int32_t tileX, std::int32_t tileY, WorldClock::time_point now);
    void want_around(const state::WorldPosition& position, float distance, WorldClock::time_point now);
    void update_wanted();
    /**
     * @return True when the navigation data applies to a space: the main world, or a space of the level of the main world (stage QG9a); for
     *         the navigation of an own level (stage DG2), a space of that level.
     */
    [[nodiscard]] bool level_data(SpaceId space) const;
    void insert(std::uint64_t key, std::shared_ptr<const gamedata::NavTile> tile, std::chrono::nanoseconds time);
    void take_loaded();
    void loader_main();
    /** Makes the links of the edge between a tile and the tile to its east (`side` 0) or to its north (`side` 1). */
    void link_edge(Held& low, const Held& high, int side) const;
    /** Makes the links of the four edges of a tile that came, or removes those of a tile that goes. */
    void link_tile(std::int32_t tileX, std::int32_t tileY, bool there);
    /** The areas of the tiles that have a way between them over a tile edge: from the links of each edge. */
    void link_areas() const;
    [[nodiscard]] std::uint64_t area_of(const Place& place) const;
    [[nodiscard]] bool connected(const Place& from, const Place& to) const;

    void run_searches();
    /** Makes the path of a search the path of its creature. `partial`: the path goes to the place nearest to the goal. */
    void finish_search(EntityId mover, bool found, std::vector<PathCell> path, bool partial = false);
    /** Moves a creature along its path. */
    [[nodiscard]] NavStep follow(Route& route, const state::WorldPosition& from, float distance);
    /** The step with no navigation data: the straight line on the terrain. */
    [[nodiscard]] NavStep terrain_step(const state::WorldPosition& from, const state::WorldPosition& goal, float distance) const;

    World& m_world;
    /** Stage DG2: the own level of this navigation; empty: the main world level. */
    std::string m_level;
    const gamedata::NavigationData* m_data;
    gamedata::Terrain* m_terrain;
    const bool m_threaded;
    gamedata::NavAgent m_agent;

    std::unordered_map<std::uint64_t, Held> m_tiles;
    /** The tile of the last query. */
    mutable std::uint64_t m_lastKey{~0ULL};
    mutable const gamedata::NavTile* m_lastTile{nullptr};
    /** The areas of the tiles: an area of a tile to the area that it has a way to. Made again after a change of the tiles. */
    mutable std::unordered_map<std::uint64_t, std::uint64_t> m_areaParent;
    mutable bool m_areasCurrent{false};
    std::optional<WorldClock::time_point> m_lastWant;

    std::unordered_map<EntityId, Route> m_routes;
    /** The creatures whose search waits, in their sequence. */
    std::deque<EntityId> m_queue;
    std::unique_ptr<Search> m_search;
    EntityId m_searchMover{0};

    // The thread that reads the tiles. The mutex protects the two lists and the stop mark.
    std::thread m_loader;
    std::mutex m_loaderMutex;
    std::condition_variable m_loaderWake;
    std::deque<std::uint64_t> m_requests;
    std::vector<Loaded> m_loaded;
    bool m_loaderStop{false};

    mutable NavigationCounts m_counts;
    LoadMeter m_searchLoad;
    LoadMeter m_tileLoad;
};

} // namespace soulwarden::backend::world

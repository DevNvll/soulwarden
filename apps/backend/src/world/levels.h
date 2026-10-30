#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "gamedata/level_data.h"
#include "world/ids.h"
#include "world/navigation.h"
#include "world/world_clock.h"

// The level data of the own levels in the world (stage DG2, docs/systems/instances.md, "The level data"). An instance whose map has a level
// that is not the level of the main world (Amrine, a solo story level) has the content of the data set of its level (gamedata/level_data.h):
// the creatures of its spawn points (world/creatures.h, one spawn set for each space), its terrain heights and its navigation data (one
// Navigation object for each level, World::navigation(space)), and its interaction objects.
//
// The world reads the set of a level when the first space of that level starts, in the world thread (the load case LV5 has its time limit),
// one time for each backend process: the data stays until the stop, and each later instance of the level uses it. A level with no set, or a
// set that the reader refuses, gives an instance with no content (stage DG1). The main world level is not a level of this system: its data
// is the game data of the backend start.
//
// World thread only.

namespace soulwarden::backend::world {

class World;

/** One own level that the world read. */
struct Level {
    std::string name;
    /** The data set; null: the source has no set (no folder of the level). */
    std::unique_ptr<gamedata::LevelData> data;
    /** The navigation of the level: its tiles and the paths of its creatures. Null: the set has no navigation data and no terrain heights. */
    std::unique_ptr<Navigation> navigation;
    /** The spaces of the level that run now. */
    std::set<SpaceId> spaces;
    /** The real time of the read of the set (a measurement), and its time on the world clock. */
    std::chrono::nanoseconds loadTime{0};
    WorldClock::time_point loadedAt{};
};

struct LevelCounts {
    /** Levels whose set the world read (accepted or not), sets that the reader refused, levels with no set. */
    std::uint64_t reads{0};
    std::uint64_t refused{0};
    std::uint64_t noSet{0};
    /** Spaces of the own levels that started, and that ended. */
    std::uint64_t spacesStarted{0};
    std::uint64_t spacesEnded{0};
};

class Levels {
public:
    /**
     * @param source The level data sets (the folder levels/ of the game data folder). Null: no set; each own level has no content.
     * @param navigationThread True: the navigation of each level reads its tiles in a thread of its own (the backend); false: in the tick
     *        (a test host).
     */
    Levels(World& world, const gamedata::LevelSource* source, bool navigationThread);
    ~Levels();
    Levels(const Levels&) = delete;
    Levels& operator=(const Levels&) = delete;

    /**
     * The instance system made a space of an own level. The first space of a level reads its set (one time for each backend process);
     * then the creature system gets the spawn points of the set for the space.
     */
    void space_started(SpaceId space, const std::string& level);
    /** The instance system ended a space (after World::clear_space). The data of its level stays. */
    void space_ended(SpaceId space);
    /**
     * Stage DG3: @return True when the level has an accepted data set (the check of an entry at the entrance of an expedition). The first
     * call reads the set (one time for each backend process, as the first space of the level).
     */
    [[nodiscard]] bool available(const std::string& level);

    /** @return The level of a space of an own level that this system knows, or null (the main world level, a space that is not there). */
    [[nodiscard]] const Level* level_of_space(SpaceId space) const;
    /** @return The data set of the level of a space, or null (no set, a refused set, the main world level). */
    [[nodiscard]] const gamedata::LevelData* data_of(SpaceId space) const;
    /** @return The navigation of the level of a space, or null. */
    [[nodiscard]] Navigation* navigation_of(SpaceId space);
    [[nodiscard]] const Navigation* navigation_of(SpaceId space) const;

    /** The tick of the navigation of each level with a space. */
    void tick();

    [[nodiscard]] const std::map<std::string, Level>& levels() const {
        return m_levels;
    }
    [[nodiscard]] const LevelCounts& counts() const {
        return m_counts;
    }
    [[nodiscard]] const gamedata::LevelSource* source() const {
        return m_source;
    }

private:
    /** @return The level of the cache: the first call reads its set (a level with no set has no data). */
    Level& read(const std::string& level);

    World& m_world;
    const gamedata::LevelSource* m_source;
    const bool m_navigationThread;
    std::map<std::string, Level> m_levels;
    /** The level of each space of an own level that runs. */
    std::map<SpaceId, std::string> m_spaces;
    LevelCounts m_counts;
};

} // namespace soulwarden::backend::world

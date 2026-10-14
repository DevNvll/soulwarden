#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// The navigation data of the world: where a creature can stand, and what stops its sight
// (protocol notes 4.19). The game copy has no navigation mesh: the original one was only on
// the server. The tool tools/gamedata/navigation.sh makes this data from the collision shapes
// of the game copy of the user (the terrain, the solid objects, the water, and the navigation
// marks of the level designers), and the files stay outside the repository.
//
// One file has one region of the world (2048 m by 2048 m) as 32 by 32 tiles of 64 m. A tile is
// a grid with cells of 0.25 m. A cell has zero or more floors (a street, a porch above it, a
// bridge), each one with its height. The world has 49 regions and 3 billion cells: the backend
// reads only the tiles near the players (world/navigation.h), and this class only reads and
// checks one tile.
//
// Without the files the backend starts and works: a creature then walks on the straight line on
// the terrain height (the rule of stage 3.4b).

namespace soulwarden::backend::gamedata {

/** Folder of the navigation files in the game data directory of the backend. */
inline constexpr const char* kNavigationFolder = "navigation";

/** The body of the creature that the tool made the data for (project values, protocol notes 4.19). */
struct NavAgent {
    /** Edge of one cell, in meters. */
    float cell{0.25F};
    /** No solid is nearer than this to the center of a free floor, at the height of the body (meters). */
    float radius{0.35F};
    /** The free height above a free floor (meters). */
    float height{1.8F};
    /** The largest height difference of one step between two cells (meters). */
    float step{0.35F};
    /** The steepest surface that is a floor (degrees). */
    float slope{50.0F};
    /** Water above a floor that is deeper than this is deep water (meters). */
    float wade{1.0F};
};

/** One floor of a cell. */
struct NavFloor {
    /** The floor is the terrain surface: its height is the terrain height of the tile. */
    static constexpr std::uint8_t kTerrain = 1;
    /** Water above the floor that a creature walks in, and water that is too deep for a walk. */
    static constexpr std::uint8_t kShallowWater = 2;
    static constexpr std::uint8_t kDeepWater = 4;
    /** No place for a creature (too steep, too near to a solid, a mark of the level designers): the floor is there for the sight. */
    static constexpr std::uint8_t kBlocked = 8;
    static constexpr std::uint8_t kKnownFlags = kTerrain | kShallowWater | kDeepWater | kBlocked;
    /** The value of `sightLow` for a floor with no free sight above it, and of `sightHigh` for the sky. */
    static constexpr std::uint8_t kNoSight = 255;
    static constexpr std::uint8_t kSky = 255;

    /** The height in 1/32 m. */
    std::uint16_t height{0};
    std::uint8_t flags{0};
    /** The sight is free from `height + sightLow / 8` to `height + sightHigh / 4` (meters). */
    std::uint8_t sightLow{0};
    std::uint8_t sightHigh{kSky};
    std::uint8_t reserved{0};
    /**
     * The area of the floor in its tile: two floors of one tile with one area have a way
     * between them in the tile. 0: no creature walks on the floor (blocked, or deep water).
     */
    std::uint16_t area{0};

    /** @return True when a creature that walks can stand on the floor. */
    [[nodiscard]] bool walkable() const {
        return (flags & (kBlocked | kDeepWater)) == 0;
    }
};

/** One floor of a cell with its height in meters, as a query gives it. */
struct NavSpot {
    float height{0.0F};
    NavFloor floor;
    /** The place of the floor in its cell, from the lowest one. */
    std::uint8_t index{0};
};

/** One tile of 64 m of the navigation data. It does not change after its load. */
class NavTile {
public:
    /** Edge of a tile in meters and in cells, and the terrain samples on one edge. */
    static constexpr std::int32_t kMeters = 64;
    static constexpr std::int32_t kCells = 256;
    static constexpr std::int32_t kSamples = kMeters + 1;
    /** A cell has this number of floors at most (the file permits more: the reader drops the highest ones). */
    static constexpr std::size_t kMaxFloors = 16;

    /** The tile in tiles of the world: the tile starts at x * 64 m, y * 64 m. */
    std::int32_t x{0};
    std::int32_t y{0};
    /** The number of areas of the tile: the areas are 1 to `areas`. */
    std::uint16_t areas{0};
    /** Floors of the file that the reader left out (more than kMaxFloors in one cell). */
    std::uint32_t floorsDropped{0};

    /** @return The terrain height at a place of the tile, in meters from its south-west corner (0 to 64). */
    [[nodiscard]] float terrain_height(float inX, float inY) const;
    /**
     * The floors of a cell, from the lowest one.
     * @param out An array of kMaxFloors entries.
     * @return The number of floors.
     */
    [[nodiscard]] std::size_t spots(std::int32_t cellX, std::int32_t cellY, NavSpot* out) const;
    /** @return The bytes that the tile has in memory. */
    [[nodiscard]] std::size_t bytes() const;

    /**
     * Makes a tile from the plain bytes of a tile of a file (the content of its zlib stream).
     * The reader uses it, and a test makes a tile with it.
     * @param sampleScale The terrain height of the sample 65535, in meters.
     * @return No tile when the bytes do not have the correct form; `problem` then has the cause.
     */
    [[nodiscard]] static std::shared_ptr<const NavTile> decode(std::int32_t tileX,
                                                               std::int32_t tileY,
                                                               const std::vector<std::uint8_t>& plain,
                                                               float sampleScale,
                                                               const NavAgent& agent,
                                                               std::string* problem);

private:
    /** Kinds of a cell word (the low 2 bits). */
    static constexpr std::uint32_t kNone = 0;
    /** One floor on the terrain with the sky above it: bits 2 to 5 the flags, bits 16 to 31 the area. */
    static constexpr std::uint32_t kPlain = 1;
    /** Floors in the list: bits 2 to 7 the number, bits 8 to 31 the index of the first one. */
    static constexpr std::uint32_t kList = 2;

    /** Finds the area of each floor: the floors of one area have a way between them in the tile. */
    void find_areas(float step);

    /** Meters for one unit of a terrain sample. */
    float m_sampleScale{0.0F};
    float m_cell{0.25F};
    /** 65 by 65 samples: the rows from the south to the north. */
    std::vector<std::uint16_t> m_terrain;
    /** One word for each cell, row by row. */
    std::vector<std::uint32_t> m_cells;
    std::vector<NavFloor> m_floors;
};

/** Counts of the navigation files, for the log and for the tests. Any thread can read them after a load. */
struct NavigationDataCounts {
    std::uint64_t tilesRead{0};
    /** Tiles that the reader refused: the file or the tile does not have the correct form. */
    std::uint64_t tilesBad{0};
    /** Tiles of a region that has no file. */
    std::uint64_t tilesNoFile{0};
};

/**
 * The navigation files of a folder. `load` reads and checks one tile. Each function is safe in
 * each thread: the world gives the loads to a thread of its own.
 */
class NavigationData {
public:
    /** Edge of one region in meters and in tiles. */
    static constexpr std::int32_t kRegionMeters = 2048;
    static constexpr std::int32_t kRegionTiles = kRegionMeters / NavTile::kMeters;
    /** The world has 7 by 7 regions: a region number above this is not a region. */
    static constexpr std::int32_t kMaxRegion = 255;

    /**
     * Sets the folder of the navigation files and reads the header of each file.
     * @return True when the folder has one file or more with a correct header.
     */
    bool set_directory(const std::string& directory);

    /** @return True when a folder with a navigation file is set. */
    [[nodiscard]] bool available() const {
        return !m_regions.empty();
    }
    /** @return The agent of the files. */
    [[nodiscard]] const NavAgent& agent() const {
        return m_agent;
    }
    /** @return The number of region files with a correct header. */
    [[nodiscard]] std::size_t regions() const {
        return m_regions.size();
    }
    /** @return The text of the first file that the reader refused, or an empty text. */
    [[nodiscard]] const std::string& problem() const {
        return m_problem;
    }
    /** @return True when the folder has a file for the region of a tile. */
    [[nodiscard]] bool has_region(std::int32_t tileX, std::int32_t tileY) const;

    /**
     * Reads one tile (64 m) and checks it.
     * @return No tile when the region has no file, or when the tile does not have the correct form.
     */
    [[nodiscard]] std::shared_ptr<const NavTile> load(std::int32_t tileX, std::int32_t tileY) const;

    [[nodiscard]] NavigationDataCounts counts() const;

private:
    struct Region {
        std::string path;
        std::uint64_t fileSize{0};
        float sampleScale{0.0F};
        /** The place, the size, and the plain size of each tile in the file. */
        std::vector<std::uint64_t> offsets;
        std::vector<std::uint32_t> sizes;
        std::vector<std::uint32_t> plainSizes;
    };

    std::map<std::pair<std::int32_t, std::int32_t>, Region> m_regions;
    NavAgent m_agent;
    std::string m_problem;
    mutable std::mutex m_mutex;
    mutable NavigationDataCounts m_counts;
};

/**
 * @return The floor of a cell that a creature at the height `height` steps to: the floor on
 *         which a creature walks that is nearest in height, and not more than `step` from it.
 *         -1: none.
 */
[[nodiscard]] int nav_step_target(const NavSpot* spots, std::size_t count, float height, float step);

} // namespace soulwarden::backend::gamedata

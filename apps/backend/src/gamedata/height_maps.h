#pragma once

#include <cstdint>
#include <fstream>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// The terrain heights of the world: the height of the terrain at a position (protocol notes
// 4.17). The backend has no terrain of its own, and the client shows a creature at the exact
// position of its states: a creature that walks needs the height of the ground at each step.
//
// The heights are game data: the tool tools/gamedata/heights.sh makes one file for each region
// of the world (2048 m by 2048 m) from the height maps of the game copy of the user, and the
// files stay outside the repository. Without the files the backend starts and works, and a
// creature does not leave its place.
//
// The world has 49 regions, and one region has 4 million samples. The backend reads a region
// only when a position in it is asked, and then only the tile of 64 m by 64 m of that
// position. It keeps the tiles that it used last.
//
// The terrain height is the height of the terrain only: a floor of a building, a bridge, and a
// cave are not in it (the navigation data of stage 3.4d has them).

namespace soulwarden::backend::gamedata {

/** Folder of the height files in the game data directory of the backend. */
inline constexpr const char* kHeightMapsFolder = "heights";

/** The source of the terrain height. A test gives the world a source that it sets. */
class Terrain {
public:
    virtual ~Terrain() = default;

    /**
     * @return The height of the terrain at a horizontal position, in meters. No value: the
     *         source has no height for the place.
     */
    [[nodiscard]] virtual std::optional<float> height(float x, float y) = 0;
};

/** Counts of the height files, for the log and for the tests. */
struct HeightMapCounts {
    /** Region files that the reader opened, and regions that have no file or a file that is not correct. */
    std::uint64_t filesOpened{0};
    std::uint64_t filesNotThere{0};
    /** Tiles that the reader decoded, and tiles that it has now. */
    std::uint64_t tilesRead{0};
    std::size_t tilesHeld{0};
    /** Tiles of a file that the reader could not decode. */
    std::uint64_t tilesBad{0};
};

class HeightMaps final : public Terrain {
public:
    /** Edge of one region and of one tile, in meters: one sample for each meter. */
    static constexpr std::uint32_t kRegionSize = 2048;
    static constexpr std::uint32_t kTileSize = 64;
    /** The reader keeps this number of tiles at most (one tile has 4,225 samples of 16 bits). */
    static constexpr std::size_t kMaxTiles = 512;

    /**
     * Sets the folder of the height files. The reader opens no file here.
     * @return True when the folder has one height file or more.
     */
    bool set_directory(const std::string& directory);

    /** @return True when a folder with a height file is set. */
    [[nodiscard]] bool available() const {
        return m_available;
    }

    [[nodiscard]] std::optional<float> height(float x, float y) override;

    [[nodiscard]] const HeightMapCounts& counts() const {
        return m_counts;
    }

private:
    struct Region {
        std::ifstream file;
        /** The place and the size of each tile in the file. Empty: the region has no file that is correct. */
        std::vector<std::pair<std::uint32_t, std::uint32_t>> tiles;
        /** The height of the sample 65535, in meters. */
        float scale{0.0F};
    };
    struct Tile {
        std::uint64_t key{0};
        /** 65 by 65 samples: the rows from the south to the north. Empty: the tile did not decode. */
        std::vector<std::uint16_t> samples;
        float scale{0.0F};
    };

    [[nodiscard]] Region& region(std::uint32_t x, std::uint32_t y);
    [[nodiscard]] const Tile* tile(std::uint32_t regionX, std::uint32_t regionY, std::uint32_t tileX, std::uint32_t tileY);

    std::string m_directory;
    bool m_available{false};
    std::unordered_map<std::uint32_t, std::unique_ptr<Region>> m_regions;
    /** The tiles, the one that was used last at the front. */
    std::list<Tile> m_tiles;
    std::unordered_map<std::uint64_t, std::list<Tile>::iterator> m_tileIndex;
    HeightMapCounts m_counts;
};

} // namespace soulwarden::backend::gamedata

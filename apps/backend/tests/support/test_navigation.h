#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "gamedata/navigation_data.h"
#include "middleware/compression/zlib.h"

// Navigation data files for the tests: a region file in the form of the tool
// (tools/gamedata/nav_file.py), with made-up content. A region of a test has the real size: 32
// by 32 tiles of 64 m with cells of 0.25 m. A tile with no description is flat terrain with one
// free floor in each cell.

namespace soulwarden::backend::tests {

/** One floor of a cell of a test tile. */
struct TestFloor {
    float height{0.0F};
    std::uint8_t flags{0};
    /** The sight above the floor, in meters from the floor: from `sightFrom` to `sightTo` (a negative `sightTo`: the sky). */
    float sightFrom{0.0F};
    float sightTo{-1.0F};
};

/** The floors of one cell of a tile (cell 0 to 255 on each axis). An empty list: no floor. */
using TestCells = std::function<void(int cellX, int cellY, std::vector<TestFloor>& floors)>;

struct TestNavRegion {
    int x{0};
    int y{0};
    /** The agent values of the file. */
    gamedata::NavAgent agent{};
    /** The terrain height of each sample of the region (meters). */
    float ground{50.0F};
    /** The terrain height of the sample 65535, in meters. */
    float heightScale{2048.0F};
    std::uint32_t version{3};
    /** The tiles with a description: the tile (0 to 31 on each axis) to the floors of its cells. */
    std::map<std::pair<int, int>, TestCells> tiles;

    static void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            out.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    }
    static void put_f32(std::vector<std::uint8_t>& out, float value) {
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        put_u32(out, bits);
    }

    /** @return The terrain sample of the flat ground. */
    [[nodiscard]] std::uint16_t sample() const {
        return static_cast<std::uint16_t>(std::lround(ground / heightScale * 65535.0F));
    }

    /** @return The plain bytes of one tile (the content of its zlib stream). */
    [[nodiscard]] std::vector<std::uint8_t> tile_bytes(const TestCells* cells) const {
        std::vector<std::uint8_t> out;
        // The terrain: each sample is the difference to the sample before it. A flat tile has one value and zeros.
        const std::uint16_t first = sample();
        out.resize(static_cast<std::size_t>(gamedata::NavTile::kSamples) * gamedata::NavTile::kSamples * 2, 0);
        out[0] = static_cast<std::uint8_t>(first & 0xFF);
        out[1] = static_cast<std::uint8_t>(first >> 8);
        std::vector<std::uint8_t> runs;
        std::vector<std::uint8_t> extras;
        std::uint32_t runCount = 0;
        std::uint32_t extraCount = 0;
        int runCode = -1;
        std::uint32_t runLength = 0;
        const auto flush = [&] {
            if (runLength != 0) {
                runs.push_back(static_cast<std::uint8_t>(runCode));
                runs.push_back(static_cast<std::uint8_t>(runLength & 0xFF));
                runs.push_back(static_cast<std::uint8_t>(runLength >> 8));
                ++runCount;
                runLength = 0;
            }
        };
        std::vector<TestFloor> floors;
        for (int cellY = 0; cellY < gamedata::NavTile::kCells; ++cellY) {
            for (int cellX = 0; cellX < gamedata::NavTile::kCells; ++cellX) {
                int code = 1;
                if (cells != nullptr) {
                    floors.clear();
                    floors.push_back({ground, gamedata::NavFloor::kTerrain});
                    (*cells)(cellX, cellY, floors);
                    const bool plain = floors.size() == 1 && (floors[0].flags & gamedata::NavFloor::kTerrain) != 0 && floors[0].sightFrom == 0.0F
                                       && floors[0].sightTo < 0.0F && floors[0].height == ground;
                    if (floors.empty()) {
                        code = 0;
                    } else if (plain) {
                        const std::uint8_t flags = floors[0].flags;
                        code = (flags & gamedata::NavFloor::kBlocked) != 0        ? 4
                               : (flags & gamedata::NavFloor::kDeepWater) != 0    ? 3
                               : (flags & gamedata::NavFloor::kShallowWater) != 0 ? 2
                                                                                  : 1;
                    } else {
                        code = 4 + static_cast<int>(floors.size());
                        for (const TestFloor& floor : floors) {
                            const auto height = static_cast<std::uint16_t>(std::lround(floor.height * 32.0F));
                            extras.push_back(static_cast<std::uint8_t>(height & 0xFF));
                            extras.push_back(static_cast<std::uint8_t>(height >> 8));
                            extras.push_back(floor.flags);
                            extras.push_back(floor.sightFrom < 0.0F ? gamedata::NavFloor::kNoSight
                                                                    : static_cast<std::uint8_t>(std::lround(floor.sightFrom * 8.0F)));
                            extras.push_back(floor.sightTo < 0.0F ? gamedata::NavFloor::kSky
                                                                  : static_cast<std::uint8_t>(std::lround(floor.sightTo * 4.0F)));
                            ++extraCount;
                        }
                    }
                }
                if (code != runCode || runLength == 65535) {
                    flush();
                    runCode = code;
                }
                ++runLength;
            }
        }
        flush();
        put_u32(out, runCount);
        out.insert(out.end(), runs.begin(), runs.end());
        put_u32(out, extraCount);
        out.insert(out.end(), extras.begin(), extras.end());
        return out;
    }

    [[nodiscard]] std::string name() const {
        char text[32];
        std::snprintf(text, sizeof(text), "r_+%02d_+%02d.swnav", x, y);
        return text;
    }

    /**
     * Writes the file of the region into a folder.
     * @param change When set: changes the bytes of the file before the write (a file that is not correct).
     */
    void write(const std::filesystem::path& folder, const std::function<void(std::vector<std::uint8_t>&)>& change = {}) const {
        std::filesystem::create_directories(folder);
        const int count = gamedata::NavigationData::kRegionTiles * gamedata::NavigationData::kRegionTiles;
        const std::vector<std::uint8_t> plainTile = tile_bytes(nullptr);
        const std::vector<std::uint8_t> plainStream = compression::zlib_store(plainTile);
        std::vector<std::vector<std::uint8_t>> streams(static_cast<std::size_t>(count));
        std::vector<std::uint32_t> plainSizes(static_cast<std::size_t>(count), static_cast<std::uint32_t>(plainTile.size()));
        for (const auto& [place, cells] : tiles) {
            const std::size_t index = static_cast<std::size_t>(place.second) * gamedata::NavigationData::kRegionTiles + place.first;
            const std::vector<std::uint8_t> bytes = tile_bytes(&cells);
            streams[index] = compression::zlib_store(bytes);
            plainSizes[index] = static_cast<std::uint32_t>(bytes.size());
        }
        std::vector<std::uint8_t> file;
        file.insert(file.end(), {'S', 'W', 'N', 'V'});
        put_u32(file, version);
        put_u32(file, static_cast<std::uint32_t>(x));
        put_u32(file, static_cast<std::uint32_t>(y));
        for (const float value : {agent.cell, agent.radius, agent.height, agent.step, agent.slope, agent.wade}) {
            put_f32(file, value);
        }
        put_u32(file, static_cast<std::uint32_t>(gamedata::NavTile::kCells));
        put_u32(file, static_cast<std::uint32_t>(gamedata::NavigationData::kRegionTiles));
        put_f32(file, 40.0F);
        put_f32(file, heightScale);
        put_u32(file, 0);
        put_u32(file, 0);
        std::uint64_t offset = file.size() + static_cast<std::size_t>(count) * 16;
        for (int index = 0; index < count; ++index) {
            const std::vector<std::uint8_t>& stream = streams[static_cast<std::size_t>(index)].empty() ? plainStream : streams[static_cast<std::size_t>(index)];
            put_u32(file, static_cast<std::uint32_t>(offset & 0xFFFFFFFFU));
            put_u32(file, static_cast<std::uint32_t>(offset >> 32));
            put_u32(file, static_cast<std::uint32_t>(stream.size()));
            put_u32(file, plainSizes[static_cast<std::size_t>(index)]);
            offset += stream.size();
        }
        for (int index = 0; index < count; ++index) {
            const std::vector<std::uint8_t>& stream = streams[static_cast<std::size_t>(index)].empty() ? plainStream : streams[static_cast<std::size_t>(index)];
            file.insert(file.end(), stream.begin(), stream.end());
        }
        if (change) {
            change(file);
        }
        std::ofstream out(folder / name(), std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    }
};

} // namespace soulwarden::backend::tests

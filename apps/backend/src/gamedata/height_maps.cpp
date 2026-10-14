#include "gamedata/height_maps.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "middleware/compression/zlib.h"

// The form of one height file (tools/gamedata/height_maps.py), all numbers little-endian:
//
//   header  "SWHT" version:u32 (1) regionX:i32 regionY:i32 size:u32 (2048) tile:u32 (64)
//           scale:f32 (the height of the sample 65535, in meters) reserved:u32
//   index   (size / tile)^2 entries of offset:u32 length:u32: the tiles from the south to the
//           north, and in each row from the west to the east
//   tiles   one zlib stream for each tile: 65 by 65 samples of 16 bits, the rows from the south
//           to the north. The last row and the last column are the first samples of the next
//           tile. A sample is its difference to the sample to its west, and the first sample
//           of a row is its difference to the first sample of the row to its south.

namespace soulwarden::backend::gamedata {
namespace {

constexpr char kMagic[4] = {'S', 'W', 'H', 'T'};
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kHeaderSize = 32;
constexpr std::uint32_t kTilesInRow = HeightMaps::kRegionSize / HeightMaps::kTileSize;
/** Samples of one edge of a tile: one more than its size, for the height between two tiles. */
constexpr std::uint32_t kTileSide = HeightMaps::kTileSize + 1;
constexpr std::size_t kTileBytes = static_cast<std::size_t>(kTileSide) * kTileSide * 2;
/** A zlib stream of a tile is not larger than this (bytes): a larger value is a fault of the file. */
constexpr std::uint32_t kMaxTileStream = 65536;
/** The world has 7 by 7 regions: a region number above this is not a region. */
constexpr std::uint32_t kMaxRegion = 255;
/** The terrain of the world is not higher than this (meters): a file with a larger scale is not a height file. */
constexpr float kMaxScale = 4096.0F;

std::uint32_t read_u32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8)
           | (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

std::string region_file(const std::string& directory, std::uint32_t x, std::uint32_t y) {
    char name[32];
    std::snprintf(name, sizeof(name), "r_+%02u_+%02u.swh", x, y);
    return (std::filesystem::path(directory) / name).string();
}

} // namespace

bool HeightMaps::set_directory(const std::string& directory) {
    m_directory = directory;
    m_regions.clear();
    m_tiles.clear();
    m_tileIndex.clear();
    m_available = false;
    std::error_code error;
    for (std::filesystem::directory_iterator entry(directory, error), end; !error && entry != end; entry.increment(error)) {
        if (entry->path().extension() == ".swh") {
            m_available = true;
            break;
        }
    }
    return m_available;
}

HeightMaps::Region& HeightMaps::region(std::uint32_t x, std::uint32_t y) {
    const std::uint32_t key = (x << 16) | y;
    const auto found = m_regions.find(key);
    if (found != m_regions.end()) {
        return *found->second;
    }
    Region& opened = *m_regions.emplace(key, std::make_unique<Region>()).first->second;
    opened.file.open(region_file(m_directory, x, y), std::ios::binary);
    std::uint8_t header[kHeaderSize];
    std::vector<std::uint8_t> index(static_cast<std::size_t>(kTilesInRow) * kTilesInRow * 8);
    if (!opened.file || !opened.file.read(reinterpret_cast<char*>(header), sizeof(header))
        || std::memcmp(header, kMagic, sizeof(kMagic)) != 0 || read_u32(header + 4) != kVersion || read_u32(header + 8) != x
        || read_u32(header + 12) != y || read_u32(header + 16) != kRegionSize || read_u32(header + 20) != kTileSize
        || !opened.file.read(reinterpret_cast<char*>(index.data()), static_cast<std::streamsize>(index.size()))) {
        // No file, or a file of a different form: the region has no height.
        opened.file.close();
        ++m_counts.filesNotThere;
        return opened;
    }
    std::uint32_t bits = read_u32(header + 24);
    std::memcpy(&opened.scale, &bits, sizeof(opened.scale));
    if (!std::isfinite(opened.scale) || !(opened.scale > 0.0F) || opened.scale > kMaxScale) {
        opened.file.close();
        ++m_counts.filesNotThere;
        return opened;
    }
    opened.tiles.reserve(static_cast<std::size_t>(kTilesInRow) * kTilesInRow);
    for (std::size_t entry = 0; entry < index.size(); entry += 8) {
        opened.tiles.emplace_back(read_u32(index.data() + entry), read_u32(index.data() + entry + 4));
    }
    ++m_counts.filesOpened;
    return opened;
}

const HeightMaps::Tile* HeightMaps::tile(std::uint32_t regionX, std::uint32_t regionY, std::uint32_t tileX, std::uint32_t tileY) {
    const std::uint64_t key = (static_cast<std::uint64_t>(regionX) << 48) | (static_cast<std::uint64_t>(regionY) << 32)
                              | (static_cast<std::uint64_t>(tileX) << 16) | tileY;
    const auto held = m_tileIndex.find(key);
    if (held != m_tileIndex.end()) {
        // The tile that was used last is at the front.
        m_tiles.splice(m_tiles.begin(), m_tiles, held->second);
        return held->second->samples.empty() ? nullptr : &*held->second;
    }
    Region& source = region(regionX, regionY);
    Tile loaded;
    loaded.key = key;
    loaded.scale = source.scale;
    if (!source.tiles.empty()) {
        const auto [offset, length] = source.tiles[static_cast<std::size_t>(tileY) * kTilesInRow + tileX];
        std::vector<std::uint8_t> stream(length <= kMaxTileStream ? length : 0);
        std::vector<std::uint8_t> plain;
        source.file.clear();
        if (!stream.empty() && source.file.seekg(offset) && source.file.read(reinterpret_cast<char*>(stream.data()), length)
            && compression::zlib_decode(stream, kTileBytes, plain) && plain.size() == kTileBytes) {
            loaded.samples.resize(static_cast<std::size_t>(kTileSide) * kTileSide);
            std::uint16_t south = 0;
            for (std::uint32_t row = 0; row < kTileSide; ++row) {
                std::uint16_t value = south;
                for (std::uint32_t column = 0; column < kTileSide; ++column) {
                    const std::size_t place = static_cast<std::size_t>(row) * kTileSide + column;
                    const auto delta = static_cast<std::uint16_t>(plain[place * 2] | (plain[place * 2 + 1] << 8));
                    value = static_cast<std::uint16_t>(value + delta);
                    if (column == 0) {
                        south = value;
                    }
                    loaded.samples[place] = value;
                }
            }
            ++m_counts.tilesRead;
        } else {
            ++m_counts.tilesBad;
        }
    }
    // A tile that did not decode stays in the list with no sample: the reader does not try it again.
    m_tiles.push_front(std::move(loaded));
    m_tileIndex[key] = m_tiles.begin();
    while (m_tiles.size() > kMaxTiles) {
        m_tileIndex.erase(m_tiles.back().key);
        m_tiles.pop_back();
    }
    m_counts.tilesHeld = m_tiles.size();
    return m_tiles.front().samples.empty() ? nullptr : &m_tiles.front();
}

std::optional<float> HeightMaps::height(float x, float y) {
    if (!m_available || !std::isfinite(x) || !std::isfinite(y) || x < 0.0F || y < 0.0F) {
        return std::nullopt;
    }
    const auto regionX = static_cast<std::uint32_t>(x / static_cast<float>(kRegionSize));
    const auto regionY = static_cast<std::uint32_t>(y / static_cast<float>(kRegionSize));
    if (regionX > kMaxRegion || regionY > kMaxRegion) {
        return std::nullopt;
    }
    // The position in the region, and in its tile: the samples are at whole meters.
    const float localX = x - static_cast<float>(regionX * kRegionSize);
    const float localY = y - static_cast<float>(regionY * kRegionSize);
    const std::uint32_t tileX = std::min(static_cast<std::uint32_t>(localX) / kTileSize, kTilesInRow - 1);
    const std::uint32_t tileY = std::min(static_cast<std::uint32_t>(localY) / kTileSize, kTilesInRow - 1);
    const Tile* samples = tile(regionX, regionY, tileX, tileY);
    if (samples == nullptr) {
        return std::nullopt;
    }
    const float inX = localX - static_cast<float>(tileX * kTileSize);
    const float inY = localY - static_cast<float>(tileY * kTileSize);
    const std::uint32_t lowX = std::min(static_cast<std::uint32_t>(inX), kTileSize - 1);
    const std::uint32_t lowY = std::min(static_cast<std::uint32_t>(inY), kTileSize - 1);
    const auto at = [samples](std::uint32_t column, std::uint32_t row) {
        return static_cast<float>(samples->samples[static_cast<std::size_t>(row) * kTileSide + column]);
    };
    // The height between the four samples around the position.
    const float partX = inX - static_cast<float>(lowX);
    const float partY = inY - static_cast<float>(lowY);
    const float south = at(lowX, lowY) + (at(lowX + 1, lowY) - at(lowX, lowY)) * partX;
    const float north = at(lowX, lowY + 1) + (at(lowX + 1, lowY + 1) - at(lowX, lowY + 1)) * partX;
    const float height = (south + (north - south) * partY) / 65535.0F * samples->scale;
    // No height that is not a height of the world goes to the navigation.
    if (!std::isfinite(height) || height < 0.0F || height > kMaxScale) {
        return std::nullopt;
    }
    return height;
}

} // namespace soulwarden::backend::gamedata

#include "gamedata/navigation_data.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numeric>

#include "middleware/compression/zlib.h"

// The file of one region (tools/gamedata/nav_file.py writes it; all numbers little-endian):
//
//   header     "SWNV" version:u32 (3) regionX:i32 regionY:i32
//              cell:f32 radius:f32 height:f32 step:f32 slope:f32 wade:f32
//              tileCells:u32 (256) tilesPerSide:u32 (32) ocean:f32
//              heightScale:f32 (the terrain height of the sample 65535, in meters) reserved:u32 * 2
//   directory  (offset:u64 size:u32 plainSize:u32) for each tile, from the south to the north and
//              in each row from the west to the east
//   tile       one zlib stream of: terrain runCount:u32 run* extraCount:u32 extra*
//   terrain    65 by 65 samples of 16 bits, each one stored as its difference to the sample to
//              its west (the first one of a row: to the first one of the row to its south)
//   run        code:u8 length:u16: 0 no floor; 1 to 3 one free floor on the terrain (no water,
//              shallow water, deep water); 4 the terrain, no place for a creature; 4 + n: n floors
//              of the extra list for each cell
//   extra      height:u16 (1/32 m) flags:u8 sightLow:u8 sightHigh:u8

namespace soulwarden::backend::gamedata {
namespace {

constexpr char kMagic[4] = {'S', 'W', 'N', 'V'};
constexpr std::uint32_t kVersion = 3;
constexpr std::size_t kHeaderSize = 64;
constexpr std::size_t kEntrySize = 16;
constexpr std::size_t kTerrainBytes = static_cast<std::size_t>(NavTile::kSamples) * NavTile::kSamples * 2;
constexpr std::size_t kCellCount = static_cast<std::size_t>(NavTile::kCells) * NavTile::kCells;
constexpr std::size_t kExtraSize = 5;
constexpr int kCodeFloors = 4;
/** A zlib stream of a tile and its content are not larger than this (bytes): a larger value is a fault of the file. */
constexpr std::uint32_t kMaxTileBytes = 16U << 20;
/** The terrain of the world is not higher than this (meters): the scale of a file gives no larger height. */
constexpr float kMaxTerrainHeight = 4096.0F;

std::uint32_t read_u32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8)
           | (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

std::uint64_t read_u64(const std::uint8_t* bytes) {
    return static_cast<std::uint64_t>(read_u32(bytes)) | (static_cast<std::uint64_t>(read_u32(bytes + 4)) << 32);
}

float read_f32(const std::uint8_t* bytes) {
    const std::uint32_t bits = read_u32(bytes);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool in_range(float value, float low, float high) {
    return std::isfinite(value) && value >= low && value <= high;
}

/** @return True when the values are the values of an agent that the backend can use. */
bool valid_agent(const NavAgent& agent) {
    return agent.cell == 0.25F && in_range(agent.radius, 0.0F, 4.0F) && in_range(agent.height, 0.25F, 8.0F)
           && in_range(agent.step, 0.01F, 4.0F) && in_range(agent.slope, 1.0F, 89.0F) && in_range(agent.wade, 0.0F, 16.0F);
}

std::int32_t floor_div(std::int32_t value, std::int32_t divisor) {
    return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}

} // namespace

int nav_step_target(const NavSpot* spots, std::size_t count, float height, float step) {
    int best = -1;
    float nearest = step + 0.001F;
    for (std::size_t index = 0; index < count; ++index) {
        if (!spots[index].floor.walkable()) {
            continue;
        }
        const float difference = std::fabs(spots[index].height - height);
        if (difference <= nearest) {
            nearest = difference;
            best = static_cast<int>(index);
        }
    }
    return best;
}

float NavTile::terrain_height(float inX, float inY) const {
    const float x = std::clamp(inX, 0.0F, static_cast<float>(kMeters));
    const float y = std::clamp(inY, 0.0F, static_cast<float>(kMeters));
    const auto lowX = std::min(static_cast<std::int32_t>(x), kMeters - 1);
    const auto lowY = std::min(static_cast<std::int32_t>(y), kMeters - 1);
    const float partX = x - static_cast<float>(lowX);
    const float partY = y - static_cast<float>(lowY);
    const std::uint16_t* row = m_terrain.data() + static_cast<std::size_t>(lowY) * kSamples + lowX;
    const float south = static_cast<float>(row[0]) + (static_cast<float>(row[1]) - static_cast<float>(row[0])) * partX;
    const float north = static_cast<float>(row[kSamples])
                        + (static_cast<float>(row[kSamples + 1]) - static_cast<float>(row[kSamples])) * partX;
    return (south + (north - south) * partY) * m_sampleScale;
}

std::size_t NavTile::spots(std::int32_t cellX, std::int32_t cellY, NavSpot* out) const {
    const std::uint32_t word = m_cells[static_cast<std::size_t>(cellY) * kCells + cellX];
    const std::uint32_t kind = word & 3U;
    if (kind == kNone) {
        return 0;
    }
    const float ground = terrain_height((static_cast<float>(cellX) + 0.5F) * m_cell, (static_cast<float>(cellY) + 0.5F) * m_cell);
    if (kind == kPlain) {
        NavSpot& spot = out[0];
        spot.height = ground;
        spot.floor = NavFloor{};
        spot.floor.height = static_cast<std::uint16_t>(std::clamp(std::lround(ground * 32.0F), 0L, 65535L));
        spot.floor.flags = static_cast<std::uint8_t>((word >> 2) & 0xFU);
        spot.floor.area = static_cast<std::uint16_t>(word >> 16);
        spot.index = 0;
        return 1;
    }
    const std::size_t count = (word >> 2) & 0x3FU;
    const NavFloor* floors = m_floors.data() + (word >> 8);
    for (std::size_t index = 0; index < count; ++index) {
        out[index].floor = floors[index];
        out[index].height = (floors[index].flags & NavFloor::kTerrain) != 0 ? ground : static_cast<float>(floors[index].height) / 32.0F;
        out[index].index = static_cast<std::uint8_t>(index);
    }
    return count;
}

std::size_t NavTile::bytes() const {
    return sizeof(NavTile) + m_terrain.capacity() * sizeof(std::uint16_t) + m_cells.capacity() * sizeof(std::uint32_t)
           + m_floors.capacity() * sizeof(NavFloor);
}

void NavTile::find_areas(float step) {
    // One number for each floor of the tile: the floors of a cell follow each other.
    std::vector<std::uint32_t> base(kCellCount + 1);
    std::uint32_t total = 0;
    for (std::size_t cell = 0; cell < kCellCount; ++cell) {
        base[cell] = total;
        const std::uint32_t word = m_cells[cell];
        total += (word & 3U) == kPlain ? 1U : ((word & 3U) == kList ? (word >> 2) & 0x3FU : 0U);
    }
    base[kCellCount] = total;
    std::vector<std::uint32_t> parent(total);
    std::iota(parent.begin(), parent.end(), 0U);
    const auto find = [&parent](std::uint32_t item) {
        while (parent[item] != item) {
            parent[item] = parent[parent[item]];
            item = parent[item];
        }
        return item;
    };
    const auto join = [&](std::uint32_t first, std::uint32_t second) {
        const std::uint32_t a = find(first);
        const std::uint32_t b = find(second);
        if (a != b) {
            parent[std::max(a, b)] = std::min(a, b);
        }
    };
    // A step to the east and a step to the north, in the two directions. A step to a cell on a
    // diagonal needs its two side steps: the areas are the same with the four side steps only.
    NavSpot here[kMaxFloors];
    NavSpot there[kMaxFloors];
    for (std::int32_t y = 0; y < kCells; ++y) {
        for (std::int32_t x = 0; x < kCells; ++x) {
            const std::size_t cell = static_cast<std::size_t>(y) * kCells + x;
            const std::uint32_t word = m_cells[cell];
            if ((word & 3U) == kNone) {
                continue;
            }
            // A cell with one free floor on the terrain, as most cells are.
            const bool plain = (word & 3U) == kPlain && ((word >> 2) & (NavFloor::kBlocked | NavFloor::kDeepWater)) == 0;
            std::size_t count = 0;
            bool read = false;
            for (int side = 0; side < 2; ++side) {
                const std::int32_t otherX = x + (side == 0 ? 1 : 0);
                const std::int32_t otherY = y + (side == 0 ? 0 : 1);
                if (otherX >= kCells || otherY >= kCells) {
                    continue;
                }
                const std::size_t other = static_cast<std::size_t>(otherY) * kCells + otherX;
                const std::uint32_t otherWord = m_cells[other];
                if (plain && (otherWord & 3U) == kPlain && ((otherWord >> 2) & (NavFloor::kBlocked | NavFloor::kDeepWater)) == 0) {
                    // Two free cells of the terrain side by side: the terrain between them is not
                    // steeper than the slope limit of the tool, thus the step is not higher than the step limit.
                    join(base[cell], base[other]);
                    continue;
                }
                if (!read) {
                    count = spots(x, y, here);
                    read = true;
                }
                const std::size_t otherCount = spots(otherX, otherY, there);
                for (std::size_t index = 0; index < count; ++index) {
                    if (here[index].floor.walkable()) {
                        const int target = nav_step_target(there, otherCount, here[index].height, step);
                        if (target >= 0) {
                            join(base[cell] + static_cast<std::uint32_t>(index), base[other] + static_cast<std::uint32_t>(target));
                        }
                    }
                }
                for (std::size_t index = 0; index < otherCount; ++index) {
                    if (there[index].floor.walkable()) {
                        const int target = nav_step_target(here, count, there[index].height, step);
                        if (target >= 0) {
                            join(base[other] + static_cast<std::uint32_t>(index), base[cell] + static_cast<std::uint32_t>(target));
                        }
                    }
                }
            }
        }
    }
    // The area numbers: 1 and up. A tile with more areas than a number of 16 bits has (a tile
    // of single cells) gives the last ones no area: no creature walks there.
    std::vector<std::uint16_t> label(total, 0);
    std::uint32_t next = 0;
    for (std::size_t cell = 0; cell < kCellCount; ++cell) {
        const std::uint32_t word = m_cells[cell];
        const std::uint32_t kind = word & 3U;
        const std::uint32_t count = base[cell + 1] - base[cell];
        for (std::uint32_t index = 0; index < count; ++index) {
            const bool walkable = kind == kPlain ? (((word >> 2) & (NavFloor::kBlocked | NavFloor::kDeepWater)) == 0)
                                                 : m_floors[(word >> 8) + index].walkable();
            if (!walkable) {
                continue;
            }
            const std::uint32_t root = find(base[cell] + index);
            if (label[root] == 0 && next < 65535U) {
                label[root] = static_cast<std::uint16_t>(++next);
            }
            if (kind == kPlain) {
                m_cells[cell] = (word & 0xFFFFU) | (static_cast<std::uint32_t>(label[root]) << 16);
            } else {
                m_floors[(word >> 8) + index].area = label[root];
            }
        }
    }
    areas = static_cast<std::uint16_t>(next);
}

std::shared_ptr<const NavTile> NavTile::decode(std::int32_t tileX,
                                               std::int32_t tileY,
                                               const std::vector<std::uint8_t>& plain,
                                               float sampleScale,
                                               const NavAgent& agent,
                                               std::string* problem) {
    const auto refuse = [problem](const char* cause) {
        if (problem != nullptr) {
            *problem = cause;
        }
        return std::shared_ptr<const NavTile>();
    };
    if (!valid_agent(agent) || !in_range(sampleScale, 0.001F, kMaxTerrainHeight)) {
        return refuse("the agent values or the terrain scale are not in their range");
    }
    if (plain.size() < kTerrainBytes + 8) {
        return refuse("the tile is shorter than its terrain samples");
    }
    auto tile = std::make_shared<NavTile>();
    tile->x = tileX;
    tile->y = tileY;
    tile->m_cell = agent.cell;
    tile->m_sampleScale = sampleScale / 65535.0F;
    tile->m_terrain.resize(static_cast<std::size_t>(kSamples) * kSamples);
    std::uint16_t south = 0;
    for (std::int32_t row = 0; row < kSamples; ++row) {
        std::uint16_t value = south;
        for (std::int32_t column = 0; column < kSamples; ++column) {
            const std::size_t place = static_cast<std::size_t>(row) * kSamples + column;
            const auto delta = static_cast<std::uint16_t>(plain[place * 2] | (plain[place * 2 + 1] << 8));
            value = static_cast<std::uint16_t>(value + delta);
            if (column == 0) {
                south = value;
            }
            tile->m_terrain[place] = value;
        }
    }
    const std::uint8_t* data = plain.data();
    const std::size_t size = plain.size();
    const std::uint32_t runCount = read_u32(data + kTerrainBytes);
    const std::size_t runs = kTerrainBytes + 4;
    if (runCount > (size - runs) / 3 || size - runs - static_cast<std::size_t>(runCount) * 3 < 4) {
        return refuse("the run list is longer than the tile");
    }
    const std::uint32_t extraCount = read_u32(data + runs + static_cast<std::size_t>(runCount) * 3);
    const std::size_t extras = runs + static_cast<std::size_t>(runCount) * 3 + 4;
    if (extraCount > (size - extras) / kExtraSize || extras + static_cast<std::size_t>(extraCount) * kExtraSize != size) {
        return refuse("the floor list does not have the size of the tile");
    }
    tile->m_cells.assign(kCellCount, kNone);
    tile->m_floors.reserve(extraCount);
    std::size_t cell = 0;
    std::size_t used = 0;
    for (std::uint32_t run = 0; run < runCount; ++run) {
        const std::uint8_t code = data[runs + static_cast<std::size_t>(run) * 3];
        const std::size_t length = static_cast<std::size_t>(data[runs + static_cast<std::size_t>(run) * 3 + 1])
                                   | (static_cast<std::size_t>(data[runs + static_cast<std::size_t>(run) * 3 + 2]) << 8);
        if (length > kCellCount - cell) {
            return refuse("the runs have more cells than the tile");
        }
        if (code == 0) {
            cell += length;
            continue;
        }
        if (code <= kCodeFloors) {
            static constexpr std::uint8_t kFlags[5] = {0, NavFloor::kTerrain, NavFloor::kTerrain | NavFloor::kShallowWater,
                                                      NavFloor::kTerrain | NavFloor::kDeepWater,
                                                      NavFloor::kTerrain | NavFloor::kBlocked};
            const std::uint32_t word = kPlain | (static_cast<std::uint32_t>(kFlags[code]) << 2);
            std::fill_n(tile->m_cells.begin() + static_cast<std::ptrdiff_t>(cell), length, word);
            cell += length;
            continue;
        }
        const std::size_t number = static_cast<std::size_t>(code) - kCodeFloors;
        if (number > (extraCount - used) / std::max<std::size_t>(length, 1) && length != 0) {
            return refuse("a run names more floors than the floor list has");
        }
        if (number > kMaxFloors) {
            // The reader keeps no part of a cell (code review of stage 3.4d, finding F3).
            static_assert(kMaxFloors == 63, "the text of the refusal names the limit");
            return refuse("a run names more floors for one cell than the reader keeps (63)");
        }
        for (std::size_t index = 0; index < length; ++index, ++cell) {
            const std::size_t first = tile->m_floors.size();
            std::uint16_t before = 0;
            for (std::size_t floor = 0; floor < number; ++floor, ++used) {
                const std::uint8_t* record = data + extras + used * kExtraSize;
                NavFloor value;
                value.height = static_cast<std::uint16_t>(record[0] | (record[1] << 8));
                value.flags = record[2];
                value.sightLow = record[3];
                value.sightHigh = record[4];
                if ((value.flags & ~NavFloor::kKnownFlags) != 0 || value.height < before) {
                    return refuse("a floor has a flag that is not known, or the floors of a cell are not in the sequence of their heights");
                }
                before = value.height;
                tile->m_floors.push_back(value);
            }
            const std::size_t kept = number;
            if (first >= (1U << 24)) {
                return refuse("the tile has more floors than the reader can number");
            }
            tile->m_cells[cell] = kList | (static_cast<std::uint32_t>(kept) << 2) | (static_cast<std::uint32_t>(first) << 8);
        }
    }
    if (cell != kCellCount || used != extraCount) {
        return refuse("the runs do not have each cell of the tile, or the floor list has floors of no cell");
    }
    tile->find_areas(agent.step);
    return tile;
}

bool NavigationData::set_directory(const std::string& directory) {
    m_regions.clear();
    m_problem.clear();
    m_agent = NavAgent{};
    bool first = true;
    std::error_code error;
    std::vector<std::filesystem::path> files;
    for (std::filesystem::directory_iterator entry(directory, error), end; !error && entry != end; entry.increment(error)) {
        if (entry->path().extension() == ".swnav") {
            files.push_back(entry->path());
        }
    }
    std::sort(files.begin(), files.end());
    for (const std::filesystem::path& path : files) {
        const auto refuse = [&](const char* cause) {
            if (m_problem.empty()) {
                m_problem = path.filename().string() + ": " + cause;
            }
        };
        int nameX = 0;
        int nameY = 0;
        if (std::sscanf(path.filename().string().c_str(), "r_+%d_+%d.swnav", &nameX, &nameY) != 2 || nameX < 0 || nameY < 0
            || nameX > kMaxRegion || nameY > kMaxRegion) {
            refuse("the name is not the name of a region");
            continue;
        }
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            refuse("the file does not open");
            continue;
        }
        Region region;
        region.path = path.string();
        region.fileSize = static_cast<std::uint64_t>(file.tellg());
        const std::size_t tiles = static_cast<std::size_t>(kRegionTiles) * kRegionTiles;
        std::vector<std::uint8_t> head(kHeaderSize + tiles * kEntrySize);
        file.seekg(0);
        if (region.fileSize < head.size() || !file.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()))) {
            refuse("the file is shorter than its header");
            continue;
        }
        NavAgent agent;
        agent.cell = read_f32(head.data() + 16);
        agent.radius = read_f32(head.data() + 20);
        agent.height = read_f32(head.data() + 24);
        agent.step = read_f32(head.data() + 28);
        agent.slope = read_f32(head.data() + 32);
        agent.wade = read_f32(head.data() + 36);
        region.sampleScale = read_f32(head.data() + 52);
        if (std::memcmp(head.data(), kMagic, sizeof(kMagic)) != 0 || read_u32(head.data() + 4) != kVersion
            || static_cast<std::int32_t>(read_u32(head.data() + 8)) != nameX || static_cast<std::int32_t>(read_u32(head.data() + 12)) != nameY
            || read_u32(head.data() + 40) != static_cast<std::uint32_t>(NavTile::kCells)
            || read_u32(head.data() + 44) != static_cast<std::uint32_t>(kRegionTiles) || !valid_agent(agent)
            || !in_range(region.sampleScale, 0.001F, kMaxTerrainHeight)) {
            refuse("the header is not the header of a navigation file of this version, or its values are not in their range");
            continue;
        }
        if (first) {
            m_agent = agent;
        } else if (agent.radius != m_agent.radius || agent.height != m_agent.height || agent.step != m_agent.step
                   || agent.slope != m_agent.slope || agent.wade != m_agent.wade) {
            refuse("the agent values are not the values of the other files");
            continue;
        }
        bool correct = true;
        region.offsets.reserve(tiles);
        region.sizes.reserve(tiles);
        region.plainSizes.reserve(tiles);
        for (std::size_t tile = 0; tile < tiles && correct; ++tile) {
            const std::uint8_t* entry = head.data() + kHeaderSize + tile * kEntrySize;
            const std::uint64_t offset = read_u64(entry);
            const std::uint32_t size = read_u32(entry + 8);
            const std::uint32_t plainSize = read_u32(entry + 12);
            correct = offset >= head.size() && offset <= region.fileSize && size <= region.fileSize - offset && size <= kMaxTileBytes
                      && plainSize <= kMaxTileBytes && plainSize >= kTerrainBytes + 8;
            region.offsets.push_back(offset);
            region.sizes.push_back(size);
            region.plainSizes.push_back(plainSize);
        }
        if (!correct) {
            refuse("a tile of the directory is not in the file, or it is larger than a tile can be");
            continue;
        }
        first = false;
        m_regions.emplace(std::make_pair(nameX, nameY), std::move(region));
    }
    if (!m_problem.empty()) {
        // A file that is not correct refuses the folder as a whole: a folder with a part of its
        // regions gives the terrain rule in the others with no sign (code review of stage 3.4d,
        // finding F9: different agent values in two files).
        m_regions.clear();
        m_agent = NavAgent{};
        return false;
    }
    return !m_regions.empty();
}

bool NavigationData::has_region(std::int32_t tileX, std::int32_t tileY) const {
    return m_regions.count(std::make_pair(floor_div(tileX, kRegionTiles), floor_div(tileY, kRegionTiles))) != 0;
}

NavigationDataCounts NavigationData::counts() const {
    std::lock_guard lock(m_mutex);
    return m_counts;
}

std::shared_ptr<const NavTile> NavigationData::load(std::int32_t tileX, std::int32_t tileY) const {
    const std::int32_t regionX = floor_div(tileX, kRegionTiles);
    const std::int32_t regionY = floor_div(tileY, kRegionTiles);
    const auto found = m_regions.find(std::make_pair(regionX, regionY));
    const auto count = [this](std::uint64_t NavigationDataCounts::* member) {
        std::lock_guard lock(m_mutex);
        ++(m_counts.*member);
    };
    if (found == m_regions.end()) {
        count(&NavigationDataCounts::tilesNoFile);
        return nullptr;
    }
    const Region& region = found->second;
    const std::size_t index = static_cast<std::size_t>(tileY - regionY * kRegionTiles) * kRegionTiles
                              + static_cast<std::size_t>(tileX - regionX * kRegionTiles);
    // Each load opens the file: the function is safe in each thread, and a region file stays closed between the loads.
    std::vector<std::uint8_t> stream(region.sizes[index]);
    std::vector<std::uint8_t> plain;
    std::ifstream file(region.path, std::ios::binary);
    std::shared_ptr<const NavTile> tile;
    std::size_t used = 0;
    if (file && file.seekg(static_cast<std::streamoff>(region.offsets[index]))
        && file.read(reinterpret_cast<char*>(stream.data()), static_cast<std::streamsize>(stream.size()))
        && compression::zlib_decode(stream, region.plainSizes[index], plain, &used) && plain.size() == region.plainSizes[index]
        // One zlib stream and nothing after it (code review of stage 3.4d, finding F4).
        && used == stream.size()) {
        tile = NavTile::decode(tileX, tileY, plain, region.sampleScale, m_agent, nullptr);
    }
    count(tile ? &NavigationDataCounts::tilesRead : &NavigationDataCounts::tilesBad);
    return tile;
}

} // namespace soulwarden::backend::gamedata

#include "world/navigation.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/logging/log.h"
#include "gamedata/creature_data.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

using gamedata::NavFloor;
using gamedata::NavSpot;
using gamedata::NavTile;

constexpr float kSqrt2 = 1.41421356F;

std::int32_t floor_div(std::int32_t value, std::int32_t divisor) {
    return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}

/** @return True when the numbers of a position can be a place of the navigation data. */
bool usable(const state::WorldPosition& position) {
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z) && position.x >= 0.0F
           && position.y >= 0.0F && position.x < 1.0e6F && position.y < 1.0e6F;
}

/** The areas of two tiles that have a way between them: the number of an area of the world. */
std::uint64_t area_number(std::int32_t tileX, std::int32_t tileY, std::uint16_t area) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(tileX) & 0xFFFFFFU) << 40)
           | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(tileY) & 0xFFFFFFU) << 16) | area;
}

} // namespace

// ---- the path search -------------------------------------------------------------------------

/**
 * A* on the cells of the navigation data. A search can stop after a number of steps and
 * continue later: the world gives each tick a number of steps for all searches.
 */
struct Navigation::Search {
    struct Slot {
        std::uint64_t key{0};
        float cost{0.0F};
        float height{0.0F};
        std::uint32_t parent{0};
        std::uint32_t stamp{0};
        std::uint8_t closed{0};
    };
    struct Open {
        float estimate;
        float cost;
        std::uint32_t slot;
    };
    /** The table has this number of places: two times the cells that one search can reach in its limit, and more. */
    static constexpr std::size_t kSlots = std::size_t{1} << 18;

    explicit Search(const Navigation& owner) : navigation(owner), slots(kSlots) {}

    const Navigation& navigation;
    std::vector<Slot> slots;
    std::vector<Open> open;
    std::uint32_t stamp{0};
    std::size_t used{0};
    Place start;
    Place goal;
    std::uint64_t goalKey{0};
    std::uint32_t goalSlot{0};
    std::uint32_t steps{0};
    bool running{false};
    bool found{false};
    bool full{false};

    [[nodiscard]] static std::uint64_t key_of(const Place& place) {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(place.x)) << 28)
               | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(place.y) & 0xFFFFFFU) << 4) | place.floor;
    }

    /** @return The place of a key in the table: a new one has `fresh`. No value: the table is full. */
    [[nodiscard]] std::optional<std::uint32_t> slot_of(std::uint64_t key, bool& fresh) {
        std::size_t index = static_cast<std::size_t>((key * 0x9E3779B97F4A7C15ULL) >> 45) & (kSlots - 1);
        for (std::size_t probe = 0; probe < 64; ++probe, index = (index + 1) & (kSlots - 1)) {
            Slot& slot = slots[index];
            if (slot.stamp != stamp) {
                if (used >= kSlots / 2) {
                    return std::nullopt;
                }
                slot = Slot{};
                slot.key = key;
                slot.stamp = stamp;
                ++used;
                fresh = true;
                return static_cast<std::uint32_t>(index);
            }
            if (slot.key == key) {
                fresh = false;
                return static_cast<std::uint32_t>(index);
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] float estimate(std::int32_t x, std::int32_t y) const {
        const auto dx = static_cast<float>(std::abs(x - goal.x));
        const auto dy = static_cast<float>(std::abs(y - goal.y));
        return (std::max(dx, dy) + (kSqrt2 - 1.0F) * std::min(dx, dy)) * navigation.m_agent.cell;
    }

    void begin(const Place& from, const Place& to) {
        if (++stamp == 0) {
            // The stamp went around: no old place must look like a place of this search.
            for (Slot& slot : slots) {
                slot.stamp = 0;
            }
            stamp = 1;
        }
        open.clear();
        used = 0;
        steps = 0;
        start = from;
        goal = to;
        goalKey = key_of(to);
        found = false;
        full = false;
        running = true;
        bool fresh = false;
        const std::optional<std::uint32_t> first = slot_of(key_of(from), fresh);
        Slot& slot = slots[*first];
        slot.cost = 0.0F;
        slot.height = from.height;
        slot.parent = *first;
        open.push_back({estimate(from.x, from.y), 0.0F, *first});
    }

    void visit(std::uint32_t from, const Place& to, float cost) {
        bool fresh = false;
        const std::optional<std::uint32_t> index = slot_of(key_of(to), fresh);
        if (!index) {
            full = true; // The table is full: the search ends, as at its limit of steps.
            return;
        }
        Slot& slot = slots[*index];
        if (!fresh && (slot.closed != 0 || slot.cost <= cost)) {
            return;
        }
        slot.cost = cost;
        slot.height = to.height;
        slot.parent = from;
        open.push_back({cost + estimate(to.x, to.y), cost, *index});
        std::push_heap(open.begin(), open.end(), [](const Open& a, const Open& b) { return a.estimate > b.estimate; });
    }

    /** Makes `budget` steps at most. @return The steps that it made. */
    std::uint32_t advance(std::uint32_t budget) {
        static constexpr int kDx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
        static constexpr int kDy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
        const float cell = navigation.m_agent.cell;
        std::uint32_t made = 0;
        while (running && made < budget) {
            if (open.empty() || full || steps >= navigation_rules::kSearchSteps) {
                running = false;
                break;
            }
            std::pop_heap(open.begin(), open.end(), [](const Open& a, const Open& b) { return a.estimate > b.estimate; });
            const Open top = open.back();
            open.pop_back();
            Slot& slot = slots[top.slot];
            if (slot.closed != 0 || top.cost > slot.cost) {
                continue;
            }
            slot.closed = 1;
            ++made;
            ++steps;
            if (slot.key == goalKey) {
                goalSlot = top.slot;
                found = true;
                running = false;
                break;
            }
            const auto x = static_cast<std::int32_t>(slot.key >> 28);
            const auto y = static_cast<std::int32_t>((slot.key >> 4) & 0xFFFFFFU);
            const float height = slot.height;
            const float cost = slot.cost;
            Place side[4];
            bool sideFree[4];
            for (int direction = 0; direction < 8; ++direction) {
                Place next;
                if (direction < 4) {
                    sideFree[direction] = navigation.step_to(x + kDx[direction], y + kDy[direction], height, side[direction]);
                    if (!sideFree[direction]) {
                        continue;
                    }
                    next = side[direction];
                } else {
                    // A step to a cell on a diagonal needs its two side steps: a creature does not cut a corner.
                    if (!sideFree[kDx[direction] > 0 ? 0 : 1] || !sideFree[kDy[direction] > 0 ? 2 : 3]
                        || !navigation.step_to(x + kDx[direction], y + kDy[direction], height, next)) {
                        continue;
                    }
                }
                const float flat = direction < 4 ? cell : cell * kSqrt2;
                const float rise = next.height - height;
                float length = std::sqrt(flat * flat + rise * rise);
                if (next.water) {
                    length *= navigation_rules::kWaterCost;
                }
                visit(top.slot, next, cost + length);
            }
        }
        return made;
    }

    /** @return The cells of the path that the search found, from its start to its goal. */
    [[nodiscard]] std::vector<PathCell> path() const {
        std::vector<PathCell> cells;
        std::uint32_t at = goalSlot;
        for (std::size_t guard = 0; guard < kSlots; ++guard) {
            const Slot& slot = slots[at];
            cells.push_back({static_cast<std::int32_t>(slot.key >> 28), static_cast<std::int32_t>((slot.key >> 4) & 0xFFFFFFU), slot.height});
            if (slot.parent == at) {
                break;
            }
            at = slot.parent;
        }
        std::reverse(cells.begin(), cells.end());
        return cells;
    }
};

// ---- the start and the end -------------------------------------------------------------------

Navigation::Navigation(World& world, const gamedata::NavigationData* data, gamedata::Terrain* terrain, bool loaderThread)
    : m_world(world), m_data(data), m_terrain(terrain), m_threaded(loaderThread) {
    if (m_threaded) {
        m_loader = std::thread([this] { loader_main(); });
    }
}

Navigation::~Navigation() {
    {
        std::lock_guard lock(m_loaderMutex);
        m_loaderStop = true;
    }
    m_loaderWake.notify_all();
    if (m_loader.joinable()) {
        m_loader.join();
    }
}

// ---- the tiles -------------------------------------------------------------------------------

void Navigation::loader_main() {
    // This thread reads the files only: it has no access to the world.
    for (;;) {
        std::uint64_t key = 0;
        {
            std::unique_lock lock(m_loaderMutex);
            m_loaderWake.wait(lock, [this] { return m_loaderStop || !m_requests.empty(); });
            if (m_loaderStop) {
                return;
            }
            key = m_requests.front();
            m_requests.pop_front();
        }
        const auto started = std::chrono::steady_clock::now();
        std::shared_ptr<const NavTile> tile = m_data->load(static_cast<std::int32_t>(key >> 32), static_cast<std::int32_t>(key & 0xFFFFFFFFU));
        const std::chrono::nanoseconds time = std::chrono::steady_clock::now() - started;
        std::lock_guard lock(m_loaderMutex);
        m_loaded.push_back({key, std::move(tile), time});
    }
}

void Navigation::take_loaded() {
    std::vector<Loaded> loaded;
    {
        std::lock_guard lock(m_loaderMutex);
        loaded.swap(m_loaded);
    }
    for (Loaded& item : loaded) {
        insert(item.key, std::move(item.tile), item.time);
    }
}

void Navigation::insert(std::uint64_t key, std::shared_ptr<const NavTile> tile, std::chrono::nanoseconds time) {
    m_tileLoad.add(time);
    const auto held = m_tiles.find(key);
    if (held == m_tiles.end() || held->second.state != TileState::loading) {
        return; // The world removed the tile while the thread did read it.
    }
    if (tile) {
        m_counts.tileBytes += tile->bytes();
        ++m_counts.tilesLoaded;
        held->second.state = TileState::ready;
        held->second.tile = std::move(tile);
    } else {
        ++m_counts.tilesMissing;
        held->second.state = TileState::missing;
    }
    m_lastKey = ~0ULL;
    link_tile(static_cast<std::int32_t>(key >> 32), static_cast<std::int32_t>(key & 0xFFFFFFFFU), true);
}

void Navigation::want(std::int32_t tileX, std::int32_t tileY, WorldClock::time_point now) {
    if (!m_data->has_region(tileX, tileY)) {
        return;
    }
    const std::uint64_t key = tile_key(tileX, tileY);
    const auto [held, added] = m_tiles.try_emplace(key);
    held->second.wanted = now;
    if (!added) {
        return;
    }
    m_counts.tilesHeld = m_tiles.size();
    m_counts.largestTilesHeld = std::max(m_counts.largestTilesHeld, m_tiles.size());
    if (m_threaded) {
        {
            std::lock_guard lock(m_loaderMutex);
            m_requests.push_back(key);
        }
        m_loaderWake.notify_one();
        return;
    }
    const auto started = std::chrono::steady_clock::now();
    std::shared_ptr<const NavTile> tile = m_data->load(tileX, tileY);
    insert(key, std::move(tile), std::chrono::steady_clock::now() - started);
}

void Navigation::want_around(const state::WorldPosition& position, float distance, WorldClock::time_point now) {
    if (!usable(position)) {
        return;
    }
    const auto edge = static_cast<float>(NavTile::kMeters);
    const auto lowX = static_cast<std::int32_t>(std::floor((position.x - distance) / edge));
    const auto highX = static_cast<std::int32_t>(std::floor((position.x + distance) / edge));
    const auto lowY = static_cast<std::int32_t>(std::floor((position.y - distance) / edge));
    const auto highY = static_cast<std::int32_t>(std::floor((position.y + distance) / edge));
    for (std::int32_t y = lowY; y <= highY; ++y) {
        for (std::int32_t x = lowX; x <= highX; ++x) {
            want(x, y, now);
        }
    }
}

void Navigation::update_wanted() {
    const WorldClock::time_point now = m_world.clock().now();
    m_world.for_each_character([&](const Entity& character) { want_around(character.position, navigation_rules::kKeepDistance, now); });
    for (auto route = m_routes.begin(); route != m_routes.end();) {
        // A path that no creature used for some time: its creature left the world.
        if (now - route->second.used > navigation_rules::kDropAfter) {
            route = m_routes.erase(route);
            continue;
        }
        if (route->second.hasPosition) {
            want_around(route->second.position, navigation_rules::kMoverKeepDistance, now);
        }
        ++route;
    }
    for (auto held = m_tiles.begin(); held != m_tiles.end();) {
        if (held->second.state != TileState::loading && now - held->second.wanted > navigation_rules::kDropAfter) {
            if (held->second.tile) {
                m_counts.tileBytes -= std::min(m_counts.tileBytes, held->second.tile->bytes());
            }
            const std::uint64_t gone = held->first;
            held = m_tiles.erase(held);
            ++m_counts.tilesDropped;
            m_lastKey = ~0ULL;
            link_tile(static_cast<std::int32_t>(gone >> 32), static_cast<std::int32_t>(gone & 0xFFFFFFFFU), false);
        } else {
            ++held;
        }
    }
    m_counts.tilesHeld = m_tiles.size();
}

const NavTile* Navigation::tile_of(std::int32_t cellX, std::int32_t cellY) const {
    const std::uint64_t key = tile_key(floor_div(cellX, NavTile::kCells), floor_div(cellY, NavTile::kCells));
    if (key == m_lastKey) {
        return m_lastTile;
    }
    const auto held = m_tiles.find(key);
    const NavTile* tile = held != m_tiles.end() && held->second.state == TileState::ready ? held->second.tile.get() : nullptr;
    m_lastKey = key;
    m_lastTile = tile;
    return tile;
}

int Navigation::spots(std::int32_t cellX, std::int32_t cellY, NavSpot* out) const {
    const NavTile* tile = tile_of(cellX, cellY);
    if (tile == nullptr) {
        return -1;
    }
    return static_cast<int>(tile->spots(cellX - tile->x * NavTile::kCells, cellY - tile->y * NavTile::kCells, out));
}

bool Navigation::step_to(std::int32_t cellX, std::int32_t cellY, float height, Place& out) const {
    NavSpot found[NavTile::kMaxFloors];
    const int count = spots(cellX, cellY, found);
    if (count <= 0) {
        return false;
    }
    const int target = gamedata::nav_step_target(found, static_cast<std::size_t>(count), height, m_agent.step);
    if (target < 0) {
        return false;
    }
    out.x = cellX;
    out.y = cellY;
    out.floor = found[target].index;
    out.height = found[target].height;
    out.area = found[target].floor.area;
    out.water = (found[target].floor.flags & NavFloor::kShallowWater) != 0;
    return true;
}

void Navigation::link_edge(Held& low, const Held& high, int side) const {
    std::vector<AreaLink>& links = side == 0 ? low.east : low.north;
    links.clear();
    if (low.state != TileState::ready || high.state != TileState::ready) {
        return;
    }
    const NavTile& tile = *low.tile;
    const NavTile& next = *high.tile;
    NavSpot here[NavTile::kMaxFloors];
    NavSpot there[NavTile::kMaxFloors];
    AreaLink last{0, 0};
    const auto add = [&](std::uint64_t first, std::uint64_t second) {
        // The cells of an edge that follow each other have the same two areas in most cases.
        const AreaLink link{first, second};
        if (link != last) {
            links.push_back(link);
            last = link;
        }
    };
    for (std::int32_t index = 0; index < NavTile::kCells; ++index) {
        const std::size_t count = side == 0 ? tile.spots(NavTile::kCells - 1, index, here) : tile.spots(index, NavTile::kCells - 1, here);
        const std::size_t nextCount = side == 0 ? next.spots(0, index, there) : next.spots(index, 0, there);
        if (count == 0 || nextCount == 0) {
            continue;
        }
        for (std::size_t floor = 0; floor < count; ++floor) {
            if (here[floor].floor.area == 0) {
                continue;
            }
            const int target = gamedata::nav_step_target(there, nextCount, here[floor].height, m_agent.step);
            if (target >= 0 && there[target].floor.area != 0) {
                add(area_number(tile.x, tile.y, here[floor].floor.area), area_number(next.x, next.y, there[target].floor.area));
            }
        }
        for (std::size_t floor = 0; floor < nextCount; ++floor) {
            if (there[floor].floor.area == 0) {
                continue;
            }
            const int target = gamedata::nav_step_target(here, count, there[floor].height, m_agent.step);
            if (target >= 0 && here[target].floor.area != 0) {
                add(area_number(tile.x, tile.y, here[target].floor.area), area_number(next.x, next.y, there[floor].floor.area));
            }
        }
    }
    std::sort(links.begin(), links.end());
    links.erase(std::unique(links.begin(), links.end()), links.end());
}

void Navigation::link_tile(std::int32_t tileX, std::int32_t tileY, bool there) {
    const auto own = m_tiles.find(tile_key(tileX, tileY));
    const auto east = m_tiles.find(tile_key(tileX + 1, tileY));
    const auto north = m_tiles.find(tile_key(tileX, tileY + 1));
    const auto west = m_tiles.find(tile_key(tileX - 1, tileY));
    const auto south = m_tiles.find(tile_key(tileX, tileY - 1));
    if (there && own != m_tiles.end()) {
        if (east != m_tiles.end()) {
            link_edge(own->second, east->second, 0);
        }
        if (north != m_tiles.end()) {
            link_edge(own->second, north->second, 1);
        }
    }
    // The edge to the tile to the west and to the tile to the south is a list of that tile.
    if (west != m_tiles.end()) {
        if (there && own != m_tiles.end()) {
            link_edge(west->second, own->second, 0);
        } else {
            west->second.east.clear();
        }
    }
    if (south != m_tiles.end()) {
        if (there && own != m_tiles.end()) {
            link_edge(south->second, own->second, 1);
        } else {
            south->second.north.clear();
        }
    }
    m_areasCurrent = false;
}

void Navigation::link_areas() const {
    // The cost is the number of the links, not the number of the cells: a world with 50 players
    // has some thousand links, and a tile that comes or goes makes only its own four edges again.
    m_areaParent.clear();
    const auto find = [this](std::uint64_t item) {
        for (;;) {
            const auto parent = m_areaParent.find(item);
            if (parent == m_areaParent.end() || parent->second == item) {
                return item;
            }
            item = parent->second;
        }
    };
    const auto join = [&](const AreaLink& link) {
        const std::uint64_t a = find(link.first);
        const std::uint64_t b = find(link.second);
        if (a != b) {
            m_areaParent[std::max(a, b)] = std::min(a, b);
        }
    };
    for (const auto& [key, held] : m_tiles) {
        if (held.state != TileState::ready) {
            continue;
        }
        for (const AreaLink& link : held.east) {
            join(link);
        }
        for (const AreaLink& link : held.north) {
            join(link);
        }
    }
    // Each area gets the first area of its group, so that a query needs one look.
    std::vector<std::pair<std::uint64_t, std::uint64_t>> roots;
    roots.reserve(m_areaParent.size());
    for (const auto& [item, parent] : m_areaParent) {
        roots.emplace_back(item, find(item));
    }
    for (const auto& [item, root] : roots) {
        m_areaParent[item] = root;
    }
    m_areasCurrent = true;
}

std::uint64_t Navigation::area_of(const Place& place) const {
    if (!m_areasCurrent) {
        link_areas();
    }
    const std::uint64_t own = area_number(floor_div(place.x, NavTile::kCells), floor_div(place.y, NavTile::kCells), place.area);
    const auto parent = m_areaParent.find(own);
    return parent == m_areaParent.end() ? own : parent->second;
}

bool Navigation::connected(const Place& from, const Place& to) const {
    return from.area != 0 && to.area != 0 && area_of(from) == area_of(to);
}

// ---- places ----------------------------------------------------------------------------------

state::WorldPosition Navigation::center_of(const PathCell& cell) const {
    return {(static_cast<float>(cell.x) + 0.5F) * m_agent.cell, (static_cast<float>(cell.y) + 0.5F) * m_agent.cell, cell.height};
}

std::optional<Navigation::Place> Navigation::snap(const state::WorldPosition& position, float reach, float heightReach) const {
    if (!usable(position)) {
        return std::nullopt;
    }
    const float cell = m_agent.cell;
    const auto centerX = static_cast<std::int32_t>(std::floor(position.x / cell));
    const auto centerY = static_cast<std::int32_t>(std::floor(position.y / cell));
    const auto cells = static_cast<std::int32_t>(std::ceil(reach / cell));
    std::optional<Place> best;
    float nearest = std::numeric_limits<float>::max();
    NavSpot found[NavTile::kMaxFloors];
    for (std::int32_t dy = -cells; dy <= cells; ++dy) {
        for (std::int32_t dx = -cells; dx <= cells; ++dx) {
            const int count = spots(centerX + dx, centerY + dy, found);
            if (count <= 0) {
                continue;
            }
            const float awayX = (static_cast<float>(centerX + dx) + 0.5F) * cell - position.x;
            const float awayY = (static_cast<float>(centerY + dy) + 0.5F) * cell - position.y;
            const float flat = awayX * awayX + awayY * awayY;
            if (flat > (reach + cell) * (reach + cell)) {
                continue;
            }
            for (int index = 0; index < count; ++index) {
                if (found[index].floor.area == 0) {
                    continue;
                }
                const float rise = found[index].height - position.z;
                if (std::fabs(rise) > heightReach) {
                    continue;
                }
                // A floor of a different height is farther than a floor of the same height at the same distance.
                const float distance = flat + rise * rise * 4.0F;
                if (distance < nearest) {
                    nearest = distance;
                    best = Place{centerX + dx, centerY + dy, found[index].index, found[index].height, found[index].floor.area,
                                 (found[index].floor.flags & NavFloor::kShallowWater) != 0};
                }
            }
        }
    }
    return best;
}

std::optional<float> Navigation::floor_at(float x, float y, float height, bool walkableOnly) const {
    const float cell = m_agent.cell;
    const auto cellX = static_cast<std::int32_t>(std::floor(x / cell));
    const auto cellY = static_cast<std::int32_t>(std::floor(y / cell));
    const NavTile* tile = tile_of(cellX, cellY);
    if (tile == nullptr) {
        return std::nullopt;
    }
    NavSpot found[NavTile::kMaxFloors];
    const std::size_t count = tile->spots(cellX - tile->x * NavTile::kCells, cellY - tile->y * NavTile::kCells, found);
    const NavSpot* best = nullptr;
    for (int pass = walkableOnly ? 0 : 1; pass < 2 && best == nullptr; ++pass) {
        for (std::size_t index = 0; index < count; ++index) {
            if (pass == 0 && found[index].floor.area == 0) {
                continue;
            }
            if (best == nullptr || std::fabs(found[index].height - height) < std::fabs(best->height - height)) {
                best = &found[index];
            }
        }
    }
    if (best == nullptr) {
        return std::nullopt;
    }
    if ((best->floor.flags & NavFloor::kTerrain) != 0) {
        // The terrain at the exact place, not at the center of the cell: a walk on a slope has no steps.
        return tile->terrain_height(x - static_cast<float>(tile->x * NavTile::kMeters), y - static_cast<float>(tile->y * NavTile::kMeters));
    }
    return best->height;
}

std::optional<float> Navigation::ground_height(const state::WorldPosition& position) const {
    if (!std::isfinite(position.x) || !std::isfinite(position.y)) {
        return std::nullopt;
    }
    if (has_data() && usable(position)) {
        if (const std::optional<float> floor = floor_at(position.x, position.y, position.z, false)) {
            return floor;
        }
    }
    return m_terrain != nullptr ? m_terrain->height(position.x, position.y) : std::nullopt;
}

Footing Navigation::footing(const state::WorldPosition& position) const {
    if (!usable(position)) {
        return Footing::rooted;
    }
    const float cell = m_agent.cell;
    const auto cellX = static_cast<std::int32_t>(std::floor(position.x / cell));
    const auto cellY = static_cast<std::int32_t>(std::floor(position.y / cell));
    const std::int32_t tileX = floor_div(cellX, NavTile::kCells);
    const std::int32_t tileY = floor_div(cellY, NavTile::kCells);
    if (has_data() && m_data->has_region(tileX, tileY)) {
        const auto held = m_tiles.find(tile_key(tileX, tileY));
        if (held == m_tiles.end() || held->second.state == TileState::loading) {
            return Footing::unknown;
        }
        if (held->second.state == TileState::ready) {
            return snap(position, navigation_rules::kFootingReach, navigation_rules::kFootingHeight) ? Footing::walks : Footing::rooted;
        }
    }
    // No navigation data for the place: the creature walks when it is on the terrain.
    const std::optional<float> ground = m_terrain != nullptr ? m_terrain->height(position.x, position.y) : std::nullopt;
    return ground && std::fabs(position.z - *ground) <= gamedata::kTerrainTolerance ? Footing::walks : Footing::rooted;
}

bool Navigation::walk_line(const Place& from, const Place& to) const {
    const std::int32_t dx = to.x - from.x;
    const std::int32_t dy = to.y - from.y;
    const std::int32_t steps = std::max(std::abs(dx), std::abs(dy)) * 2;
    if (steps == 0) {
        return from.floor == to.floor;
    }
    std::int32_t x = from.x;
    std::int32_t y = from.y;
    float height = from.height;
    Place next;
    for (std::int32_t index = 1; index <= steps; ++index) {
        const auto nextX = from.x + static_cast<std::int32_t>(std::lround(static_cast<double>(dx) * index / steps));
        const auto nextY = from.y + static_cast<std::int32_t>(std::lround(static_cast<double>(dy) * index / steps));
        if (nextX == x && nextY == y) {
            continue;
        }
        if (nextX != x && nextY != y) {
            Place side;
            if (!step_to(nextX, y, height, side) || !step_to(x, nextY, height, side)) {
                return false;
            }
        }
        if (!step_to(nextX, nextY, height, next)) {
            return false;
        }
        x = nextX;
        y = nextY;
        height = next.height;
    }
    return next.floor == to.floor;
}

bool Navigation::sees(const state::WorldPosition& eye, const state::WorldPosition& target) const {
    ++m_counts.sightTests;
    if (!has_data() || !usable(eye) || !usable(target)) {
        return true;
    }
    const float cell = m_agent.cell;
    const float fromZ = eye.z + navigation_rules::kEyeHeight;
    const float toZ = target.z + navigation_rules::kEyeHeight;
    const float dx = target.x - eye.x;
    const float dy = target.y - eye.y;
    const auto steps = static_cast<std::int32_t>(std::ceil(std::hypot(dx, dy) / (cell * 0.5F)));
    const auto firstX = static_cast<std::int32_t>(std::floor(eye.x / cell));
    const auto firstY = static_cast<std::int32_t>(std::floor(eye.y / cell));
    const auto lastX = static_cast<std::int32_t>(std::floor(target.x / cell));
    const auto lastY = static_cast<std::int32_t>(std::floor(target.y / cell));
    std::int32_t beforeX = firstX;
    std::int32_t beforeY = firstY;
    NavSpot found[NavTile::kMaxFloors];
    for (std::int32_t index = 1; index < steps; ++index) {
        const float part = static_cast<float>(index) / static_cast<float>(steps);
        const auto cellX = static_cast<std::int32_t>(std::floor((eye.x + dx * part) / cell));
        const auto cellY = static_cast<std::int32_t>(std::floor((eye.y + dy * part) / cell));
        // The cells of the two ends have the two bodies: they stop nothing.
        if ((cellX == beforeX && cellY == beforeY) || (cellX == firstX && cellY == firstY) || (cellX == lastX && cellY == lastY)) {
            continue;
        }
        beforeX = cellX;
        beforeY = cellY;
        const int count = spots(cellX, cellY, found);
        if (count < 0) {
            continue; // No tile: nothing stops the sight there.
        }
        const float height = fromZ + (toZ - fromZ) * part;
        // The floor below the line: the sight is free in its interval.
        const NavFloor* below = nullptr;
        for (int floor = 0; floor < count; ++floor) {
            if (static_cast<float>(found[floor].floor.height) / 32.0F <= height + 0.05F) {
                below = &found[floor].floor;
            }
        }
        bool free = false;
        if (below != nullptr && below->sightLow != NavFloor::kNoSight) {
            const float base = static_cast<float>(below->height) / 32.0F;
            const float low = base + static_cast<float>(below->sightLow) / 8.0F;
            free = height >= low - 0.05F
                   && (below->sightHigh == NavFloor::kSky || height <= base + static_cast<float>(below->sightHigh) / 4.0F + 0.25F);
        }
        if (!free) {
            ++m_counts.sightStopped;
            return false;
        }
    }
    return true;
}

// ---- the steps of a creature -----------------------------------------------------------------

NavStep Navigation::terrain_step(const state::WorldPosition& from, const state::WorldPosition& goal, float distance) const {
    const float dx = goal.x - from.x;
    const float dy = goal.y - from.y;
    const float way = std::hypot(dx, dy);
    if (!std::isfinite(way) || m_terrain == nullptr) {
        return {StepKind::noGround, from};
    }
    if (way < 1.0e-4F) {
        return {StepKind::arrived, from};
    }
    state::WorldPosition next = goal;
    if (way > distance) {
        next.x = from.x + dx / way * distance;
        next.y = from.y + dy / way * distance;
    }
    const std::optional<float> ground = m_terrain->height(next.x, next.y);
    if (!ground) {
        return {StepKind::noGround, from};
    }
    next.z = *ground;
    return {StepKind::moved, next};
}

void Navigation::forget(EntityId mover) {
    if (m_searchMover == mover && m_search && m_search->running) {
        m_search->running = false;
        m_searchMover = 0;
    }
    m_routes.erase(mover);
}

void Navigation::take_back(EntityId mover) {
    const auto entry = m_routes.find(mover);
    if (entry == m_routes.end() || !entry->second.hasStep) {
        return;
    }
    Route& route = entry->second;
    route.reached = std::min(route.stepReached, route.cells.empty() ? 0 : route.cells.size() - 1);
    route.corner = route.stepCorner;
    route.position = route.stepPosition;
    route.hasStep = false;
}

NavStep Navigation::next_step(EntityId mover, const state::WorldPosition& from, const state::WorldPosition& goal, float distance) {
    if (!usable(from) || !usable(goal) || !std::isfinite(distance) || distance <= 0.0F) {
        return {StepKind::noGround, from};
    }
    const float cell = m_agent.cell;
    const std::int32_t tileX = floor_div(static_cast<std::int32_t>(std::floor(from.x / cell)), NavTile::kCells);
    const std::int32_t tileY = floor_div(static_cast<std::int32_t>(std::floor(from.y / cell)), NavTile::kCells);
    if (!has_data() || !m_data->has_region(tileX, tileY)) {
        return terrain_step(from, goal, distance);
    }
    const WorldClock::time_point now = m_world.clock().now();
    const auto held = m_tiles.find(tile_key(tileX, tileY));
    if (held == m_tiles.end() || held->second.state == TileState::loading) {
        // The tile of the creature is not there yet: the world asks for it, and the creature waits.
        want_around(from, navigation_rules::kMoverKeepDistance, now);
        ++m_counts.waits;
        return {StepKind::waiting, from};
    }
    if (held->second.state == TileState::missing) {
        return terrain_step(from, goal, distance); // A tile that is not correct: the rule of the terrain.
    }
    Route& route = m_routes[mover];
    route.used = now;
    const std::optional<Place> start = snap(from, navigation_rules::kFootingReach, navigation_rules::kSnapHeight);
    if (!start) {
        route.cells.clear();
        return {StepKind::noGround, from};
    }
    if (route.hasPosition && !route.cells.empty() && horizontal_distance(from, route.position) > navigation_rules::kOffPath) {
        route.cells.clear(); // The creature is not on its path: a command moved it.
    }
    route.position = from;
    route.hasPosition = true;
    const auto near = [](const state::WorldPosition& a, const state::WorldPosition& b) {
        return horizontal_distance(a, b) <= navigation_rules::kRepathDistance && std::fabs(a.z - b.z) <= navigation_rules::kSnapHeight;
    };
    if (route.cells.empty() || !near(goal, route.goal)) {
        if (route.noWay && now < route.noWayUntil && near(goal, route.noWayGoal)) {
            // The same goal that had no way: the navigation does not look again yet.
        } else if (route.searching && (near(goal, route.searchGoal) || m_searchMover == mover)) {
            // A search for this goal waits or runs.
        } else {
            route.noWay = false;
            const std::optional<Place> end = snap(goal, navigation_rules::kSnapDistance, navigation_rules::kSnapHeight);
            const auto no_way = [&] {
                route.noWay = true;
                route.noWayGoal = goal;
                route.noWayUntil = now + navigation_rules::kNoWayMemory;
                route.cells.clear();
                route.searching = false;
                ++m_counts.noWay;
            };
            if (!end) {
                no_way();
            } else if (walk_line(*start, *end)) {
                route.cells = {PathCell{start->x, start->y, start->height}, PathCell{end->x, end->y, end->height}};
                route.reached = 0;
                route.corner = 0;
                route.goal = goal;
                route.searching = false;
                if (m_searchMover == mover && m_search && m_search->running) {
                    m_search->running = false;
                    m_searchMover = 0;
                }
                ++m_counts.pathsDirect;
            } else if (!connected(*start, *end)) {
                no_way();
            } else {
                if (!route.searching) {
                    m_queue.push_back(mover);
                    m_counts.largestQueue = std::max(m_counts.largestQueue, m_queue.size());
                }
                route.searching = true;
                route.splice = false;
                route.searchGoal = goal;
            }
        }
    }
    if (route.cells.empty()) {
        if (route.searching) {
            ++m_counts.waits;
            return {StepKind::waiting, from};
        }
        return {StepKind::noWay, from};
    }
    return follow(route, from, distance);
}

NavStep Navigation::follow(Route& route, const state::WorldPosition& from, float distance) {
    const std::size_t last = route.cells.size() - 1;
    // While the search of the creature runs, the creature does not go past the cell at which its new path starts.
    const std::size_t limit = route.searching && route.splice ? std::min(route.junction, last) : last;
    state::WorldPosition position = from;
    float remaining = distance;
    route.stepReached = route.reached;
    route.stepCorner = route.corner;
    route.stepPosition = from;
    route.hasStep = true;
    for (int guard = 0; guard < 256 && remaining > 1.0e-4F && route.reached < limit; ++guard) {
        if (route.corner <= route.reached || route.corner > limit) {
            // The farthest cell of the path that the creature can go to on a straight line.
            route.corner = route.reached + 1;
            Place here;
            if (step_to(route.cells[route.reached].x, route.cells[route.reached].y, route.cells[route.reached].height, here)) {
                static constexpr std::size_t kAhead[] = {96, 48, 24, 12, 6, 3, 2};
                std::size_t tried = 0;
                for (const std::size_t ahead : kAhead) {
                    const std::size_t index = std::min(route.reached + ahead, limit);
                    Place there;
                    if (index <= route.reached + 1 || index == tried) {
                        continue;
                    }
                    tried = index;
                    if (step_to(route.cells[index].x, route.cells[index].y, route.cells[index].height, there) && walk_line(here, there)) {
                        route.corner = index;
                        break;
                    }
                }
            }
        }
        const state::WorldPosition target = center_of(route.cells[route.corner]);
        const float away = horizontal_distance(position, target);
        if (away <= remaining) {
            // The step ends at a corner of the path: the client moves a creature on the straight
            // line between two of its states, and a line over a corner goes through the solid there.
            position.x = target.x;
            position.y = target.y;
            position.z = target.z;
            route.reached = route.corner;
            if (away > 1.0e-3F) {
                break;
            }
            remaining -= away;
        } else {
            position.x += (target.x - position.x) / away * remaining;
            position.y += (target.y - position.y) / away * remaining;
            // The height between the two cells of the line, as the first value for the floor.
            const state::WorldPosition before = center_of(route.cells[route.reached]);
            const float whole = horizontal_distance(before, target);
            const float part = whole > 1.0e-4F ? std::clamp(horizontal_distance(before, position) / whole, 0.0F, 1.0F) : 1.0F;
            position.z = before.z + (target.z - before.z) * part;
            remaining = 0.0F;
        }
    }
    if (horizontal_distance(position, from) < 1.0e-4F) {
        if (route.reached >= last && !route.searching) {
            return {StepKind::arrived, from};
        }
        ++m_counts.waits;
        return {StepKind::waiting, from};
    }
    if (const std::optional<float> floor = floor_at(position.x, position.y, position.z, true)) {
        position.z = *floor;
    }
    route.position = position;
    return {StepKind::moved, position};
}

// ---- the searches of a tick ------------------------------------------------------------------

void Navigation::finish_search(EntityId mover, bool found, std::vector<PathCell> path) {
    const auto entry = m_routes.find(mover);
    if (entry == m_routes.end()) {
        return;
    }
    Route& route = entry->second;
    if (!route.searching) {
        return; // The creature got a straight path while its search waited.
    }
    route.searching = false;
    if (!found || path.empty()) {
        route.noWay = true;
        route.noWayGoal = route.searchGoal;
        route.noWayUntil = m_world.clock().now() + navigation_rules::kNoWayMemory;
        route.cells.clear();
        return;
    }
    if (route.splice && route.junction < route.cells.size()) {
        // The new path starts at a cell of the old path: the creature keeps the cells before it.
        route.cells.resize(route.junction + 1);
        route.cells.insert(route.cells.end(), path.begin() + 1, path.end());
    } else {
        route.cells = std::move(path);
        route.reached = 0;
        route.corner = 0;
    }
    route.splice = false;
    route.goal = route.searchGoal;
    route.noWay = false;
}

void Navigation::run_searches() {
    if (m_queue.empty() && !(m_search && m_search->running)) {
        return;
    }
    const LoadTimer timer(m_searchLoad);
    std::uint32_t budget = navigation_rules::kTickSteps;
    while (budget > 0) {
        if (!m_search || !m_search->running) {
            if (m_queue.empty()) {
                break;
            }
            const EntityId mover = m_queue.front();
            m_queue.pop_front();
            const auto entry = m_routes.find(mover);
            if (entry == m_routes.end() || !entry->second.searching) {
                continue;
            }
            Route& route = entry->second;
            // A creature that follows a path goes on while its search runs: the new path starts
            // at a cell of the old one that the creature has not reached yet.
            std::optional<Place> start;
            route.splice = false;
            if (!route.cells.empty() && route.reached < route.cells.size()) {
                route.junction = std::min(std::max(route.reached + navigation_rules::kSpliceCells, route.corner), route.cells.size() - 1);
                Place junction;
                const PathCell& cell = route.cells[route.junction];
                if (step_to(cell.x, cell.y, cell.height, junction)) {
                    start = junction;
                    route.splice = true;
                }
            }
            if (!start) {
                route.cells.clear();
                start = snap(route.position, navigation_rules::kFootingReach, navigation_rules::kSnapHeight);
            }
            const std::optional<Place> goal = snap(route.searchGoal, navigation_rules::kSnapDistance, navigation_rules::kSnapHeight);
            if (!start || !goal || !connected(*start, *goal)) {
                ++m_counts.noWay;
                finish_search(mover, false, {});
                continue;
            }
            ++m_counts.searches;
            m_searchMover = mover;
            m_search->begin(*start, *goal);
        }
        const std::uint32_t made = m_search->advance(budget);
        budget -= std::min(budget, std::max<std::uint32_t>(made, 1));
        m_counts.searchSteps += made;
        if (!m_search->running) {
            if (!m_search->found) {
                ++m_counts.searchesFailed;
            }
            const EntityId mover = m_searchMover;
            m_searchMover = 0;
            finish_search(mover, m_search->found, m_search->found ? m_search->path() : std::vector<PathCell>());
        }
    }
    m_counts.largestTickSteps = std::max(m_counts.largestTickSteps, navigation_rules::kTickSteps - budget);
}

void Navigation::tick() {
    if (!has_data()) {
        return;
    }
    m_agent = m_data->agent();
    if (!m_search) {
        // The table of the searches (8 MB), one time: not in a tick with a fight.
        m_search = std::make_unique<Search>(*this);
    }
    take_loaded();
    const WorldClock::time_point now = m_world.clock().now();
    if (!m_lastWant || now - *m_lastWant >= navigation_rules::kWantInterval) {
        m_lastWant = now;
        update_wanted();
    }
    run_searches();
}

PathReport Navigation::find_path(const state::WorldPosition& from, const state::WorldPosition& to) {
    PathReport report;
    if (!has_data()) {
        report.cause = "the backend has no navigation data";
        return report;
    }
    if (!usable(from) || !usable(to)) {
        report.cause = "a position is not a position of the world";
        return report;
    }
    m_agent = m_data->agent();
    const auto started = std::chrono::steady_clock::now();
    // The tiles of the rectangle of the two places, with a border: this thread reads them now.
    const WorldClock::time_point now = m_world.clock().now();
    const auto edge = static_cast<float>(NavTile::kMeters);
    const float border = navigation_rules::kKeepDistance;
    const auto lowX = static_cast<std::int32_t>(std::floor((std::min(from.x, to.x) - border) / edge));
    const auto highX = static_cast<std::int32_t>(std::floor((std::max(from.x, to.x) + border) / edge));
    const auto lowY = static_cast<std::int32_t>(std::floor((std::min(from.y, to.y) - border) / edge));
    const auto highY = static_cast<std::int32_t>(std::floor((std::max(from.y, to.y) + border) / edge));
    if ((highX - lowX + 1) * (highY - lowY + 1) > 400) {
        report.cause = "the two places are too far from each other for this command";
        return report;
    }
    for (std::int32_t y = lowY; y <= highY; ++y) {
        for (std::int32_t x = lowX; x <= highX; ++x) {
            if (!m_data->has_region(x, y)) {
                continue;
            }
            const std::uint64_t key = tile_key(x, y);
            const auto [held, added] = m_tiles.try_emplace(key);
            held->second.wanted = now;
            if (added || held->second.state == TileState::loading) {
                const auto readStart = std::chrono::steady_clock::now();
                insert(key, m_data->load(x, y), std::chrono::steady_clock::now() - readStart);
            }
        }
    }
    m_counts.tilesHeld = m_tiles.size();
    m_counts.largestTilesHeld = std::max(m_counts.largestTilesHeld, m_tiles.size());
    const std::optional<Place> start = snap(from, navigation_rules::kSnapDistance, navigation_rules::kSnapHeight);
    const std::optional<Place> goal = snap(to, navigation_rules::kSnapDistance, navigation_rules::kSnapHeight);
    std::vector<PathCell> cells;
    if (!start) {
        report.cause = "the first place is on no free floor of the data";
    } else if (!goal) {
        report.cause = "the second place is on no free floor of the data";
    } else if (*start == *goal || walk_line(*start, *goal)) {
        report.direct = true;
        cells = {PathCell{start->x, start->y, start->height}, PathCell{goal->x, goal->y, goal->height}};
    } else if (!connected(*start, *goal)) {
        report.cause = "the two places have no way between them";
    } else {
        Search search(*this);
        search.begin(*start, *goal);
        while (search.running) {
            (void)search.advance(navigation_rules::kSearchSteps);
        }
        report.steps = search.steps;
        if (search.found) {
            cells = search.path();
        } else {
            report.cause = "the search found no path in its limit";
        }
    }
    if (!cells.empty()) {
        report.found = true;
        report.cells = cells.size();
        // The corners: from each corner, the farthest cell of the path on a straight line that is free.
        std::size_t at = 0;
        report.corners.push_back(center_of(cells[0]));
        while (at + 1 < cells.size()) {
            std::size_t next = at + 1;
            Place here;
            if (step_to(cells[at].x, cells[at].y, cells[at].height, here)) {
                for (std::size_t probe = std::min(cells.size() - 1, at + 400); probe > at + 1; --probe) {
                    Place there;
                    if (step_to(cells[probe].x, cells[probe].y, cells[probe].height, there) && walk_line(here, there)) {
                        next = probe;
                        break;
                    }
                }
            }
            report.length += std::hypot(horizontal_distance(center_of(cells[at]), center_of(cells[next])), cells[next].height - cells[at].height);
            report.corners.push_back(center_of(cells[next]));
            at = next;
        }
    }
    report.time = std::chrono::steady_clock::now() - started;
    return report;
}

} // namespace soulwarden::backend::world

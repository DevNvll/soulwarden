#include "gamedata/trigger_data.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "gamedata/loot_tables.h"
#include "gamedata/poi_data.h"

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

constexpr std::uint64_t kFormat = 1;
/** The rounding error that an axis of a volume can have (its length against 1, two axes against a right angle). */
constexpr float kAxisTolerance = 0.001F;
/** Stage QG3: the cell size of the grid of the place view (meters). */
constexpr float kCell = 256.0F;

std::uint64_t cell_key(std::int64_t x, std::int64_t y) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32U) | static_cast<std::uint32_t>(y);
}

std::int64_t cell_of(float value) {
    return static_cast<std::int64_t>(std::floor(value / kCell));
}

/**
 * Code review of stage QG3, finding QG3R-R3: the largest count of cells of one volume in the grid (a square of 32 km: twice the open world),
 * and of all the volumes of one file. A volume above it is refused at the load (a test volume of `set` goes into the list of the volumes
 * that each lookup tests): a small file cannot take all the memory at the start.
 */
constexpr double kMaxVolumeCells = 16384.0;
constexpr double kMaxGridEntries = 2000000.0;
/** The largest count of the volumes that the grid cannot hold (QG3R-R2: a turned volume with no height limit). */
constexpr std::size_t kMaxUnboundedVolumes = 64;

/**
 * Code review of stage QG3, finding QG3R-R2: a volume with no height limit (ignoreHeight) whose local height axis is not vertical has no
 * horizontal limit along that axis: the sphere of `reach` does not hold it, and the place view tests it at each lookup.
 */
bool unbounded(const TriggerVolume& volume) {
    return volume.ignoreHeight && (volume.axes[6] != 0.0F || volume.axes[7] != 0.0F);
}

/** @return The radius of a sphere around the center that holds the volume (the cells of the grid). */
float reach(const TriggerVolume& volume);

/** @return The count of the cells of the grid that the sphere of a volume covers (QG3R-R3: in double, before any integer conversion). */
double cell_count(const TriggerVolume& volume) {
    const double radius = reach(volume);
    const double side = std::floor((static_cast<double>(volume.center[0]) + radius) / kCell) - std::floor((static_cast<double>(volume.center[0]) - radius) / kCell) + 1.0;
    const double depth = std::floor((static_cast<double>(volume.center[1]) + radius) / kCell) - std::floor((static_cast<double>(volume.center[1]) - radius) / kCell) + 1.0;
    const double cells = side * depth;
    return std::isfinite(cells) ? cells : kMaxVolumeCells * 2.0;
}

float reach(const TriggerVolume& volume) {
    switch (volume.shape) {
    case TriggerShape::aabb:
    case TriggerShape::box:
        return std::sqrt(volume.half[0] * volume.half[0] + volume.half[1] * volume.half[1] + volume.half[2] * volume.half[2]);
    case TriggerShape::sphere:
        return volume.radius;
    case TriggerShape::cylinder:
    case TriggerShape::capsule:
        return std::sqrt(volume.radius * volume.radius + volume.height * volume.height / 4.0F);
    }
    return 0.0F;
}

template <std::size_t N>
bool read_floats(const json& value, std::array<float, N>& out) {
    if (!value.is_array() || value.size() != N) {
        return false;
    }
    for (std::size_t index = 0; index < N; ++index) {
        if (!value[index].is_number() || !std::isfinite(value[index].get<double>())) {
            return false;
        }
        out[index] = static_cast<float>(value[index].get<double>());
    }
    return true;
}

bool read_size(const json& entry, const char* name, float& out) {
    if (!entry.contains(name) || !entry[name].is_number() || !std::isfinite(entry[name].get<double>()) || entry[name].get<double>() < 0.0) {
        return false;
    }
    out = static_cast<float>(entry[name].get<double>());
    return true;
}

} // namespace

bool TriggerVolume::contains(float x, float y, float z) const {
    const float dx = x - center[0];
    const float dy = y - center[1];
    const float dz = z - center[2];
    // The point on the axes of the volume (unit axes: a dot product for each one).
    const float lx = dx * axes[0] + dy * axes[1] + dz * axes[2];
    const float ly = dx * axes[3] + dy * axes[4] + dz * axes[5];
    const float lz = dx * axes[6] + dy * axes[7] + dz * axes[8];
    switch (shape) {
    case TriggerShape::aabb:
    case TriggerShape::box:
        return std::fabs(lx) <= half[0] && std::fabs(ly) <= half[1] && (ignoreHeight || std::fabs(lz) <= half[2]);
    case TriggerShape::sphere:
        return ignoreHeight ? lx * lx + ly * ly <= radius * radius : lx * lx + ly * ly + lz * lz <= radius * radius;
    case TriggerShape::cylinder:
    case TriggerShape::capsule:
        // A capsule as its cylinder of the full height (the rule of the collision tool, nav_slices.py).
        return lx * lx + ly * ly <= radius * radius && (ignoreHeight || std::fabs(lz) <= height / 2.0F);
    }
    return false;
}

bool TriggerData::fail(std::string cause) {
    m_volumes.clear();
    m_byTag.clear();
    m_elsewhere.clear();
    m_cells.clear();
    m_unbounded.clear();
    m_otherWorld = 0;
    m_error = std::move(cause);
    return false;
}

bool TriggerData::load(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return fail("the file is not there");
    }
    std::ostringstream text;
    text << input.rdbuf();
    return load_text(text.str());
}

bool TriggerData::load_text(const std::string& text) {
    m_volumes.clear();
    m_byTag.clear();
    m_elsewhere.clear();
    m_otherWorld = 0;
    m_error.clear();
    const json document = json::parse(text, nullptr, false);
    if (!document.is_object() || !document.contains("format") || !document["format"].is_number_unsigned()
        || document["format"].get<std::uint64_t>() != kFormat) {
        return fail("the file is not a JSON object of the format 1");
    }
    if (!document.contains("volumes") || !document["volumes"].is_array()) {
        return fail("the file has no list volumes");
    }
    // Code review of stage QG3, finding QG3R-R1: the world of the file (the member world at its root: the trigger tool of before stage QG3
    // wrote it) is the world of each volume with no world of its own. No member: the open world.
    if (document.contains("world") && !document["world"].is_string()) {
        return fail("the member world of the file is not a text");
    }
    const std::string fileWorld = document.contains("world") ? document["world"].get<std::string>() : std::string(kOpenWorld);
    std::vector<TriggerVolume> volumes;
    double entries = 0.0;
    std::size_t unboundedCount = 0;
    for (const json& entry : document["volumes"]) {
        if (!entry.is_object() || !entry.contains("tag") || !entry["tag"].is_string() || !entry.contains("shape") || !entry["shape"].is_string()) {
            return fail("a volume does not have the members tag and shape");
        }
        TriggerVolume volume;
        volume.tag = lower_text(entry["tag"].get<std::string>());
        // Stage QG3: a volume of another world (an instance) is never tested in the open world.
        const bool ownWorld = entry.contains("world");
        if ((ownWorld && (!entry["world"].is_string() || entry["world"].get<std::string>() != kOpenWorld)) || (!ownWorld && fileWorld != kOpenWorld)) {
            ++m_otherWorld;
            const std::string world = !ownWorld ? fileWorld : entry["world"].is_string() ? entry["world"].get<std::string>() : std::string("?");
            std::vector<std::string>& worlds = m_elsewhere[volume.tag];
            if (std::find(worlds.begin(), worlds.end(), world) == worlds.end()) {
                worlds.push_back(world);
            }
            continue;
        }
        const std::string shape = entry["shape"].get<std::string>();
        bool sized = false;
        if (shape == "aabb" || shape == "box") {
            volume.shape = shape == "aabb" ? TriggerShape::aabb : TriggerShape::box;
            sized = entry.contains("half") && read_floats(entry["half"], volume.half) && volume.half[0] >= 0.0F && volume.half[1] >= 0.0F
                    && volume.half[2] >= 0.0F;
        } else if (shape == "sphere") {
            volume.shape = TriggerShape::sphere;
            sized = read_size(entry, "radius", volume.radius);
        } else if (shape == "cylinder" || shape == "capsule") {
            volume.shape = shape == "cylinder" ? TriggerShape::cylinder : TriggerShape::capsule;
            sized = read_size(entry, "radius", volume.radius) && read_size(entry, "height", volume.height);
        } else {
            return fail("the volume " + volume.tag + " has the shape " + shape + ", which the backend does not know");
        }
        if (!sized || !entry.contains("center") || !read_floats(entry["center"], volume.center) || !entry.contains("axes")
            || !read_floats(entry["axes"], volume.axes)) {
            return fail("the volume " + volume.tag + " does not have a center, axes, and the sizes of its shape");
        }
        // The axes are three unit vectors at right angles (code review of stage 1.8b, finding QR8B-R12: zero axes put each position at the
        // center). The tool writes them from the rotation of the entity: the common file has an error below 1e-6.
        for (std::size_t first = 0; first < 3; ++first) {
            const float* axis = &volume.axes[first * 3];
            if (std::fabs(std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]) - 1.0F) > kAxisTolerance) {
                return fail("the volume " + volume.tag + " has an axis that is not a unit vector");
            }
            for (std::size_t second = first + 1; second < 3; ++second) {
                const float* other = &volume.axes[second * 3];
                if (std::fabs(axis[0] * other[0] + axis[1] * other[1] + axis[2] * other[2]) > kAxisTolerance) {
                    return fail("the volume " + volume.tag + " has two axes that are not at right angles");
                }
            }
        }
        volume.ignoreHeight = entry.contains("ignoreHeight") && entry["ignoreHeight"].is_boolean() && entry["ignoreHeight"].get<bool>();
        if (entry.contains("source") && entry["source"].is_string()) {
            volume.source = entry["source"].get<std::string>();
        }
        if (entry.contains("via") && entry["via"].is_string()) {
            volume.via = entry["via"].get<std::string>();
        }
        // Code review of stage QG3, findings QG3R-R2 and QG3R-R3: the cells of the grid of the volume, before the grid (a bound for one
        // volume and for the file); a volume with no horizontal limit goes into a short list that each lookup tests.
        if (unbounded(volume)) {
            if (++unboundedCount > kMaxUnboundedVolumes) {
                return fail("the file has more than " + std::to_string(kMaxUnboundedVolumes) + " turned volumes with no height limit");
            }
        } else {
            const double cells = cell_count(volume);
            entries += cells;
            if (cells > kMaxVolumeCells || entries > kMaxGridEntries) {
                return fail("the volume " + volume.tag + " covers too many cells of the grid of the place view (" + std::to_string(cells) + ")");
            }
        }
        volumes.push_back(std::move(volume));
    }
    // Stage QG3: the volumes of the other worlds (the instances): their tags and worlds, for the cause of a task that waits.
    if (document.contains("elsewhere")) {
        if (!document["elsewhere"].is_array()) {
            return fail("the member elsewhere is not a list");
        }
        for (const json& entry : document["elsewhere"]) {
            if (!entry.is_object() || !entry.contains("tag") || !entry["tag"].is_string() || !entry.contains("world") || !entry["world"].is_string()) {
                return fail("an entry of elsewhere does not have a tag and a world");
            }
            std::vector<std::string>& worlds = m_elsewhere[lower_text(entry["tag"].get<std::string>())];
            const std::string world = entry["world"].get<std::string>();
            if (std::find(worlds.begin(), worlds.end(), world) == worlds.end()) {
                worlds.push_back(world);
            }
        }
    }
    set(std::move(volumes));
    return true;
}

void TriggerData::set(std::vector<TriggerVolume> volumes) {
    m_volumes = std::move(volumes);
    index();
}

void TriggerData::index() {
    m_byTag.clear();
    m_cells.clear();
    m_unbounded.clear();
    for (std::size_t index = 0; index < m_volumes.size(); ++index) {
        const TriggerVolume& volume = m_volumes[index];
        m_byTag[volume.tag].push_back(static_cast<std::uint32_t>(index));
        // QG3R-R2 and QG3R-R3: a volume that the grid cannot hold (no horizontal limit, or too many cells: only `set` of a test can give
        // the second) is in the list that each lookup tests.
        if (unbounded(volume) || cell_count(volume) > kMaxVolumeCells) {
            m_unbounded.push_back(static_cast<std::uint32_t>(index));
            continue;
        }
        const float radius = reach(volume);
        for (std::int64_t x = cell_of(volume.center[0] - radius); x <= cell_of(volume.center[0] + radius); ++x) {
            for (std::int64_t y = cell_of(volume.center[1] - radius); y <= cell_of(volume.center[1] + radius); ++y) {
                m_cells[cell_key(x, y)].push_back(static_cast<std::uint32_t>(index));
            }
        }
    }
}

const std::vector<std::string>* TriggerData::elsewhere(std::string_view tag) const {
    const auto found = m_elsewhere.find(std::string(tag));
    return found != m_elsewhere.end() ? &found->second : nullptr;
}

std::vector<const TriggerVolume*> TriggerData::at(float x, float y, float z) const {
    std::vector<const TriggerVolume*> result;
    for (const std::uint32_t index : m_unbounded) {
        if (m_volumes[index].contains(x, y, z)) {
            result.push_back(&m_volumes[index]);
        }
    }
    if (!std::isfinite(x) || !std::isfinite(y) || std::fabs(x) > 1.0e9F || std::fabs(y) > 1.0e9F) {
        return result;
    }
    const auto found = m_cells.find(cell_key(cell_of(x), cell_of(y)));
    if (found == m_cells.end()) {
        return result;
    }
    for (const std::uint32_t index : found->second) {
        if (m_volumes[index].contains(x, y, z)) {
            result.push_back(&m_volumes[index]);
        }
    }
    return result;
}

bool TriggerData::knows(std::string_view tag) const {
    return m_byTag.count(std::string(tag)) != 0;
}

bool TriggerData::inside(std::string_view tag, float x, float y, float z, std::uint64_t* visits) const {
    const auto found = m_byTag.find(std::string(tag));
    if (found == m_byTag.end()) {
        return false;
    }
    for (const std::uint32_t index : found->second) {
        if (visits != nullptr) {
            ++*visits;
        }
        if (m_volumes[index].contains(x, y, z)) {
            return true;
        }
    }
    return false;
}

} // namespace soulwarden::backend::gamedata

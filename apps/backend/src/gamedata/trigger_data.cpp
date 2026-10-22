#include "gamedata/trigger_data.h"

#include <cmath>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "gamedata/loot_tables.h"

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

constexpr std::uint64_t kFormat = 1;
/** The rounding error that an axis of a volume can have (its length against 1, two axes against a right angle). */
constexpr float kAxisTolerance = 0.001F;

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
    m_error.clear();
    const json document = json::parse(text, nullptr, false);
    if (!document.is_object() || !document.contains("format") || !document["format"].is_number_unsigned()
        || document["format"].get<std::uint64_t>() != kFormat) {
        return fail("the file is not a JSON object of the format 1");
    }
    if (!document.contains("volumes") || !document["volumes"].is_array()) {
        return fail("the file has no list volumes");
    }
    std::vector<TriggerVolume> volumes;
    for (const json& entry : document["volumes"]) {
        if (!entry.is_object() || !entry.contains("tag") || !entry["tag"].is_string() || !entry.contains("shape") || !entry["shape"].is_string()) {
            return fail("a volume does not have the members tag and shape");
        }
        TriggerVolume volume;
        volume.tag = lower_text(entry["tag"].get<std::string>());
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
        volumes.push_back(std::move(volume));
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
    for (std::size_t index = 0; index < m_volumes.size(); ++index) {
        m_byTag[m_volumes[index].tag].push_back(static_cast<std::uint32_t>(index));
    }
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

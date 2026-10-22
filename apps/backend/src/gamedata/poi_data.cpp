#include "gamedata/poi_data.h"

#include <cmath>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "gamedata/loot_tables.h"

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

constexpr std::uint64_t kFormat = 1;

bool finite_number(const json& value) {
    return value.is_number() && std::isfinite(value.get<double>());
}

} // namespace

bool Poi::contains(float x, float y) const {
    if (shape.size() < 3 || x < minX || x > maxX || y < minY || y > maxY) {
        return false;
    }
    bool inside = false;
    for (std::size_t index = 0, previous = shape.size() - 1; index < shape.size(); previous = index++) {
        const auto& [x1, y1] = shape[index];
        const auto& [x2, y2] = shape[previous];
        if ((y1 > y) != (y2 > y) && x < (x2 - x1) * (y - y1) / (y2 - y1) + x1) {
            inside = !inside;
        }
    }
    return inside;
}

bool PoiData::fail(std::string cause) {
    *this = PoiData{};
    m_error = std::move(cause);
    return false;
}

bool PoiData::load(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return fail("the file is not there");
    }
    std::ostringstream text;
    text << input.rdbuf();
    return load_text(text.str());
}

bool PoiData::load_text(const std::string& text) {
    *this = PoiData{};
    const json document = json::parse(text, nullptr, false);
    if (!document.is_object() || !document.contains("format") || !document["format"].is_number_unsigned()
        || document["format"].get<std::uint64_t>() != kFormat) {
        return fail("the file is not a JSON object of the format 1");
    }
    if (!document.contains("pois") || !document["pois"].is_array()) {
        return fail("the file has no list pois");
    }
    for (const json& entry : document["pois"]) {
        if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_number_unsigned() || !entry.contains("shape")
            || !entry["shape"].is_array() || entry["shape"].size() < 3 || !entry.contains("center") || !entry["center"].is_array()
            || entry["center"].size() != 3) {
            return fail("a point of interest does not have an id, a center, and a shape of 3 points or more");
        }
        Poi poi;
        poi.id = static_cast<std::uint32_t>(entry["id"].get<std::uint64_t>());
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (!finite_number(entry["center"][axis])) {
                return fail("the point of interest " + std::to_string(poi.id) + " has a center that is not finite");
            }
            poi.center[axis] = entry["center"][axis].get<float>();
        }
        if (entry.contains("radius") && finite_number(entry["radius"])) {
            poi.radius = entry["radius"].get<float>();
        }
        if (entry.contains("territory") && entry["territory"].is_number_unsigned() && entry["territory"].get<std::uint64_t>() <= 65535) {
            poi.territory = static_cast<std::uint16_t>(entry["territory"].get<std::uint64_t>());
        }
        if (entry.contains("tags") && entry["tags"].is_array()) {
            for (const json& tag : entry["tags"]) {
                if (tag.is_string() && !tag.get<std::string>().empty()) {
                    poi.tags.push_back(lower_text(tag.get<std::string>()));
                }
            }
        }
        for (const json& point : entry["shape"]) {
            if (!point.is_array() || point.size() < 2 || !finite_number(point[0]) || !finite_number(point[1])) {
                return fail("the point of interest " + std::to_string(poi.id) + " has a shape point that is not two finite numbers");
            }
            poi.shape.push_back({point[0].get<float>(), point[1].get<float>()});
        }
        poi.minX = poi.maxX = poi.shape.front()[0];
        poi.minY = poi.maxY = poi.shape.front()[1];
        for (const auto& [x, y] : poi.shape) {
            poi.minX = std::min(poi.minX, x);
            poi.maxX = std::max(poi.maxX, x);
            poi.minY = std::min(poi.minY, y);
            poi.maxY = std::max(poi.maxY, y);
        }
        if (!m_byId.emplace(poi.id, static_cast<std::uint32_t>(m_pois.size())).second) {
            return fail("two points of interest have the id " + std::to_string(poi.id));
        }
        for (const std::string& tag : poi.tags) {
            m_byTag[tag].push_back(static_cast<std::uint32_t>(m_pois.size()));
        }
        m_pois.push_back(std::move(poi));
    }
    m_loaded = true;
    return true;
}

const std::vector<std::uint32_t>* PoiData::with_tag(std::string_view tag) const {
    const auto found = m_byTag.find(std::string(tag));
    return found != m_byTag.end() ? &found->second : nullptr;
}

const Poi* PoiData::poi(std::uint32_t id) const {
    const auto found = m_byId.find(id);
    return found != m_byId.end() ? &m_pois[found->second] : nullptr;
}

bool PoiData::inside(const std::vector<std::string>& tags, std::uint16_t territory, float x, float y, std::uint64_t* visited) const {
    for (const std::string& tag : tags) {
        const std::vector<std::uint32_t>* list = with_tag(tag);
        for (std::size_t position = 0; list != nullptr && position < list->size(); ++position) {
            const Poi& poi = m_pois[(*list)[position]];
            if (territory != 0 && poi.territory != territory) {
                continue;
            }
            if (visited != nullptr) {
                ++*visited;
            }
            if (poi.contains(x, y)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace soulwarden::backend::gamedata

#include "gamedata/poi_data.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "gamedata/loot_tables.h"

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

/** Format 1 (stage 1.8c): the points of interest. Format 2 (stage QG1): and the bounds of the territories. */
constexpr std::uint64_t kLastFormat = 2;
/** Stage QG3: the cell size of the grid of the place view (meters). */
constexpr float kCell = 256.0F;
/** Stage QG3: the cause of a tag that no row of the territory tables has (the tool lists only the tags of the rows). */
constexpr const char* kNoRow = "no row of the territory tables has the tag";

std::uint64_t cell_key(std::int64_t x, std::int64_t y) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32U) | static_cast<std::uint32_t>(y);
}

std::int64_t cell_of(float value) {
    return static_cast<std::int64_t>(std::floor(value / kCell));
}

bool finite_number(const json& value) {
    return value.is_number() && std::isfinite(value.get<double>());
}

/** Reads the shape of an entry and sets its bounds. @return False when a point is not two finite numbers. */
bool read_shape(const json& points, Poi& poi) {
    for (const json& point : points) {
        if (!point.is_array() || point.size() < 2 || !finite_number(point[0]) || !finite_number(point[1])) {
            return false;
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
    return true;
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
        || document["format"].get<std::uint64_t>() == 0 || document["format"].get<std::uint64_t>() > kLastFormat) {
        return fail("the file is not a JSON object of the format 1 or 2");
    }
    const std::uint64_t format = document["format"].get<std::uint64_t>();
    if (!document.contains("pois") || !document["pois"].is_array()) {
        return fail("the file has no list pois");
    }
    for (const json& entry : document["pois"]) {
        if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_number_unsigned() || !entry.contains("shape")
            || !entry["shape"].is_array() || entry["shape"].size() < 3 || !entry.contains("center") || !entry["center"].is_array()
            || entry["center"].size() != 3) {
            return fail("a point of interest does not have an id, a center, and a shape of 3 points or more");
        }
        // Stage QG3: a shape of another world (an instance) is never tested in the open world.
        if (entry.contains("world") && (!entry["world"].is_string() || entry["world"].get<std::string>() != kOpenWorld)) {
            ++m_otherWorld;
            continue;
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
        if (!read_shape(entry["shape"], poi)) {
            return fail("the point of interest " + std::to_string(poi.id) + " has a shape point that is not two finite numbers");
        }
        poi.area = entry.contains("kind") && entry["kind"] == "area";
        if (entry.contains("source") && entry["source"].is_string()) {
            poi.source = entry["source"].get<std::string>();
        }
        if (!m_byId.emplace(poi.id, static_cast<std::uint32_t>(m_pois.size())).second) {
            return fail("two points of interest have the id " + std::to_string(poi.id));
        }
        for (const std::string& tag : poi.tags) {
            m_byTag[tag].push_back(static_cast<std::uint32_t>(m_pois.size()));
        }
        for (std::int64_t x = cell_of(poi.minX); x <= cell_of(poi.maxX); ++x) {
            for (std::int64_t y = cell_of(poi.minY); y <= cell_of(poi.maxY); ++y) {
                m_cells[cell_key(x, y)].push_back(static_cast<std::uint32_t>(m_pois.size()));
            }
        }
        m_pois.push_back(std::move(poi));
    }
    // Stage QG3: the tags of the territory tables with no shape and their cause (a tag of a shape is not one of them).
    if (document.contains("unshaped")) {
        if (!document["unshaped"].is_array()) {
            return fail("the member unshaped is not a list");
        }
        m_hasUnshaped = true;
        for (const json& entry : document["unshaped"]) {
            if (!entry.is_object() || !entry.contains("tag") || !entry["tag"].is_string() || !entry.contains("cause") || !entry["cause"].is_string()) {
                return fail("an entry of unshaped does not have a tag and a cause");
            }
            const std::string tag = lower_text(entry["tag"].get<std::string>());
            if (m_byTag.count(tag) == 0) {
                m_unshaped[tag] = entry["cause"].get<std::string>();
            }
        }
    }
    // Stage QG1 (format 2): the bounds of the territories (objects/worldprisms/bounds/territory of the game data), each with its territory id.
    if (format >= 2) {
        if (!document.contains("territories") || !document["territories"].is_array()) {
            return fail("the file of the format 2 has no list territories");
        }
        for (const json& entry : document["territories"]) {
            if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_number_unsigned() || entry["id"].get<std::uint64_t>() == 0
                || entry["id"].get<std::uint64_t>() > 65535 || !entry.contains("shape") || !entry["shape"].is_array() || entry["shape"].size() < 3) {
                return fail("a territory does not have an id of 1 to 65535 and a shape of 3 points or more");
            }
            Poi bounds;
            bounds.id = static_cast<std::uint32_t>(entry["id"].get<std::uint64_t>());
            bounds.territory = static_cast<std::uint16_t>(bounds.id);
            if (!read_shape(entry["shape"], bounds)) {
                return fail("the territory " + std::to_string(bounds.id) + " has a shape point that is not two finite numbers");
            }
            if (std::any_of(m_territories.begin(), m_territories.end(), [&](const Poi& other) { return other.id == bounds.id; })) {
                return fail("two bounds of the territory " + std::to_string(bounds.id));
            }
            m_territories.push_back(std::move(bounds));
        }
    }
    m_loaded = true;
    return true;
}

std::uint16_t PoiData::territory_at(float x, float y) const {
    for (const Poi& bounds : m_territories) {
        if (bounds.contains(x, y)) {
            return bounds.territory;
        }
    }
    return 0;
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

std::string PoiData::cause(std::string_view tag) const {
    if (!m_hasUnshaped || m_byTag.count(std::string(tag)) != 0) {
        return {};
    }
    const auto found = m_unshaped.find(std::string(tag));
    return found != m_unshaped.end() ? found->second : kNoRow;
}

std::vector<const Poi*> PoiData::at(float x, float y) const {
    std::vector<const Poi*> result;
    const auto found = m_cells.find(cell_key(cell_of(x), cell_of(y)));
    if (found == m_cells.end()) {
        return result;
    }
    for (const std::uint32_t index : found->second) {
        if (m_pois[index].contains(x, y)) {
            result.push_back(&m_pois[index]);
        }
    }
    return result;
}

bool PoiData::knows(const std::vector<std::string>& tags) const {
    if (tags.empty()) {
        return false;
    }
    return std::all_of(tags.begin(), tags.end(), [this](const std::string& tag) {
        const std::vector<std::uint32_t>* list = with_tag(tag);
        return list != nullptr && !list->empty();
    });
}

} // namespace soulwarden::backend::gamedata

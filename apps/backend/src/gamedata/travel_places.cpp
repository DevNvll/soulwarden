#include "gamedata/travel_places.h"

#include <cmath>
#include <fstream>
#include <set>

#include <nlohmann/json.hpp>

#include "world/position_index.h"

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

/** The form of the file that this code reads (tools/gamedata/travel_places.py). */
constexpr std::uint64_t kFormat = 1;
/** Largest number of characters of the id of a place. */
constexpr std::size_t kMaxIdLength = 64;

std::string lower_ascii(std::string_view text) {
    std::string lower(text);
    for (char& letter : lower) {
        if (letter >= 'A' && letter <= 'Z') {
            letter = static_cast<char>(letter + ('a' - 'A'));
        }
    }
    return lower;
}

/** @return True for an id that a command and a URL query can have with no encoding. */
bool valid_id(const std::string& id) {
    if (id.empty() || id.size() > kMaxIdLength) {
        return false;
    }
    for (const char letter : id) {
        const bool allowed = (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z')
                             || (letter >= '0' && letter <= '9') || letter == '_';
        if (!allowed) {
            return false;
        }
    }
    return true;
}

bool read_text(const json& parent, const char* key, std::string& text) {
    if (!parent.is_object() || !parent.contains(key) || !parent[key].is_string()) {
        return false;
    }
    text = parent[key].get<std::string>();
    return true;
}

bool read_place(const json& entry, TravelPlace& place) {
    if (!read_text(entry, "id", place.id) || !read_text(entry, "kind", place.kind) || !read_text(entry, "name", place.name)
        || !read_text(entry, "territory", place.territory) || !entry.contains("territoryId")
        || !entry["territoryId"].is_number_unsigned() || entry["territoryId"].get<std::uint64_t>() > 0xFFFFFFFFULL
        || !entry.contains("position") || !entry["position"].is_array() || entry["position"].size() != 3) {
        return false;
    }
    place.territoryId = entry["territoryId"].get<std::uint32_t>();
    float* const values[] = {&place.position.x, &place.position.y, &place.position.z};
    for (std::size_t index = 0; index < 3; ++index) {
        const json& value = entry["position"][index];
        if (!value.is_number()) {
            return false;
        }
        *values[index] = value.get<float>();
    }
    return true;
}

} // namespace

bool TravelPlaces::load(const std::string& path) {
    m_places.clear();
    m_byId.clear();
    m_territories = 0;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        m_error = "the file is not there";
        return false;
    }
    const json document = json::parse(input, nullptr, false);
    if (!document.is_object()) {
        m_error = "the file is not a JSON object";
        return false;
    }
    if (!document.contains("format") || !document["format"].is_number_unsigned()
        || document["format"].get<std::uint64_t>() != kFormat) {
        m_error = "the file does not have the format 1";
        return false;
    }
    if (!document.contains("places") || !document["places"].is_array()) {
        m_error = "the file does not have the list places";
        return false;
    }
    std::vector<TravelPlace> places;
    places.reserve(document["places"].size());
    for (const json& entry : document["places"]) {
        if (!read_place(entry, places.emplace_back())) {
            m_error = "place " + std::to_string(places.size() - 1) + " does not have each member";
            return false;
        }
    }
    // The count of the tool: a file that lost a part of its list is not complete.
    if (document.contains("counts")) {
        const json& counts = document["counts"];
        if (!counts.is_object() || !counts.contains("places") || !counts["places"].is_number_unsigned()
            || counts["places"].get<std::uint64_t>() != places.size()) {
            m_error = "the list does not have the count of the file";
            return false;
        }
    }
    return set(std::move(places));
}

bool TravelPlaces::set(std::vector<TravelPlace> places) {
    m_places.clear();
    m_byId.clear();
    m_territories = 0;
    if (places.empty()) {
        m_error = "the data has no place";
        return false;
    }
    std::unordered_map<std::string, std::size_t> byId;
    std::set<std::string> territories;
    for (std::size_t index = 0; index < places.size(); ++index) {
        const TravelPlace& place = places[index];
        if (!valid_id(place.id)) {
            m_error = "place " + std::to_string(index) + " has no id, or an id with a character that is not a letter, a digit, or \"_\"";
            return false;
        }
        if (!world::travel_position(place.position)) {
            m_error = "place " + std::to_string(index) + " (" + place.id + ") has a position that is not a place for a character";
            return false;
        }
        if (!byId.emplace(lower_ascii(place.id), index).second) {
            m_error = "two places have the id " + place.id;
            return false;
        }
        territories.insert(place.territory);
    }
    m_places = std::move(places);
    m_byId = std::move(byId);
    m_territories = territories.size();
    m_error.clear();
    return true;
}

const TravelPlace* TravelPlaces::find(std::string_view id) const {
    const auto found = m_byId.find(lower_ascii(id));
    return found == m_byId.end() ? nullptr : &m_places[found->second];
}

} // namespace soulwarden::backend::gamedata

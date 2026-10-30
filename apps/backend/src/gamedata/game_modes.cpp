#include "gamedata/game_modes.h"

#include <cmath>
#include <fstream>

#include <nlohmann/json.hpp>

#include "world/position_index.h"

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

/** The form of the file that this code reads (tools/gamedata/game_modes.py). */
constexpr std::uint64_t kFormat = 1;

std::string lower_ascii(std::string_view text) {
    std::string lower(text);
    for (char& letter : lower) {
        if (letter >= 'A' && letter <= 'Z') {
            letter = static_cast<char>(letter + ('a' - 'A'));
        }
    }
    return lower;
}

bool read_text(const json& parent, const char* key, std::string& text) {
    if (!parent.contains(key) || !parent[key].is_string()) {
        return false;
    }
    text = parent[key].get<std::string>();
    return true;
}

bool read_count(const json& parent, const char* key, std::uint32_t& value) {
    if (!parent.contains(key) || !parent[key].is_number_unsigned() || parent[key].get<std::uint64_t>() > 0xFFFFFFFFULL) {
        return false;
    }
    value = parent[key].get<std::uint32_t>();
    return true;
}

bool read_flag(const json& parent, const char* key, bool& value) {
    if (!parent.contains(key) || !parent[key].is_boolean()) {
        return false;
    }
    value = parent[key].get<bool>();
    return true;
}

bool read_map(const json& entry, GameMap& map) {
    if (!entry.is_object() || !read_text(entry, "id", map.id) || !read_text(entry, "mode", map.mode) || !read_count(entry, "row", map.row)
        || !read_count(entry, "modeRow", map.modeRow) || !read_text(entry, "level", map.level) || !read_text(entry, "terrain", map.terrain)
        || !read_text(entry, "nameKey", map.nameKey) || !read_text(entry, "name", map.name)
        || !read_count(entry, "recommendedLevel", map.recommendedLevel) || !read_count(entry, "requiredLevel", map.requiredLevel)
        || !read_count(entry, "teamCapacity", map.teamCapacity) || !read_count(entry, "minGroupSize", map.minGroupSize)
        || !read_flag(entry, "dungeon", map.dungeon) || !read_flag(entry, "soloTrial", map.soloTrial)
        || !read_flag(entry, "raidTrial", map.raidTrial) || !read_count(entry, "timeoutSeconds", map.timeoutSeconds)
        || !entry.contains("starts") || !entry["starts"].is_array()) {
        return false;
    }
    // Stage DG3: the entry rules of the mode. A member that is there must have its type; a file of before has none (no rule).
    if ((entry.contains("mutable") && !read_flag(entry, "mutable", map.mutableMode))
        || (entry.contains("entryConditional") && !read_text(entry, "entryConditional", map.entryConditional))
        || (entry.contains("requiredItemClasses") && !read_text(entry, "requiredItemClasses", map.requiredItemClasses))) {
        return false;
    }
    for (const json& start : entry["starts"]) {
        if (!start.is_array() || start.size() != 4) {
            return false;
        }
        float values[4] = {};
        for (std::size_t index = 0; index < 4; ++index) {
            if (!start[index].is_number()) {
                return false;
            }
            values[index] = start[index].get<float>();
        }
        map.starts.push_back(ModeStart{state::WorldPosition{values[0], values[1], values[2]}, values[3]});
    }
    return true;
}

} // namespace

bool GameModes::load(const std::string& path) {
    m_maps.clear();
    m_byId.clear();
    m_byMode.clear();
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
    if (!document.contains("format") || !document["format"].is_number_unsigned() || document["format"].get<std::uint64_t>() != kFormat) {
        m_error = "the file does not have the format 1";
        return false;
    }
    if (!document.contains("maps") || !document["maps"].is_array()) {
        m_error = "the file does not have the list maps";
        return false;
    }
    std::vector<GameMap> maps;
    maps.reserve(document["maps"].size());
    for (const json& entry : document["maps"]) {
        if (!read_map(entry, maps.emplace_back())) {
            m_error = "map " + std::to_string(maps.size() - 1) + " does not have each member";
            return false;
        }
    }
    // The count of the tool: a file that lost a part of its list is not complete.
    if (document.contains("counts")) {
        const json& counts = document["counts"];
        if (!counts.is_object() || !counts.contains("maps") || !counts["maps"].is_number_unsigned()
            || counts["maps"].get<std::uint64_t>() != maps.size()) {
            m_error = "the list does not have the count of the file";
            return false;
        }
    }
    return set(std::move(maps));
}

bool GameModes::set(std::vector<GameMap> maps) {
    m_maps.clear();
    m_byId.clear();
    m_byMode.clear();
    if (maps.empty()) {
        m_error = "the data has no map";
        return false;
    }
    std::unordered_map<std::string, std::size_t> byId;
    std::unordered_map<std::string, std::size_t> modeCount;
    for (std::size_t index = 0; index < maps.size(); ++index) {
        const GameMap& map = maps[index];
        const std::string what = "map " + std::to_string(index) + " (" + map.id + ")";
        if (map.id.empty() || map.mode.empty() || map.level.empty() || map.terrain.empty()) {
            m_error = what + " has no id, no mode, no level, or no terrain";
            return false;
        }
        if (map.starts.empty()) {
            m_error = what + " has no start place";
            return false;
        }
        for (const ModeStart& start : map.starts) {
            if (!world::travel_position(start.position) || !std::isfinite(start.heading)) {
                m_error = what + " has a start that is not a place for a character";
                return false;
            }
        }
        if (!byId.emplace(lower_ascii(map.id), index).second) {
            m_error = "two maps have the id " + map.id;
            return false;
        }
        ++modeCount[lower_ascii(map.mode)];
    }
    m_maps = std::move(maps);
    m_byId = std::move(byId);
    for (std::size_t index = 0; index < m_maps.size(); ++index) {
        const std::string mode = lower_ascii(m_maps[index].mode);
        if (modeCount[mode] == 1) {
            m_byMode.emplace(mode, index);
        }
    }
    m_error.clear();
    return true;
}

const GameMap* GameModes::find(std::string_view id) const {
    const std::string key = lower_ascii(id);
    if (const auto map = m_byId.find(key); map != m_byId.end()) {
        return &m_maps[map->second];
    }
    const auto mode = m_byMode.find(key);
    return mode == m_byMode.end() ? nullptr : &m_maps[mode->second];
}

} // namespace soulwarden::backend::gamedata

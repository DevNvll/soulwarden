#include "gamedata/quest_object_data.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
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

bool hex_bytes(const json& value, std::array<std::uint8_t, 16>& out) {
    if (!value.is_string() || value.get<std::string>().size() != 32) {
        return false;
    }
    const std::string text = value.get<std::string>();
    for (std::size_t index = 0; index < 16; ++index) {
        unsigned byte = 0;
        for (std::size_t digit = 0; digit < 2; ++digit) {
            const char letter = text[index * 2 + digit];
            byte <<= 4;
            if (letter >= '0' && letter <= '9') {
                byte |= static_cast<unsigned>(letter - '0');
            } else if (letter >= 'a' && letter <= 'f') {
                byte |= static_cast<unsigned>(letter - 'a' + 10);
            } else if (letter >= 'A' && letter <= 'F') {
                byte |= static_cast<unsigned>(letter - 'A' + 10);
            } else {
                return false;
            }
        }
        out[index] = static_cast<std::uint8_t>(byte);
    }
    return true;
}

std::string text_of(const json& object, const char* name) {
    return object.contains(name) && object[name].is_string() ? object[name].get<std::string>() : std::string{};
}

} // namespace

bool QuestObjectData::fail(std::string cause) {
    m_loaded = false;
    m_error = std::move(cause);
    m_rows.clear();
    m_objects.clear();
    m_cells.clear();
    m_proximityReach = 0.0F;
    m_proximityCount = 0;
    return false;
}

std::int64_t QuestObjectData::cell_of(float x, float y) {
    const auto cx = static_cast<std::int64_t>(std::floor(x / kCell));
    const auto cy = static_cast<std::int64_t>(std::floor(y / kCell));
    return (cx << 32) ^ (cy & 0xFFFFFFFF);
}

bool QuestObjectData::load(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return fail("the file is not there");
    }
    std::stringstream text;
    text << file.rdbuf();
    return load_text(text.str());
}

bool QuestObjectData::load_text(const std::string& text) {
    m_loaded = false;
    m_error.clear();
    m_rows.clear();
    m_objects.clear();
    m_cells.clear();
    m_proximityReach = 0.0F;
    m_proximityCount = 0;
    const json document = json::parse(text, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return fail("the file is not a JSON object");
    }
    if (!document.contains("format") || !document["format"].is_number_unsigned() || document["format"].get<std::uint64_t>() != kFormat) {
        return fail("the file does not have format 1");
    }
    if (!document.contains("rows") || !document["rows"].is_array() || !document.contains("objects") || !document["objects"].is_array()) {
        return fail("the file does not have the members rows and objects");
    }
    for (const json& entry : document["rows"]) {
        if (!entry.is_object() || !entry.contains("time") || !finite_number(entry["time"]) || !entry.contains("respawn")
            || !entry["respawn"].is_array() || entry["respawn"].size() != 2 || !finite_number(entry["respawn"][0])
            || !finite_number(entry["respawn"][1])) {
            return fail("a row does not have the members time and respawn");
        }
        QuestObjectRow row;
        row.id = text_of(entry, "id");
        row.table = text_of(entry, "table");
        row.time = entry["time"].get<float>();
        row.respawnMin = entry["respawn"][0].get<float>();
        row.respawnMax = entry["respawn"][1].get<float>();
        row.loot = text_of(entry, "loot");
        row.event = text_of(entry, "event");
        if (row.id.empty() || !(row.time > 0.0F) || row.respawnMin < 0.0F || row.respawnMax < row.respawnMin) {
            return fail("the row " + row.id + " has no id, a work that is not above 0, or respawn bounds that are not a range of 0 or more");
        }
        m_rows.push_back(std::move(row));
    }
    // One object for each placement key and for each actor (code review of stage 1.8e, finding QR8E-R7): the actor is the key of the
    // LootTracker entry of the client (the rule of the chest loader, finding CH-R4), and the strike keeps its work by the object.
    std::set<std::string> keys;
    std::map<std::array<std::uint8_t, 16>, std::string> actors;
    // Stage QG5a: the proximity objects (the member "proximity": a file of before has none) are objects with the radius of their detection
    // volume; a backend of before reads only "objects".
    if (document.contains("proximity") && !document["proximity"].is_array()) {
        return fail("the member proximity is not a list");
    }
    for (const bool proximity : {false, true}) {
        if (proximity && !document.contains("proximity")) {
            break;
        }
        for (const json& entry : document[proximity ? "proximity" : "objects"]) {
            QuestObjectPlacement object;
            if (!entry.is_object() || !hex_bytes(entry.value("actor", json()), object.actor) || !hex_bytes(entry.value("asset", json()), object.asset)
                || !entry.contains("row") || !entry["row"].is_number_unsigned() || !entry.contains("position") || !entry["position"].is_array()
                || entry["position"].size() != 3 || !entry.contains("slot") || !entry["slot"].is_number_unsigned()) {
                return fail("an object does not have the members actor, asset, row, slot, and position");
            }
            for (const json& axis : entry["position"]) {
                if (!finite_number(axis) || std::fabs(axis.get<double>()) > kMaxCoordinate) {
                    return fail("an object has a position that is not finite or that is outside the coordinates of the grid");
                }
            }
            object.key = lower_text(text_of(entry, "key"));
            object.sub = entry.contains("sub") && entry["sub"].is_number_unsigned() ? entry["sub"].get<std::uint32_t>() : 2;
            object.slot = entry["slot"].get<std::uint32_t>();
            object.variant = text_of(entry, "variant");
            object.row = entry["row"].get<std::uint32_t>();
            object.tag = lower_text(text_of(entry, "tag"));
            object.onlyOnQuest = entry.contains("onlyOnQuest") && entry["onlyOnQuest"].is_boolean() && entry["onlyOnQuest"].get<bool>();
            object.position = state::WorldPosition{entry["position"][0].get<float>(), entry["position"][1].get<float>(), entry["position"][2].get<float>()};
            object.proximity = proximity;
            if (proximity) {
                if (!entry.contains("radius") || !finite_number(entry["radius"]) || !(entry["radius"].get<double>() > 0.0)
                    || entry["radius"].get<double>() > kLargestRadius) {
                    return fail("the proximity object " + object.key + " has no radius above 0 and at most " + std::to_string(kLargestRadius) + " m");
                }
                object.radius = entry["radius"].get<float>();
                m_proximityReach = std::max(m_proximityReach, object.radius);
                ++m_proximityCount;
            }
            if (object.key.empty() || object.row >= m_rows.size()) {
                return fail("the object " + object.key + " has no key or names a row that the file does not have");
            }
            if (!keys.insert(object.key).second) {
                return fail("two objects have the key " + object.key + ": one object for each placement key");
            }
            if (const auto [other, added] = actors.emplace(object.actor, object.key); !added) {
                return fail("the object " + object.key + " has the actor of the object " + other->second + ": one object for each actor");
            }
            m_cells[cell_of(object.position.x, object.position.y)].push_back(static_cast<std::uint32_t>(m_objects.size()));
            m_objects.push_back(std::move(object));
        }
    }
    m_loaded = true;
    return true;
}

std::vector<std::uint32_t> QuestObjectData::nearby(const state::WorldPosition& place, float radius, std::uint64_t* visits) const {
    std::vector<std::uint32_t> found;
    if (!m_loaded) {
        return found;
    }
    const auto low_x = static_cast<std::int64_t>(std::floor((place.x - radius) / kCell));
    const auto high_x = static_cast<std::int64_t>(std::floor((place.x + radius) / kCell));
    const auto low_y = static_cast<std::int64_t>(std::floor((place.y - radius) / kCell));
    const auto high_y = static_cast<std::int64_t>(std::floor((place.y + radius) / kCell));
    for (std::int64_t cx = low_x; cx <= high_x; ++cx) {
        for (std::int64_t cy = low_y; cy <= high_y; ++cy) {
            const auto cell = m_cells.find((cx << 32) ^ (cy & 0xFFFFFFFF));
            if (cell == m_cells.end()) {
                continue;
            }
            for (const std::uint32_t index : cell->second) {
                if (visits != nullptr) {
                    ++*visits;
                }
                const QuestObjectPlacement& object = m_objects[index];
                const float dx = object.position.x - place.x;
                const float dy = object.position.y - place.y;
                if (dx * dx + dy * dy <= radius * radius) {
                    found.push_back(index);
                }
            }
        }
    }
    return found;
}

} // namespace soulwarden::backend::gamedata

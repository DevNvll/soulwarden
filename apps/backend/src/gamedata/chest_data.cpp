#include "gamedata/chest_data.h"

#include <array>
#include <cmath>
#include <fstream>
#include <map>

#include <nlohmann/json.hpp>

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

/** The form of the file that this code reads (tools/gamedata/chests.py). */
constexpr std::uint64_t kFormat = 1;
/** The largest reset value of a gatherable row (seconds): a week. A larger value is not a value of a chest. */
constexpr float kLargestReset = 7.0F * 24.0F * 3600.0F;
/** The largest open time of a gatherable row (seconds). */
constexpr float kLongestOpen = 600.0F;
/** The largest content level of a territory. */
constexpr std::uint64_t kLargestLevel = 1000;
/** The largest coordinate of a chest place (the world limits of world/world.h: a region grid of 2,048 m). */
constexpr float kLargestCoordinate = 200000.0F;

constexpr std::pair<const char*, ChestKind> kKinds[] = {
    {"supply", ChestKind::supply}, {"provision", ChestKind::provision}, {"alchemy", ChestKind::alchemy},
    {"elite", ChestKind::elite},   {"other", ChestKind::other},
};

std::string lower_ascii(std::string_view text) {
    std::string lower(text);
    for (char& letter : lower) {
        if (letter >= 'A' && letter <= 'Z') {
            letter = static_cast<char>(letter + ('a' - 'A'));
        }
    }
    return lower;
}

bool hex_bytes(const std::string& text, std::array<std::uint8_t, 16>& bytes) {
    if (text.size() != 32) {
        return false;
    }
    for (std::size_t index = 0; index < 16; ++index) {
        unsigned value = 0;
        for (std::size_t digit = 0; digit < 2; ++digit) {
            const char letter = text[index * 2 + digit];
            value <<= 4;
            if (letter >= '0' && letter <= '9') {
                value |= static_cast<unsigned>(letter - '0');
            } else if (letter >= 'a' && letter <= 'f') {
                value |= static_cast<unsigned>(letter - 'a' + 10);
            } else if (letter >= 'A' && letter <= 'F') {
                value |= static_cast<unsigned>(letter - 'A' + 10);
            } else {
                return false;
            }
        }
        bytes[index] = static_cast<std::uint8_t>(value);
    }
    return true;
}

/** @return True for the text form of a UUID (8-4-4-4-12 hexadecimal digits). */
bool uuid_text(const std::string& text) {
    if (text.size() != 36) {
        return false;
    }
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char letter = text[index];
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            if (letter != '-') {
                return false;
            }
        } else if (!((letter >= '0' && letter <= '9') || (letter >= 'a' && letter <= 'f') || (letter >= 'A' && letter <= 'F'))) {
            return false;
        }
    }
    return true;
}

bool finite_number(const json& value, float& out) {
    if (!value.is_number()) {
        return false;
    }
    const double number = value.get<double>();
    if (!std::isfinite(number)) {
        return false;
    }
    out = static_cast<float>(number);
    return std::isfinite(out);
}

} // namespace

const char* chest_kind_text(ChestKind kind) {
    for (const auto& [text, value] : kKinds) {
        if (value == kind) {
            return text;
        }
    }
    return "?";
}

bool ChestData::load(const std::string& path) {
    *this = ChestData{};
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
    if (!document.contains("rules") || !document["rules"].is_object() || !document.contains("gatherables")
        || !document["gatherables"].is_object() || !document.contains("chests") || !document["chests"].is_array()) {
        m_error = "the file does not have the members rules, gatherables, and chests";
        return false;
    }
    ChestRules rules;
    const json& ruleDocument = document["rules"];
    const std::pair<const char*, std::uint32_t*> ruleValues[] = {{"resetMinutes", &rules.resetMinutes}, {"countLimit", &rules.countLimit}};
    for (const auto& [key, target] : ruleValues) {
        if (!ruleDocument.contains(key) || !ruleDocument[key].is_number_unsigned() || ruleDocument[key].get<std::uint64_t>() > 100000) {
            m_error = std::string("the rule ") + key + " is not a whole number in its range";
            return false;
        }
        *target = ruleDocument[key].get<std::uint32_t>();
    }
    std::vector<ChestGatherable> gatherables;
    std::unordered_map<std::string, std::int32_t> gatherableIndex;
    for (const auto& [id, entry] : document["gatherables"].items()) {
        ChestGatherable row;
        row.id = id;
        if (!entry.is_object() || !entry.contains("open") || !finite_number(entry["open"], row.openSeconds) || !entry.contains("resetMin")
            || !finite_number(entry["resetMin"], row.resetMin) || !entry.contains("resetMax")
            || !finite_number(entry["resetMax"], row.resetMax) || !entry.contains("elite") || !entry["elite"].is_boolean()) {
            m_error = "the gatherable row " + id + " has no open time, reset values, or elite mark of a correct form";
            return false;
        }
        row.elite = entry["elite"].get<bool>();
        if (entry.contains("event") && entry["event"].is_string()) {
            row.event = entry["event"].get<std::string>();
        }
        gatherableIndex[id] = static_cast<std::int32_t>(gatherables.size());
        gatherables.push_back(std::move(row));
    }
    std::vector<ChestDefinition> chests;
    chests.reserve(document["chests"].size());
    for (const json& entry : document["chests"]) {
        const std::string where = "chest " + std::to_string(chests.size());
        if (!entry.is_object()) {
            m_error = where + " is not an object";
            return false;
        }
        ChestDefinition chest;
        const std::pair<const char*, std::string*> texts[] = {{"key", &chest.key},   {"variant", &chest.variant}, {"name", &chest.name},
                                                              {"root", &chest.root}, {"reason", &chest.reason},   {"region", &chest.region}};
        for (const auto& [key, target] : texts) {
            if (!entry.contains(key) || !entry[key].is_string()) {
                m_error = where + std::string(" has no text ") + key;
                return false;
            }
            *target = entry[key].get<std::string>();
        }
        std::string actor;
        std::string kind;
        std::string state;
        std::string gatherable;
        const std::pair<const char*, std::string*> marks[] = {{"actor", &actor}, {"kind", &kind}, {"state", &state}, {"gatherable", &gatherable}};
        for (const auto& [key, target] : marks) {
            if (!entry.contains(key) || !entry[key].is_string()) {
                m_error = where + std::string(" has no text ") + key;
                return false;
            }
            *target = entry[key].get<std::string>();
        }
        if (!hex_bytes(actor, chest.actor)) {
            m_error = where + " has an actor that is not 32 hexadecimal digits";
            return false;
        }
        bool kindFound = false;
        for (const auto& [text, value] : kKinds) {
            if (kind == text) {
                chest.kind = value;
                kindFound = true;
            }
        }
        if (!kindFound || (state != "ordinary" && state != "unavailable")) {
            m_error = where + " has a kind or a state that the backend does not know";
            return false;
        }
        chest.available = state == "ordinary";
        if (!gatherable.empty()) {
            const auto found = gatherableIndex.find(gatherable);
            if (found == gatherableIndex.end()) {
                m_error = where + " names the gatherable row " + gatherable + ", which the file does not have";
                return false;
            }
            chest.gatherable = found->second;
        }
        if (!entry.contains("position") || !entry["position"].is_array() || entry["position"].size() != 3) {
            m_error = where + " has no position of three numbers";
            return false;
        }
        float* const axes[] = {&chest.position.x, &chest.position.y, &chest.position.z};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (!finite_number(entry["position"][axis], *axes[axis])) {
                m_error = where + " has a position that is not finite";
                return false;
            }
        }
        if (!entry.contains("tags") || !entry["tags"].is_array()) {
            m_error = where + " has no list tags";
            return false;
        }
        for (const json& tag : entry["tags"]) {
            if (!tag.is_string()) {
                m_error = where + " has a tag that is not a text";
                return false;
            }
            chest.tags.push_back(tag.get<std::string>());
        }
        if (!entry.contains("contentLevel")) {
            m_error = where + " has no member contentLevel";
            return false;
        }
        if (!entry["contentLevel"].is_null()) {
            if (!entry["contentLevel"].is_number_unsigned() || entry["contentLevel"].get<std::uint64_t>() == 0
                || entry["contentLevel"].get<std::uint64_t>() > kLargestLevel) {
                m_error = where + " has a content level that is not a level";
                return false;
            }
            chest.contentLevel = entry["contentLevel"].get<std::uint32_t>();
        }
        chests.push_back(std::move(chest));
    }
    if (!set(rules, std::move(gatherables), std::move(chests))) {
        const std::string cause = m_error;
        *this = ChestData{};
        m_error = cause;
        return false;
    }
    return true;
}

bool ChestData::set(ChestRules rules, std::vector<ChestGatherable> gatherables, std::vector<ChestDefinition> chests) {
    const auto refuse = [this](std::string cause) {
        const std::string text = std::move(cause);
        *this = ChestData{};
        m_error = text;
        return false;
    };
    if (rules.resetMinutes == 0) {
        return refuse("the rule resetMinutes is 0");
    }
    for (const ChestGatherable& row : gatherables) {
        if (!(row.openSeconds > 0.0F) || row.openSeconds > kLongestOpen || !(row.resetMin >= 0.0F) || !(row.resetMax >= 0.0F)
            || row.resetMin > kLargestReset || row.resetMax > kLargestReset || (row.resetMax != 0.0F && row.resetMax < row.resetMin)) {
            return refuse("the gatherable row " + row.id + " has an open time or a reset value out of its range");
        }
    }
    std::unordered_map<std::string, std::uint32_t> byKey;
    // The actor of a chest is its key in the LootTracker of the client (code review of stage 3.5b.1, finding CH-R4): one chest for
    // each actor, or the map of the client hides a different chest.
    std::map<std::array<std::uint8_t, 16>, std::uint32_t> byActor;
    std::unordered_map<std::int64_t, std::vector<std::uint32_t>> cells;
    std::size_t ordinary = 0;
    for (std::uint32_t index = 0; index < chests.size(); ++index) {
        const ChestDefinition& chest = chests[index];
        const std::string where = "the chest " + chest.key;
        if (!uuid_text(chest.key)) {
            return refuse(where + ": the key is not a UUID");
        }
        if (!byKey.emplace(lower_ascii(chest.key), index).second) {
            return refuse(where + " is in the file two times");
        }
        if (const auto [other, added] = byActor.emplace(chest.actor, index); !added) {
            return refuse(where + " has the actor of the chest " + chests[other->second].key + ": one chest for each actor");
        }
        if (chest.gatherable >= static_cast<std::int32_t>(gatherables.size())) {
            return refuse(where + " names a gatherable row that the data does not have");
        }
        for (const float value : {chest.position.x, chest.position.y, chest.position.z}) {
            if (!std::isfinite(value) || std::fabs(value) > kLargestCoordinate) {
                return refuse(where + " has a position out of the world");
            }
        }
        if (chest.available && (chest.gatherable < 0 || chest.root.empty() || !chest.reason.empty())) {
            return refuse(where + " is ordinary with no gatherable row, with no root, or with a reason");
        }
        if (!chest.available && chest.reason.empty()) {
            return refuse(where + " is unavailable with no reason");
        }
        ordinary += chest.available ? 1 : 0;
        cells[cell_of(chest.position.x, chest.position.y)].push_back(index);
    }
    m_rules = rules;
    m_gatherables = std::move(gatherables);
    m_chests = std::move(chests);
    m_byKey = std::move(byKey);
    m_cells = std::move(cells);
    m_ordinary = ordinary;
    m_error.clear();
    return true;
}

std::optional<std::uint32_t> ChestData::find(std::string_view key) const {
    const auto found = m_byKey.find(lower_ascii(key));
    return found == m_byKey.end() ? std::nullopt : std::optional<std::uint32_t>(found->second);
}

} // namespace soulwarden::backend::gamedata

#include "gamedata/progression_tables.h"

#include <algorithm>
#include <fstream>

#include <nlohmann/json.hpp>

#include "gamedata/item_catalog.h"

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

bool read_number(const json& value, std::uint32_t& number) {
    if (!value.is_number_unsigned() && !(value.is_number_integer() && value.get<std::int64_t>() >= 0)) {
        return false;
    }
    const std::uint64_t wide = value.get<std::uint64_t>();
    if (wide > 0xFFFFFFFFULL) {
        return false;
    }
    number = static_cast<std::uint32_t>(wide);
    return true;
}

bool read_numbers(const json& value, std::vector<std::uint32_t>& numbers) {
    if (!value.is_array()) {
        return false;
    }
    numbers.clear();
    numbers.reserve(value.size());
    for (const json& entry : value) {
        std::uint32_t number = 0;
        if (!read_number(entry, number)) {
            return false;
        }
        numbers.push_back(number);
    }
    return true;
}

} // namespace

bool valid_ranks(const ProgressionRow& row) {
    if (row.maxRank > kLargestRank || row.ranks.experience.size() < row.maxRank) {
        return false;
    }
    for (std::uint32_t rank = 0; rank < row.maxRank; ++rank) {
        if (row.ranks.experience[rank] == 0) {
            return false;
        }
    }
    // A weapon: the cost of a tree reset at each rank that a character can have.
    return row.kind != ProgressionKind::weapon || row.ranks.resetCost.size() > row.maxRank;
}

std::uint32_t progression_id(std::string_view text) {
    // The same rule as for an item id: the CRC-32 of the text with the letters A to Z in lower case.
    return item_id_crc(text);
}

bool ProgressionTables::load(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    const json document = json::parse(input, nullptr, false);
    if (!document.is_object()) {
        return false;
    }
    for (const char* member : {"levelCap", "levels", "attributes", "vitals", "rankTables", "progressions", "abilities"}) {
        if (!document.contains(member)) {
            return false;
        }
    }
    ProgressionData data;
    if (!read_number(document["levelCap"], data.levelCap) || !document["levels"].is_array()) {
        return false;
    }
    for (const json& row : document["levels"]) {
        LevelRow level;
        if (!row.is_array() || row.size() != 4 || !read_number(row[0], level.experience)
            || !read_number(row[1], level.attributePoints) || !read_number(row[2], level.health)
            || !read_number(row[3], level.resetCost)) {
            return false;
        }
        // The total experience of the first level is 0, and each later level starts at a
        // larger total: the gain of a level is a compare with these limits in their sequence.
        if (data.levels.empty() ? level.experience != 0 : level.experience <= data.levels.back().experience) {
            return false;
        }
        data.levels.push_back(level);
    }
    const json& attributes = document["attributes"];
    const json& vitals = document["vitals"];
    if (!attributes.is_object() || !attributes.contains("minimum") || !attributes.contains("constitutionHealth")
        || !read_number(attributes["minimum"], data.attributeMinimum)
        || !read_numbers(attributes["constitutionHealth"], data.constitutionHealth) || !vitals.is_object()
        || !vitals.contains("health") || !vitals.contains("stamina") || !vitals.contains("mana")
        || !read_number(vitals["health"], data.baseHealth) || !read_number(vitals["stamina"], data.baseStamina)
        || !read_number(vitals["mana"], data.baseMana)) {
        return false;
    }
    // The rank tables: {"<name>": {"experience": [...], "resetCost": [...]}}.
    std::map<std::string, RankTable> rankTables;
    if (!document["rankTables"].is_object()) {
        return false;
    }
    for (const auto& [name, table] : document["rankTables"].items()) {
        RankTable ranks;
        if (!table.is_object() || !table.contains("experience") || !read_numbers(table["experience"], ranks.experience)
            || (table.contains("resetCost") && !read_numbers(table["resetCost"], ranks.resetCost))) {
            return false;
        }
        rankTables.emplace(name, std::move(ranks));
    }
    // The progressions: [id, kind, maxRank, rankTable].
    if (!document["progressions"].is_array()) {
        return false;
    }
    for (const json& row : document["progressions"]) {
        ProgressionRow progression;
        if (!row.is_array() || row.size() != 4 || !row[0].is_string() || !row[1].is_string()
            || !read_number(row[2], progression.maxRank) || !row[3].is_string()) {
            return false;
        }
        progression.id = row[0].get<std::string>();
        const std::string kind = row[1].get<std::string>();
        const auto ranks = rankTables.find(row[3].get<std::string>());
        if ((kind != "weapon" && kind != "tradeskill") || ranks == rankTables.end()) {
            return false;
        }
        progression.kind = kind == "weapon" ? ProgressionKind::weapon : ProgressionKind::tradeSkill;
        progression.ranks = ranks->second;
        if (!valid_ranks(progression)) {
            return false;
        }
        data.progressions.push_back(std::move(progression));
    }
    // The nodes of the weapon trees: [table, node, tree, row, column, active, requiredNode].
    if (!document["abilities"].is_array()) {
        return false;
    }
    for (const json& row : document["abilities"]) {
        AbilityNode node;
        if (!row.is_array() || row.size() != 7 || !row[0].is_string() || !row[1].is_string()
            || !read_number(row[2], node.tree) || !read_number(row[3], node.row) || !read_number(row[4], node.column)
            || !row[5].is_boolean() || !row[6].is_string()) {
            return false;
        }
        node.table = progression_id(row[0].get<std::string>());
        node.id = row[1].get<std::string>();
        node.active = row[5].get<bool>();
        const std::string required = row[6].get<std::string>();
        node.required = required.empty() ? 0 : progression_id(required);
        data.nodes.push_back(std::move(node));
    }
    if (data.levels.empty() || data.levelCap == 0 || data.levelCap > data.levels.size()) {
        return false;
    }
    set(std::move(data));
    return true;
}

void ProgressionTables::set(ProgressionData data) {
    m_data = std::move(data);
    build_keys();
}

void ProgressionTables::build_keys() {
    m_progressionKeys.clear();
    m_nodeKeys.clear();
    m_lastRows.clear();
    for (std::size_t position = 0; position < m_data.progressions.size(); ++position) {
        ProgressionRow& row = m_data.progressions[position];
        row.wireId = progression_id(row.id);
        m_progressionKeys.emplace(row.wireId, position);
    }
    for (std::size_t position = 0; position < m_data.nodes.size(); ++position) {
        AbilityNode& node = m_data.nodes[position];
        node.wireId = progression_id(node.id);
        m_nodeKeys.emplace(node.wireId, position);
        std::uint32_t& last = m_lastRows[{node.table, node.tree}];
        last = std::max(last, node.row);
    }
}

const LevelRow* ProgressionTables::level(std::uint32_t level) const {
    return level == 0 || level > m_data.levels.size() ? nullptr : &m_data.levels[level - 1];
}

std::uint32_t ProgressionTables::attribute_points(std::uint32_t level) const {
    // The first level gives no point: the sum is for the levels 2 to `level`.
    std::uint32_t total = 0;
    for (std::size_t index = 1; index < level && index < m_data.levels.size(); ++index) {
        total += m_data.levels[index].attributePoints;
    }
    return total;
}

std::uint32_t ProgressionTables::attribute_maximum() const {
    return m_data.constitutionHealth.empty()
               ? m_data.attributeMinimum
               : m_data.attributeMinimum + static_cast<std::uint32_t>(m_data.constitutionHealth.size()) - 1;
}

std::uint32_t ProgressionTables::constitution_health(std::uint32_t value) const {
    if (value < m_data.attributeMinimum || value - m_data.attributeMinimum >= m_data.constitutionHealth.size()) {
        return 0;
    }
    return m_data.constitutionHealth[value - m_data.attributeMinimum];
}

const ProgressionRow* ProgressionTables::progression(std::uint32_t wireId) const {
    const auto found = m_progressionKeys.find(wireId);
    return found == m_progressionKeys.end() ? nullptr : &m_data.progressions[found->second];
}

const ProgressionRow* ProgressionTables::progression(std::string_view id) const {
    return progression(progression_id(id));
}

const AbilityNode* ProgressionTables::node(std::uint32_t wireId) const {
    const auto found = m_nodeKeys.find(wireId);
    return found == m_nodeKeys.end() ? nullptr : &m_data.nodes[found->second];
}

const AbilityNode* ProgressionTables::node(std::string_view id) const {
    return node(progression_id(id));
}

std::uint32_t ProgressionTables::last_row(std::uint32_t table, std::uint32_t tree) const {
    const auto found = m_lastRows.find({table, tree});
    return found == m_lastRows.end() ? 0 : found->second;
}

} // namespace soulwarden::backend::gamedata

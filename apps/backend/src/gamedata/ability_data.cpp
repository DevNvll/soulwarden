#include "gamedata/ability_data.h"

#include <cmath>
#include <fstream>

#include <nlohmann/json.hpp>

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

/** The form of the file that this code reads (tools/gamedata/abilities.py). */
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

/** @return True when a cell of a row is a finite number; the value goes to `value`. */
bool number(const json& row, std::size_t column, float& value) {
    if (column >= row.size() || !row[column].is_number()) {
        return false;
    }
    value = row[column].get<float>();
    return std::isfinite(value);
}

bool whole(const json& row, std::size_t column, std::uint64_t highest, std::uint64_t& value) {
    if (column >= row.size() || !row[column].is_number_unsigned()) {
        return false;
    }
    value = row[column].get<std::uint64_t>();
    return value <= highest;
}

bool flag(const json& row, std::size_t column, bool& value) {
    if (column >= row.size() || !row[column].is_boolean()) {
        return false;
    }
    value = row[column].get<bool>();
    return true;
}

bool text(const json& row, std::size_t column, std::string& value) {
    if (column >= row.size() || !row[column].is_string() || row[column].get<std::string>().empty()) {
        return false;
    }
    value = row[column].get<std::string>();
    return true;
}

bool read_cooldown(const json& row, CooldownRow& cooldown) {
    return row.is_array() && row.size() == 3 && text(row, 0, cooldown.id) && number(row, 1, cooldown.time)
           && number(row, 2, cooldown.minTime);
}

bool read_status(const json& row, StatusRow& status) {
    std::uint64_t index = 0;
    std::uint64_t stopOnHit = 0;
    if (!row.is_array() || row.size() != 9 || !text(row, 0, status.id) || !whole(row, 1, 0xFFFF, index)
        || !number(row, 2, status.duration) || !number(row, 3, status.moveSpeed) || !flag(row, 4, status.stunned)
        || !flag(row, 5, status.root) || !whole(row, 6, 1000, stopOnHit) || !flag(row, 7, status.nameplate)
        || !number(row, 8, status.damage)) {
        return false;
    }
    status.index = static_cast<std::uint16_t>(index);
    status.stopOnHit = static_cast<std::uint32_t>(stopOnHit);
    return true;
}

bool read_spell(const json& row, SpellRow& spell) {
    std::uint64_t count = 0;
    if (!row.is_array() || row.size() != 7 || !text(row, 0, spell.id) || !number(row, 1, spell.radius)
        || !number(row, 2, spell.height) || !number(row, 3, spell.castDistance) || !whole(row, 4, 1000, count)
        || !number(row, 5, spell.spawnRate) || !number(row, 6, spell.duration)) {
        return false;
    }
    spell.count = static_cast<std::uint32_t>(count);
    return true;
}

template <class Row, class Read>
bool read_list(const json& document, const char* name, Read read, std::vector<Row>& rows, std::string& error) {
    if (!document.contains(name) || !document[name].is_array()) {
        error = std::string("the file does not have the list ") + name;
        return false;
    }
    for (const json& entry : document[name]) {
        if (!read(entry, rows.emplace_back())) {
            error = std::string("row ") + std::to_string(rows.size() - 1) + " of the list " + name + " does not have the form of the tool";
            return false;
        }
    }
    if (document.contains("counts")) {
        const json& counts = document["counts"];
        if (!counts.is_object() || !counts.contains(name) || !counts[name].is_number_unsigned()
            || counts[name].get<std::uint64_t>() != rows.size()) {
            error = std::string("the list ") + name + " does not have the count of the file";
            return false;
        }
    }
    return true;
}

template <class Row>
bool index_rows(const std::vector<Row>& rows, std::unordered_map<std::string, std::size_t>& keys, const char* name,
                std::string& error) {
    for (std::size_t position = 0; position < rows.size(); ++position) {
        if (!keys.emplace(lower_ascii(rows[position].id), position).second) {
            error = std::string("the list ") + name + " has two rows with the id " + rows[position].id;
            return false;
        }
    }
    return true;
}

template <class Row>
const Row* find_row(const std::vector<Row>& rows, const std::unordered_map<std::string, std::size_t>& keys, std::string_view id) {
    const auto found = keys.find(lower_ascii(id));
    return found == keys.end() ? nullptr : &rows[found->second];
}

} // namespace

bool AbilityData::load(const std::string& path) {
    m_tables = {};
    m_cooldowns.clear();
    m_status.clear();
    m_spells.clear();
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
    AbilityTables tables;
    if (!read_list(document, "cooldowns", read_cooldown, tables.cooldowns, m_error)
        || !read_list(document, "statusEffects", read_status, tables.statusEffects, m_error)
        || !read_list(document, "spells", read_spell, tables.spells, m_error)) {
        return false;
    }
    return set(std::move(tables));
}

bool AbilityData::set(AbilityTables tables) {
    m_tables = {};
    m_cooldowns.clear();
    m_status.clear();
    m_spells.clear();
    std::unordered_map<std::string, std::size_t> cooldowns;
    std::unordered_map<std::string, std::size_t> status;
    std::unordered_map<std::string, std::size_t> spells;
    std::string error;
    if (tables.cooldowns.empty()) {
        error = "the data has no cooldown";
    } else if (index_rows(tables.cooldowns, cooldowns, "cooldowns", error) && index_rows(tables.statusEffects, status, "statusEffects", error)
               && index_rows(tables.spells, spells, "spells", error)) {
        for (const CooldownRow& row : tables.cooldowns) {
            if (row.time < 0.0F || row.minTime < 0.0F) {
                error = "the cooldown of " + row.id + " is negative";
                break;
            }
        }
    }
    if (!error.empty()) {
        m_error = error;
        return false;
    }
    m_tables = std::move(tables);
    m_cooldowns = std::move(cooldowns);
    m_status = std::move(status);
    m_spells = std::move(spells);
    m_error.clear();
    return true;
}

const CooldownRow* AbilityData::cooldown(std::string_view id) const {
    return find_row(m_tables.cooldowns, m_cooldowns, id);
}

const StatusRow* AbilityData::status(std::string_view id) const {
    return find_row(m_tables.statusEffects, m_status, id);
}

const SpellRow* AbilityData::spell(std::string_view id) const {
    return find_row(m_tables.spells, m_spells, id);
}

} // namespace soulwarden::backend::gamedata

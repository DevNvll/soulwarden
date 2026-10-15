#include "gamedata/ability_data.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

#include <nlohmann/json.hpp>

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

/** The forms of the file that this code reads (tools/gamedata/abilities.py): 3 from stage 3.3b-S1, with the caster
 * effect rows and the status categories; 2 from stage 1.3b, with the other cells of each status row and spell row and
 * the costs; 1 has none of them. */
constexpr std::uint64_t kFormat = 3;
/** Format 4 (stage 3.3b-S5): each row of the ability tables of the players, and the action spells and nodes. */
constexpr std::uint64_t kRulesFormat = 4;
constexpr std::uint64_t kCostFormat = 2;
constexpr std::uint64_t kOldFormat = 1;

/** @return The CRC-32 of a text as the client computes it: only the letters A to Z go to lower case. */
std::uint32_t status_crc(std::string_view text) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (char letter : text) {
        const auto byte = static_cast<std::uint8_t>(letter >= 'A' && letter <= 'Z' ? letter + 32 : letter);
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

/** A positive timer of a status row is not below this (seconds): the due list of the status system has milliseconds. */
constexpr float kShortestTimer = 0.001F;

/** A link of the game data to a row that the game data does not have (the retail tables have it so). */
struct MissingLink {
    const char* row;
    const char* target;
};
/**
 * The OnEndStatusEffect links of the game data (client build 1.400.6031.6004151) that name no row: the loader accepts
 * only these (code review of stage 3.3b-S1, finding F8). The status of the row then gets no end status.
 */
constexpr MissingLink kMissingEndLinks[] = {{"AI_IceDragon_EmpoweringLink_Stack_Apply", "AI_IceDragon_EmpoweringLink_Stack"}};

/** @return The parts of a list of the data ("A+B", "A,B"), with no space around them. */
std::vector<std::string_view> list_parts(std::string_view text) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find_first_of("+,", start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view part = text.substr(start, end - start);
        while (!part.empty() && part.front() == ' ') {
            part.remove_prefix(1);
        }
        while (!part.empty() && part.back() == ' ') {
            part.remove_suffix(1);
        }
        if (!part.empty()) {
            parts.push_back(part);
        }
        start = end + 1;
    }
    return parts;
}

bool read_cells(const json& value, CellValues& cells) {
    if (!value.is_object()) {
        return false;
    }
    for (const auto& [column, cell] : value.items()) {
        if (cell.is_string()) {
            cells.texts.emplace(column, cell.get<std::string>());
        } else if (cell.is_number() && std::isfinite(cell.get<float>())) {
            cells.numbers.emplace(column, cell.get<float>());
        } else {
            return false;
        }
    }
    return true;
}

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
    if (!row.is_array() || (row.size() != 9 && row.size() != 10) || (row.size() == 10 && !read_cells(row[9], status.cells))
        || !text(row, 0, status.id) || !whole(row, 1, 0xFFFF, index)
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
    if (!row.is_array() || (row.size() != 7 && row.size() != 8) || (row.size() == 8 && !read_cells(row[7], spell.cells))
        || !text(row, 0, spell.id) || !number(row, 1, spell.radius)
        || !number(row, 2, spell.height) || !number(row, 3, spell.castDistance) || !whole(row, 4, 1000, count)
        || !number(row, 5, spell.spawnRate) || !number(row, 6, spell.duration)) {
        return false;
    }
    spell.count = static_cast<std::uint32_t>(count);
    return true;
}

bool read_cost(const json& row, CostRow& cost) {
    return row.is_array() && row.size() == 2 && text(row, 0, cost.id) && number(row, 1, cost.cost) && cost.cost >= 0.0F;
}

bool read_texts(const json& value, std::vector<std::string>& out) {
    if (!value.is_array()) {
        return false;
    }
    for (const json& entry : value) {
        if (!entry.is_string() || entry.get<std::string>().empty()) {
            return false;
        }
        out.push_back(entry.get<std::string>());
    }
    return true;
}

bool read_ability_cost(const json& row, AbilityCostRow& cost) {
    return row.is_array() && row.size() == 5 && text(row, 0, cost.id) && read_texts(row[1], cost.mana)
           && number(row, 2, cost.manaMultiplier) && read_texts(row[3], cost.stamina) && number(row, 4, cost.staminaFlat);
}

bool read_ability_row(const json& row, AbilityRow& ability) {
    return row.is_array() && row.size() == 2 && text(row, 0, ability.id) && read_cells(row[1], ability.cells);
}

bool read_category(const json& row, StatusCategory& category) {
    if (!row.is_array() || row.size() != 2 || !text(row, 0, category.id) || !row[1].is_object()) {
        return false;
    }
    for (const auto& [name, limit] : row[1].items()) {
        if (name.empty() || !limit.is_number() || !std::isfinite(limit.get<float>())) {
            return false;
        }
        category.limits.emplace(name, limit.get<float>());
    }
    return true;
}

bool read_action_spell(const json& row, ActionSpell& action) {
    return row.is_array() && row.size() == 5 && text(row, 0, action.list) && text(row, 1, action.action) && text(row, 2, action.spell)
           && read_texts(row[3], action.needs) && read_texts(row[4], action.excludes);
}

bool read_action_nodes(const json& row, ActionNodes& nodes) {
    return row.is_array() && row.size() == 2 && text(row, 0, nodes.list) && read_texts(row[1], nodes.nodes) && !nodes.nodes.empty();
}

bool read_stat_limit(const json& row, StatLimit& limit) {
    return row.is_array() && row.size() == 3 && text(row, 0, limit.id) && number(row, 1, limit.min) && number(row, 2, limit.max);
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
    m_manaCosts.clear();
    m_staminaCosts.clear();
    m_abilityCosts.clear();
    m_abilityRows.clear();
    m_categories.clear();
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
    const std::uint64_t format =
        document.contains("format") && document["format"].is_number_unsigned() ? document["format"].get<std::uint64_t>() : 0;
    if (format != kRulesFormat && format != kFormat && format != kCostFormat && format != kOldFormat) {
        m_error = "the file does not have the format 1, 2, 3, or 4";
        return false;
    }
    AbilityTables tables;
    if (!document.contains("statusRows") || !document["statusRows"].is_number_unsigned()
        || document["statusRows"].get<std::uint64_t>() > 0x10000) {
        m_error = "the file does not have the number of the status rows";
        return false;
    }
    tables.statusRows = static_cast<std::uint32_t>(document["statusRows"].get<std::uint64_t>());
    if (!read_list(document, "cooldowns", read_cooldown, tables.cooldowns, m_error)
        || !read_list(document, "statusEffects", read_status, tables.statusEffects, m_error)
        || !read_list(document, "spells", read_spell, tables.spells, m_error)) {
        return false;
    }
    if (format >= kCostFormat
        && (!read_list(document, "manaCosts", read_cost, tables.manaCosts, m_error)
            || !read_list(document, "staminaCosts", read_cost, tables.staminaCosts, m_error)
            || !read_list(document, "abilityCosts", read_ability_cost, tables.abilityCosts, m_error))) {
        return false;
    }
    if (format >= kFormat
        && (!read_list(document, "abilityRows", read_ability_row, tables.abilityRows, m_error)
            || !read_list(document, "statusCategories", read_category, tables.statusCategories, m_error))) {
        return false;
    }
    // The limits of the stat multipliers (stage 3.3b-S2a): a member of a newer file of format 3. With no member the speed
    // table of a character has no limit of the data (the client bounds it itself).
    if (format >= kFormat && document.contains("statMultipliers")
        && !read_list(document, "statMultipliers", read_stat_limit, tables.statMultipliers, m_error)) {
        return false;
    }
    if (format >= kRulesFormat
        && (!read_list(document, "actionSpells", read_action_spell, tables.actionSpells, m_error)
            || !read_list(document, "actionNodes", read_action_nodes, tables.actionNodes, m_error))) {
        return false;
    }
    return accept(std::move(tables), true);
}

bool AbilityData::set(AbilityTables tables) {
    return accept(std::move(tables), false);
}

bool AbilityData::accept(AbilityTables tables, bool fromFile) {
    m_tables = {};
    m_cooldowns.clear();
    m_status.clear();
    m_spells.clear();
    m_manaCosts.clear();
    m_staminaCosts.clear();
    m_abilityCosts.clear();
    m_abilityRows.clear();
    m_categories.clear();
    m_statLimits.clear();
    m_actionSpells.clear();
    m_actionNodes.clear();
    m_trackingSpells.clear();
    std::unordered_map<std::string, std::size_t> cooldowns;
    std::unordered_map<std::string, std::size_t> status;
    std::unordered_map<std::string, std::size_t> spells;
    std::unordered_map<std::string, std::size_t> manaCosts;
    std::unordered_map<std::string, std::size_t> staminaCosts;
    std::unordered_map<std::string, std::size_t> abilityCosts;
    std::unordered_map<std::string, std::size_t> abilityRows;
    std::unordered_map<std::string, std::size_t> categories;
    std::unordered_map<std::string, std::size_t> statLimits;
    std::string error;
    if (tables.cooldowns.empty()) {
        error = "the data has no cooldown";
    } else if (index_rows(tables.cooldowns, cooldowns, "cooldowns", error) && index_rows(tables.statusEffects, status, "statusEffects", error)
               && index_rows(tables.spells, spells, "spells", error) && index_rows(tables.manaCosts, manaCosts, "manaCosts", error)
               && index_rows(tables.staminaCosts, staminaCosts, "staminaCosts", error)
               && index_rows(tables.abilityCosts, abilityCosts, "abilityCosts", error)
               && index_rows(tables.abilityRows, abilityRows, "abilityRows", error)
               && index_rows(tables.statusCategories, categories, "statusCategories", error)
               && index_rows(tables.statMultipliers, statLimits, "statMultipliers", error)) {
        for (const CooldownRow& row : tables.cooldowns) {
            if (row.time < 0.0F || row.minTime < 0.0F) {
                error = "the cooldown of " + row.id + " is negative";
                break;
            }
        }
    }
    // A file must name the number of the status rows of the game (code review of stage 1.3b, finding F10): a file that
    // declares 0 rows cannot give any status number to the wire. Only a test with no file uses 0 (not known).
    if (error.empty() && fromFile && !tables.statusEffects.empty() && tables.statusRows == 0) {
        error = "the file declares no status rows, but it has status effects";
    }
    // Each status that a caster effect row names is in the data (stage 3.3b-S1).
    for (const AbilityRow& row : tables.abilityRows) {
        const std::string_view names = row.cells.text("SelfApplyStatusEffect");
        std::size_t start = 0;
        while (error.empty() && start <= names.size()) {
            std::size_t end = names.find(',', start);
            if (end == std::string_view::npos) {
                end = names.size();
            }
            std::string_view name = names.substr(start, end - start);
            while (!name.empty() && name.front() == ' ') {
                name.remove_prefix(1);
            }
            while (!name.empty() && name.back() == ' ') {
                name.remove_suffix(1);
            }
            if (!name.empty() && status.count(lower_ascii(name)) == 0) {
                error = "the caster effect row " + row.id + " names the status " + std::string(name) + ", which the data does not have";
            }
            start = end + 1;
        }
    }
    // The timers of a status row (code review of stage 3.3b-S1, finding F9): the world clock of the status system has
    // milliseconds, and a positive time below 1 ms would not advance its due list. The data of the game has no such value.
    for (const StatusRow& row : tables.statusEffects) {
        if (!error.empty()) {
            break;
        }
        for (const auto& [name, value] : {std::pair<const char*, float>{"BaseDuration", row.duration},
                                          {"TickRate", row.cells.number("TickRate")},
                                          {"DurationMax", row.cells.number("DurationMax")},
                                          {"ApplicationCooldown", row.cells.number("ApplicationCooldown")}}) {
            if (value > 0.0F && value < kShortestTimer) {
                error = "the status effect " + row.id + " has a " + name + " above 0 and below 1 ms";
                break;
            }
        }
    }
    // The links of a status row (finding F8): each status of OnEndStatusEffect and the spell of CastSpell are in the data.
    // The game data has one link with no row: kMissingEndLinks names it.
    for (const StatusRow& row : tables.statusEffects) {
        if (!error.empty()) {
            break;
        }
        for (const std::string_view next : list_parts(row.cells.text("OnEndStatusEffect"))) {
            if (status.count(lower_ascii(next)) != 0) {
                continue;
            }
            const bool named = std::any_of(std::begin(kMissingEndLinks), std::end(kMissingEndLinks), [&](const MissingLink& link) {
                return lower_ascii(link.row) == lower_ascii(row.id) && lower_ascii(link.target) == lower_ascii(next);
            });
            if (!named) {
                error = "the status effect " + row.id + " names the end status " + std::string(next) + ", which the data does not have";
                break;
            }
        }
        const std::string_view spell = row.cells.text("CastSpell");
        if (error.empty() && !spell.empty() && spells.count(lower_ascii(spell)) == 0) {
            error = "the status effect " + row.id + " names the spell " + std::string(spell) + " (CastSpell), which the data does not have";
        }
    }
    if (error.empty() && !tables.statusEffects.empty()) {
        // The numbers of the status effects fit together (code review of stage 3.3a, finding F10): a number is the
        // position of the CRC-32 of the id in the sorted CRC list of all rows. Thus a larger CRC has a larger number,
        // two ids have two numbers, and each number is below the number of the rows.
        std::vector<std::pair<std::uint32_t, std::uint16_t>> numbers;
        numbers.reserve(tables.statusEffects.size());
        for (const StatusRow& row : tables.statusEffects) {
            numbers.emplace_back(status_crc(row.id), row.index);
            if (tables.statusRows != 0 && row.index >= tables.statusRows) {
                error = "the status effect " + row.id + " has a number that is not below the number of the status rows";
                break;
            }
        }
        if (tables.statusRows != 0 && tables.statusRows < tables.statusEffects.size()) {
            error = "the file has more status effects than status rows";
        }
        std::sort(numbers.begin(), numbers.end());
        for (std::size_t position = 1; error.empty() && position < numbers.size(); ++position) {
            if (numbers[position].second <= numbers[position - 1].second) {
                error = "two status effects have numbers that do not fit the sequence of their CRC values";
            }
        }
    }
    // The action spells (stage 3.3b-S5): each spell of a selection is a spell of the data.
    std::unordered_map<std::string, std::vector<std::size_t>> actionSpells;
    for (std::size_t index = 0; error.empty() && index < tables.actionSpells.size(); ++index) {
        const ActionSpell& action = tables.actionSpells[index];
        if (spells.count(lower_ascii(action.spell)) == 0) {
            error = "the action " + action.action + " casts the spell " + action.spell + ", which the data does not have";
        }
        actionSpells[lower_ascii(action.action)].push_back(index);
    }
    std::unordered_map<std::string, std::vector<std::string>> actionNodes;
    for (const ActionNodes& list : tables.actionNodes) {
        for (const std::string& node : list.nodes) {
            actionNodes[lower_ascii(node)].push_back(list.list);
        }
    }
    // The tracking spell of a rule: the first spell row with its AbilityId and TrackNumInVolume.
    std::unordered_map<std::string, std::size_t> tracking;
    for (std::size_t index = 0; index < tables.spells.size(); ++index) {
        const SpellRow& spell = tables.spells[index];
        const std::string_view rule = spell.cells.text("AbilityId");
        if (!rule.empty() && spell.cells.number("TrackNumInVolume") != 0.0F) {
            tracking.emplace(lower_ascii(rule), index);
        }
    }
    if (!error.empty()) {
        m_error = error;
        return false;
    }
    m_actionSpells = std::move(actionSpells);
    m_actionNodes = std::move(actionNodes);
    m_trackingSpells = std::move(tracking);
    m_tables = std::move(tables);
    m_cooldowns = std::move(cooldowns);
    m_status = std::move(status);
    m_spells = std::move(spells);
    m_manaCosts = std::move(manaCosts);
    m_staminaCosts = std::move(staminaCosts);
    m_abilityCosts = std::move(abilityCosts);
    m_abilityRows = std::move(abilityRows);
    m_categories = std::move(categories);
    m_statLimits = std::move(statLimits);
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

std::optional<float> AbilityData::mana_cost(std::string_view id) const {
    const CostRow* row = find_row(m_tables.manaCosts, m_manaCosts, id);
    return row != nullptr ? std::optional<float>(row->cost) : std::nullopt;
}

std::optional<float> AbilityData::stamina_cost(std::string_view id) const {
    const CostRow* row = find_row(m_tables.staminaCosts, m_staminaCosts, id);
    return row != nullptr ? std::optional<float>(row->cost) : std::nullopt;
}

const AbilityCostRow* AbilityData::ability_cost(std::string_view id) const {
    return find_row(m_tables.abilityCosts, m_abilityCosts, id);
}

const AbilityRow* AbilityData::ability_row(std::string_view id) const {
    return find_row(m_tables.abilityRows, m_abilityRows, id);
}

const StatusCategory* AbilityData::category(std::string_view id) const {
    return find_row(m_tables.statusCategories, m_categories, id);
}

const StatLimit* AbilityData::stat_limit(std::string_view id) const {
    return find_row(m_tables.statMultipliers, m_statLimits, id);
}

std::vector<const ActionSpell*> AbilityData::action_spells(std::string_view action) const {
    std::vector<const ActionSpell*> result;
    if (const auto found = m_actionSpells.find(lower_ascii(action)); found != m_actionSpells.end()) {
        for (const std::size_t index : found->second) {
            result.push_back(&m_tables.actionSpells[index]);
        }
    }
    return result;
}

bool AbilityData::action_tests(std::string_view node) const {
    return m_actionNodes.count(lower_ascii(node)) != 0;
}

std::vector<std::string> AbilityData::action_lists_of(std::string_view node) const {
    const auto found = m_actionNodes.find(lower_ascii(node));
    return found == m_actionNodes.end() ? std::vector<std::string>{} : found->second;
}

const SpellRow* AbilityData::tracking_spell(std::string_view rule) const {
    const auto found = m_trackingSpells.find(lower_ascii(rule));
    return found == m_trackingSpells.end() ? nullptr : &m_tables.spells[found->second];
}

} // namespace soulwarden::backend::gamedata

#include "gamedata/progression_tables.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>

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

bool valid_rank_caps(const ProgressionRow& row) {
    if (!valid_ranks(row)) {
        return false;
    }
    ProgressionRow expansion = row;
    expansion.maxRank = row.expansionMaxRank;
    return row.expansionMaxRank == 0 || valid_ranks(expansion);
}

namespace {

/** @return True when the expansion rows, the entitlements, and the achievements of format 2 fit together (stage 1.1b). */
bool valid_ownership(const ProgressionData& data) {
    std::set<std::string> aliases;
    std::set<std::uint32_t> entitlementBits;
    std::set<std::string> achievementIds;
    std::set<std::uint32_t> achievementBits;
    for (const AchievementRow& achievement : data.achievements) {
        if (achievement.id.empty() || achievement.index >= kAchievementBitLimit || !achievementIds.insert(achievement.id).second
            || !achievementBits.insert(achievement.index).second) {
            return false;
        }
    }
    for (const EntitlementRow& entitlement : data.entitlements) {
        if (entitlement.alias.empty() || entitlement.index >= kEntitlementBitLimit || !aliases.insert(entitlement.alias).second
            || !entitlementBits.insert(entitlement.index).second
            || (!entitlement.achievement.empty() && achievementIds.count(entitlement.achievement) == 0)) {
            return false;
        }
    }
    // A root of a DLC pack gives its children (reward type Entitlement, the aliases with "+" between them): each child is an
    // entitlement of the file (code review of stage 1.1b, finding F6: a root whose child was absent gave no child).
    for (const EntitlementRow& entitlement : data.entitlements) {
        if (entitlement.rewardType != "Entitlement") {
            continue;
        }
        std::size_t start = 0;
        bool any = false;
        while (start <= entitlement.rewards.size()) {
            std::size_t end = entitlement.rewards.find('+', start);
            if (end == std::string::npos) {
                end = entitlement.rewards.size();
            }
            std::string child = entitlement.rewards.substr(start, end - start);
            while (!child.empty() && child.front() == ' ') {
                child.erase(child.begin());
            }
            while (!child.empty() && child.back() == ' ') {
                child.pop_back();
            }
            if (!child.empty()) {
                any = true;
                if (aliases.count(child) == 0) {
                    return false;
                }
            }
            start = end + 1;
        }
        if (!any) {
            return false;
        }
    }
    std::set<std::string> expansionIds;
    for (const ExpansionRow& expansion : data.expansions) {
        if (expansion.id.empty() || !expansionIds.insert(expansion.id).second || expansion.maxLevel == 0
            || expansion.maxLevel > data.levels.size()
            || (!expansion.entitlement.empty() && aliases.count(expansion.entitlement) == 0)) {
            return false;
        }
    }
    return true;
}

} // namespace

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
    // The format first (code review of stage 1.1b, finding F5): format 1 (or no member: a file of stage 1.6) has rows of four
    // values and no expansion; format 2 (stage 1.1b) has rows of five values and the three lists of the ownership.
    std::uint32_t format = 1;
    if (document.contains("format") && (!read_number(document["format"], format) || (format != 1 && format != 2))) {
        return false;
    }
    const bool format2 = format == 2;
    if (format2 != (document.contains("expansions") || document.contains("entitlements") || document.contains("achievements"))) {
        return false;
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
    // The threshold abilities of the attributes (stage 3.5b.2): optional; each entry [value, [ids]] with values that go up.
    if (attributes.contains("abilities")) {
        if (!attributes["abilities"].is_object()) {
            return false;
        }
        for (const auto& [name, list] : attributes["abilities"].items()) {
            if (!list.is_array()) {
                return false;
            }
            std::vector<AttributeThreshold>& thresholds = data.attributeAbilities[name];
            for (const json& entry : list) {
                AttributeThreshold threshold;
                if (!entry.is_array() || entry.size() != 2 || !read_number(entry[0], threshold.value) || !entry[1].is_array()
                    || (!thresholds.empty() && threshold.value <= thresholds.back().value)) {
                    return false;
                }
                for (const json& ability : entry[1]) {
                    if (!ability.is_string() || ability.get<std::string>().empty()) {
                        return false;
                    }
                    threshold.abilities.push_back(ability.get<std::string>());
                }
                thresholds.push_back(std::move(threshold));
            }
        }
    }
    // The mana regeneration (stage 1.3b): three numbers of 0 or more, or none of them (a file from before).
    for (const auto& [member, target] : {std::pair<const char*, float*>{"manaRegenRate", &data.manaRegenRate},
                                         {"manaRegenDelay", &data.manaRegenDelay},
                                         {"manaRegenDelayOnEmpty", &data.manaRegenDelayOnEmpty}}) {
        if (vitals.contains(member)) {
            if (!vitals[member].is_number() || !std::isfinite(vitals[member].get<float>()) || vitals[member].get<float>() < 0.0F) {
                return false;
            }
            *target = vitals[member].get<float>();
        }
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
        // Format 2 (stage 1.1b): a fifth value, the largest rank with the expansion.
        if (!row.is_array() || row.size() != (format2 ? 5U : 4U) || !row[0].is_string() || !row[1].is_string()
            || !read_number(row[2], progression.maxRank) || !row[3].is_string()
            || (format2 && !read_number(row[4], progression.expansionMaxRank))) {
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
        if (!valid_rank_caps(progression)) {
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
    // Stage 1.1b (format 2): the expansions, the entitlements of each account, and the achievements that they give. A
    // file of format 1 has none of them.
    if (format2) {
        if (!document.contains("expansions") || !document.contains("entitlements") || !document.contains("achievements")
            || !document["expansions"].is_array() || !document["entitlements"].is_array() || !document["achievements"].is_array()) {
            return false;
        }
        for (const json& row : document["expansions"]) {
            ExpansionRow expansion;
            if (!row.is_array() || row.size() != 6 || !row[0].is_string() || !read_number(row[1], expansion.maxLevel)
                || !read_number(row[2], expansion.maxCraftGearScore) || !read_number(row[3], expansion.maxEquipGearScore)
                || !read_number(row[4], expansion.maxTradeSkillLevel) || !row[5].is_string()) {
                return false;
            }
            expansion.id = row[0].get<std::string>();
            expansion.entitlement = row[5].get<std::string>();
            data.expansions.push_back(std::move(expansion));
        }
        for (const json& row : document["entitlements"]) {
            EntitlementRow entitlement;
            if (!row.is_array() || row.size() != 5 || !row[0].is_string() || !read_number(row[1], entitlement.index)
                || !row[2].is_string() || !row[3].is_string() || !row[4].is_string()) {
                return false;
            }
            entitlement.alias = row[0].get<std::string>();
            entitlement.rewardType = row[2].get<std::string>();
            entitlement.rewards = row[3].get<std::string>();
            entitlement.achievement = row[4].get<std::string>();
            data.entitlements.push_back(std::move(entitlement));
        }
        for (const json& row : document["achievements"]) {
            AchievementRow achievement;
            if (!row.is_array() || row.size() != 2 || !row[0].is_string() || !read_number(row[1], achievement.index)) {
                return false;
            }
            achievement.id = row[0].get<std::string>();
            data.achievements.push_back(std::move(achievement));
        }
    }
    if (data.levels.empty() || data.levelCap == 0 || data.levelCap > data.levels.size() || !valid_ownership(data)) {
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
    // The owned expansion: the last row whose entitlement is in the list and gives an achievement (the expansion check
    // of the client reads the achievement, image RVA 0x684B340).
    m_owned = static_cast<std::size_t>(-1);
    for (std::size_t position = 0; position < m_data.expansions.size(); ++position) {
        const ExpansionRow& expansion = m_data.expansions[position];
        const EntitlementRow* row = expansion.entitlement.empty() ? nullptr : entitlement(expansion.entitlement);
        if (row != nullptr && !row->achievement.empty() && achievement(row->achievement) != nullptr
            && expansion.maxLevel != 0 && expansion.maxLevel <= m_data.levels.size()) {
            m_owned = position;
        }
    }
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

const ExpansionRow* ProgressionTables::base_expansion() const {
    for (const ExpansionRow& expansion : m_data.expansions) {
        if (expansion.entitlement.empty()) {
            return &expansion;
        }
    }
    return nullptr;
}

std::uint32_t ProgressionTables::max_equip_gear_score() const {
    const ExpansionRow* row = owned_expansion() != nullptr ? owned_expansion() : base_expansion();
    return row != nullptr ? row->maxEquipGearScore : 0;
}

std::uint32_t ProgressionTables::max_craft_gear_score() const {
    const ExpansionRow* row = owned_expansion() != nullptr ? owned_expansion() : base_expansion();
    return row != nullptr ? row->maxCraftGearScore : 0;
}

const EntitlementRow* ProgressionTables::entitlement(std::string_view alias) const {
    for (const EntitlementRow& row : m_data.entitlements) {
        if (row.alias == alias) {
            return &row;
        }
    }
    return nullptr;
}

const AchievementRow* ProgressionTables::achievement(std::string_view id) const {
    for (const AchievementRow& row : m_data.achievements) {
        if (row.id == id) {
            return &row;
        }
    }
    return nullptr;
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

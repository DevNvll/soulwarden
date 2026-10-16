#include "gamedata/loot_tables.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>

#include <nlohmann/json.hpp>

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

/** Largest value of a quantity, a threshold, a roll, and a gear score of the file. */
constexpr std::uint32_t kLargestNumber = 100000000;
/** Largest level of a reward table. */
constexpr std::size_t kLargestLevel = 1000;

bool read_float(const json& value, float& number) {
    if (!value.is_number()) {
        return false;
    }
    number = value.get<float>();
    return std::isfinite(number);
}

bool read_whole(const json& value, std::uint32_t& number) {
    if (!value.is_number_unsigned() && !(value.is_number_integer() && value.get<std::int64_t>() >= 0)) {
        return false;
    }
    const std::uint64_t wide = value.get<std::uint64_t>();
    if (wide > kLargestNumber) {
        return false;
    }
    number = static_cast<std::uint32_t>(wide);
    return true;
}

bool read_integer(const json& value, std::int32_t& number) {
    if (!value.is_number_integer()) {
        return false;
    }
    const std::int64_t wide = value.get<std::int64_t>();
    if (wide < -100000 || wide > 100000) {
        return false;
    }
    number = static_cast<std::int32_t>(wide);
    return true;
}

bool read_text(const json& value, std::string& text) {
    if (!value.is_string()) {
        return false;
    }
    text = value.get<std::string>();
    return true;
}

bool read_entry(const json& value, LootEntry& entry) {
    // [kind, id, threshold, quantity min, quantity max, gear score min, gear score max]
    if (!value.is_array() || value.size() != 7 || !value[0].is_string() || !read_text(value[1], entry.id) || entry.id.empty()
        || !read_whole(value[2], entry.threshold) || !read_whole(value[3], entry.quantityMin)
        || !read_whole(value[4], entry.quantityMax) || !read_whole(value[5], entry.gearScoreMin)
        || !read_whole(value[6], entry.gearScoreMax)) {
        return false;
    }
    const std::string kind = value[0].get<std::string>();
    if (kind == "t") {
        entry.kind = LootEntryKind::table;
    } else if (kind == "b") {
        entry.kind = LootEntryKind::bucket;
    } else if (kind == "i") {
        entry.kind = LootEntryKind::item;
    } else {
        return false;
    }
    return true;
}

bool read_candidate(const json& value, BucketCandidate& candidate) {
    // [item, quantity min, quantity max, odds, match one, [[tag] | [tag, low, high], ...]]
    if (!value.is_array() || value.size() != 6 || !read_text(value[0], candidate.item) || candidate.item.empty()
        || !read_whole(value[1], candidate.quantityMin) || !read_whole(value[2], candidate.quantityMax)
        || !read_float(value[3], candidate.odds) || !value[4].is_boolean() || !value[5].is_array()) {
        return false;
    }
    candidate.matchOne = value[4].get<bool>();
    for (const json& tagValue : value[5]) {
        LootTag tag;
        if (!tagValue.is_array() || tagValue.empty() || !read_text(tagValue[0], tag.name) || tag.name.empty()) {
            return false;
        }
        if (tagValue.size() == 3) {
            tag.numeric = true;
            if (!read_whole(tagValue[1], tag.low) || !read_whole(tagValue[2], tag.high)) {
                return false;
            }
        } else if (tagValue.size() != 1) {
            return false;
        }
        candidate.tags.push_back(std::move(tag));
    }
    return true;
}

} // namespace

std::string lower_text(std::string_view text) {
    std::string result(text);
    for (char& character : result) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return result;
}

bool LootTables::check(LootData& data, std::string& error) {
    const auto fail = [&error](const std::string& text) {
        error = text;
        return false;
    };
    if (data.rewards.empty() || data.rewards.size() > kLargestLevel) {
        return fail("the reward table has no level");
    }
    for (const auto& [name, modifier] : data.modifiers) {
        if (name.empty() || !std::isfinite(modifier.experience) || modifier.experience < 0.0F || !std::isfinite(modifier.coin)
            || modifier.coin < 0.0F || !std::isfinite(modifier.coinChance) || modifier.coinChance < 0.0F
            || modifier.coinChance > 1.0F || !std::isfinite(modifier.lootDrop) || modifier.lootDrop < 0.0F
            || (modifier.progression && (!std::isfinite(*modifier.progression) || *modifier.progression < 0.0F))) {
            return fail("the reward factors of the creature type " + name + " are not valid");
        }
    }
    std::set<std::int32_t> differences;
    for (const LevelDisparity& row : data.disparity) {
        if (!std::isfinite(row.experience) || row.experience < -1.0F || row.experience > 100.0F || row.rewardLevelDelta > kLargestLevel
            || !differences.insert(row.difference).second) {
            return fail("a level difference row is not valid");
        }
    }
    std::sort(data.disparity.begin(), data.disparity.end(),
              [](const LevelDisparity& left, const LevelDisparity& right) { return left.difference < right.difference; });
    if (data.events.find(kKillEvent) == data.events.end()) {
        return fail("the file has no kill event");
    }
    for (const auto& [event, chance] : data.events) {
        if (!std::isfinite(chance) || chance < 0.0F || chance > 1.0F) {
            return fail("the coin chance of the event " + event + " is not valid");
        }
    }
    const CreditData& credit = data.credit;
    if (!std::isfinite(credit.timeLimit) || credit.timeLimit <= 0.0F || !std::isfinite(credit.healthShare)
        || credit.healthShare < 0.0F || credit.healthShare > 1.0F || !std::isfinite(credit.range) || credit.range <= 0.0F
        || credit.receivers == 0 || credit.modifiers.empty()) {
        return fail("the credit data is not valid");
    }
    for (const CreditData::Modifier& modifier : credit.modifiers) {
        if (!std::isfinite(modifier.experience) || modifier.experience < 0.0F || !std::isfinite(modifier.loot) || modifier.loot < 0.0F
            || !std::isfinite(modifier.coin) || modifier.coin < 0.0F) {
            return fail("a credit modifier is not valid");
        }
    }
    for (const auto& level : data.gearScore) {
        for (const auto& [low, high] : level) {
            if (high < low) {
                return fail("a gear score range of a level is not valid");
            }
        }
    }
    // The references: the keys of the tables and of the buckets.
    std::unordered_map<std::string, std::uint32_t> tableKeys;
    for (std::uint32_t index = 0; index < data.tables.size(); ++index) {
        if (data.tables[index].id.empty() || !tableKeys.emplace(lower_text(data.tables[index].id), index).second) {
            return fail("a loot table has no id, or its id is in the file two times");
        }
    }
    // A chest root names a table of the file (stage 3.5b.1).
    for (const std::string& root : data.chestRoots) {
        if (tableKeys.find(lower_text(root)) == tableKeys.end()) {
            return fail("the chest root " + root + " names a table that the file does not have");
        }
    }
    std::unordered_map<std::string, std::uint32_t> bucketKeys;
    for (std::uint32_t index = 0; index < data.buckets.size(); ++index) {
        if (data.buckets[index].id.empty() || !bucketKeys.emplace(lower_text(data.buckets[index].id), index).second) {
            return fail("a loot bucket has no id, or its id is in the file two times");
        }
        for (const BucketCandidate& candidate : data.buckets[index].candidates) {
            if (candidate.quantityMax < candidate.quantityMin || !std::isfinite(candidate.odds) || candidate.odds < 0.0F) {
                return fail("a candidate of the loot bucket " + data.buckets[index].id + " is not valid");
            }
            for (const LootTag& tag : candidate.tags) {
                if (tag.numeric && tag.high < tag.low) {
                    return fail("a tag of the loot bucket " + data.buckets[index].id + " has a range whose end is below its start");
                }
            }
        }
    }
    for (LootTable& table : data.tables) {
        for (LootEntry& entry : table.entries) {
            if (entry.quantityMax < entry.quantityMin || entry.gearScoreMax < entry.gearScoreMin) {
                return fail("an entry of the loot table " + table.id + " has a range whose end is below its start");
            }
            if (entry.kind == LootEntryKind::table || entry.kind == LootEntryKind::bucket) {
                const auto& keys = entry.kind == LootEntryKind::table ? tableKeys : bucketKeys;
                const auto found = keys.find(lower_text(entry.id));
                if (found == keys.end()) {
                    return fail("the loot table " + table.id + " names " + entry.id + ", which the file does not have");
                }
                entry.index = found->second;
            }
        }
        // The client sorts the entries by their threshold (image RVA 0x366F799).
        std::stable_sort(table.entries.begin(), table.entries.end(),
                         [](const LootEntry& left, const LootEntry& right) { return left.threshold < right.threshold; });
    }
    for (const auto& [name, base] : data.vitals) {
        if (base >= data.bases.size()) {
            return fail("the vitals row " + name + " names a base row that the file does not have");
        }
    }
    for (const LootVitals& base : data.bases) {
        if (!std::isfinite(base.dropChance) || base.dropChance < 0.0F || (base.table && *base.table >= data.tables.size())) {
            return fail("the loot data of the vitals row " + base.id + " is not valid");
        }
        // Each reference of a base row resolves (code review of stage 3.5a, finding F2): a file
        // with a part that is not there is not complete, and the backend does not use it.
        if (!base.creatureType.empty() && data.modifiers.find(base.creatureType) == data.modifiers.end()) {
            return fail("the vitals row " + base.id + " names the creature type " + base.creatureType + ", which the file does not have");
        }
        if (!base.event.empty() && data.events.find(lower_text(base.event)) == data.events.end()) {
            return fail("the vitals row " + base.id + " names the kill event " + base.event + ", which the file does not have");
        }
    }
    // A credit modifier for each number of characters that can get a reward.
    if (credit.modifiers.size() < credit.receivers) {
        return fail("the credit data has " + std::to_string(credit.modifiers.size()) + " modifiers for " +
                    std::to_string(credit.receivers) + " characters");
    }
    return true;
}

void LootTables::build_keys() {
    m_tableKeys.clear();
    for (std::uint32_t index = 0; index < m_data.tables.size(); ++index) {
        m_tableKeys.emplace(lower_text(m_data.tables[index].id), index);
    }
}

bool LootTables::set(LootData data) {
    std::string error;
    if (!check(data, error)) {
        return false;
    }
    m_data = std::move(data);
    m_error.clear();
    build_keys();
    return true;
}

bool LootTables::load(const std::string& path) {
    const auto fail = [this](const std::string& text) {
        m_data = LootData{};
        m_tableKeys.clear();
        m_error = text;
        return false;
    };
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return fail("the file is not there");
    }
    const json document = json::parse(input, nullptr, false);
    if (!document.is_object()) {
        return fail("the file is not a JSON object");
    }
    for (const char* member : {"format", "rewards", "modifiers", "disparity", "events", "credit", "gearScore", "lootCeiling",
                               "bases", "vitals", "tables", "buckets"}) {
        if (!document.contains(member)) {
            return fail(std::string("the file has no member ") + member);
        }
    }
    // Format 1 (stage 3.5a), format 2 (stage 3.5b.1: the container floor, the chest roots, the loot limits).
    if (document["format"] != 1 && document["format"] != 2) {
        return fail("the file has a format that this backend does not read");
    }
    const bool chests = document["format"] == 2;
    if (chests) {
        for (const char* member : {"containerFloor", "chestRoots", "limits"}) {
            if (!document.contains(member)) {
                return fail(std::string("the file of format 2 has no member ") + member);
            }
        }
    }
    LootData data;
    // The rewards: [level, experience, coin, progression], the levels from 1 up by 1.
    if (!document["rewards"].is_array()) {
        return fail("the rewards are not a list");
    }
    for (const json& row : document["rewards"]) {
        std::uint32_t level = 0;
        KillReward reward;
        if (!row.is_array() || row.size() != 4 || !read_whole(row[0], level) || !read_whole(row[1], reward.experience)
            || !read_whole(row[2], reward.coin) || !read_whole(row[3], reward.progression)) {
            return fail("a reward row does not have the form of the tool");
        }
        if (level != data.rewards.size() + 1) {
            return fail("the levels of the reward table do not start at 1 and go up by 1");
        }
        data.rewards.push_back(reward);
    }
    // The creature types: {"<type>": [experience, coin, coin chance, loot drop, progression | null]}.
    if (!document["modifiers"].is_object()) {
        return fail("the reward factors are not an object");
    }
    for (const auto& [name, row] : document["modifiers"].items()) {
        RewardModifier modifier;
        if (!row.is_array() || row.size() != 5 || !read_float(row[0], modifier.experience) || !read_float(row[1], modifier.coin)
            || !read_float(row[2], modifier.coinChance) || !read_float(row[3], modifier.lootDrop)
            || !(row[4].is_null() || row[4].is_number())) {
            return fail("the reward factors of the creature type " + name + " do not have the form of the tool");
        }
        if (row[4].is_number()) {
            float progression = 0.0F;
            if (!read_float(row[4], progression)) {
                return fail("the progression amount of the creature type " + name + " is not a finite number");
            }
            modifier.progression = progression;
        }
        data.modifiers.emplace(name, modifier);
    }
    // The level differences: [difference, experience, reward level delta].
    if (!document["disparity"].is_array()) {
        return fail("the level differences are not a list");
    }
    for (const json& row : document["disparity"]) {
        LevelDisparity disparity;
        if (!row.is_array() || row.size() != 3 || !read_integer(row[0], disparity.difference)
            || !read_float(row[1], disparity.experience) || !read_whole(row[2], disparity.rewardLevelDelta)) {
            return fail("a level difference row does not have the form of the tool");
        }
        data.disparity.push_back(disparity);
    }
    if (!document["events"].is_object()) {
        return fail("the events are not an object");
    }
    for (const auto& [name, chance] : document["events"].items()) {
        float value = 0.0F;
        if (!read_float(chance, value)) {
            return fail("the coin chance of the event " + name + " is not a number");
        }
        data.events.emplace(lower_text(name), value);
    }
    // The credit data.
    const json& credit = document["credit"];
    std::uint32_t timeLimit = 0;
    std::uint32_t range = 0;
    if (!credit.is_object() || !credit.contains("timeLimit") || !credit.contains("healthShare") || !credit.contains("range")
        || !credit.contains("receivers") || !credit.contains("modifiers") || !read_whole(credit["timeLimit"], timeLimit)
        || !read_float(credit["healthShare"], data.credit.healthShare) || !read_whole(credit["range"], range)
        || !read_whole(credit["receivers"], data.credit.receivers) || !credit["modifiers"].is_array()) {
        return fail("the credit data does not have the form of the tool");
    }
    data.credit.timeLimit = static_cast<float>(timeLimit);
    data.credit.range = static_cast<float>(range);
    for (const json& row : credit["modifiers"]) {
        CreditData::Modifier modifier;
        if (!row.is_array() || row.size() != 3 || !read_float(row[0], modifier.experience) || !read_float(row[1], modifier.loot)
            || !read_float(row[2], modifier.coin)) {
            return fail("a credit modifier does not have the form of the tool");
        }
        data.credit.modifiers.push_back(modifier);
    }
    // The gear score ranges of each level: [t1 min, t1 max, ..., t5 min, t5 max].
    if (!document["gearScore"].is_array()) {
        return fail("the gear score ranges are not a list");
    }
    for (const json& row : document["gearScore"]) {
        if (!row.is_array() || row.size() != 2 * kGearScoreTiers) {
            return fail("a gear score row does not have the form of the tool");
        }
        std::array<std::pair<std::uint32_t, std::uint32_t>, kGearScoreTiers> level{};
        for (std::size_t tier = 0; tier < kGearScoreTiers; ++tier) {
            if (!read_whole(row[2 * tier], level[tier].first) || !read_whole(row[2 * tier + 1], level[tier].second)) {
                return fail("a gear score of a level is not a number");
            }
        }
        data.gearScore.push_back(level);
    }
    if (!document["lootCeiling"].is_array()) {
        return fail("the loot ceilings are not a list");
    }
    for (const json& row : document["lootCeiling"]) {
        std::uint32_t level = 0;
        std::uint32_t ceiling = 0;
        if (!row.is_array() || row.size() != 2 || !read_whole(row[0], level) || !read_whole(row[1], ceiling)) {
            return fail("a loot ceiling row does not have the form of the tool");
        }
        data.lootCeiling[level] = ceiling;
    }
    if (chests) {
        if (!document["containerFloor"].is_array() || !document["chestRoots"].is_array() || !document["limits"].is_object()) {
            return fail("the container floors, the chest roots, or the loot limits do not have the form of the tool");
        }
        for (const json& row : document["containerFloor"]) {
            std::uint32_t level = 0;
            std::uint32_t floor = 0;
            if (!row.is_array() || row.size() != 2 || !read_whole(row[0], level) || !read_whole(row[1], floor)) {
                return fail("a container floor row does not have the form of the tool");
            }
            data.containerFloor[level] = floor;
        }
        for (const json& root : document["chestRoots"]) {
            if (!root.is_string() || root.get<std::string>().empty()) {
                return fail("a chest root is not a text");
            }
            data.chestRoots.push_back(root.get<std::string>());
        }
        data.limits = document["limits"].size();
    }
    // The tables first: a base row names a table by its id.
    if (!document["tables"].is_object()) {
        return fail("the loot tables are not an object");
    }
    for (const auto& [name, row] : document["tables"].items()) {
        LootTable table;
        table.id = name;
        std::uint32_t mode = 0;
        std::uint32_t bonus = 0;
        if (!row.is_array() || row.size() != 8 || !read_whole(row[0], mode) || mode > 1 || !read_whole(row[1], bonus) || bonus > 2
            || !read_whole(row[2], table.maxRoll) || !row[3].is_array() || !row[4].is_boolean() || !row[5].is_boolean()
            || !read_integer(row[6], table.gearScoreBonus) || !row[7].is_array()) {
            return fail("the loot table " + name + " does not have the form of the tool");
        }
        table.all = mode == 1;
        table.bonus = static_cast<RollBonus>(bonus);
        for (const json& condition : row[3]) {
            std::string text;
            if (!read_text(condition, text) || text.empty()) {
                return fail("a condition of the loot table " + name + " is not a text");
            }
            table.conditions.push_back(std::move(text));
        }
        table.conditionOverridesRoll = row[4].get<bool>();
        table.useLevelGearScore = row[5].get<bool>();
        for (const json& value : row[7]) {
            LootEntry entry;
            if (!read_entry(value, entry)) {
                return fail("an entry of the loot table " + name + " does not have the form of the tool");
            }
            table.entries.push_back(std::move(entry));
        }
        data.tables.push_back(std::move(table));
    }
    std::unordered_map<std::string, std::uint32_t> tableKeys;
    for (std::uint32_t index = 0; index < data.tables.size(); ++index) {
        tableKeys.emplace(lower_text(data.tables[index].id), index);
    }
    if (!document["buckets"].is_object()) {
        return fail("the loot buckets are not an object");
    }
    for (const auto& [name, rows] : document["buckets"].items()) {
        LootBucket bucket;
        bucket.id = name;
        if (!rows.is_array()) {
            return fail("the loot bucket " + name + " is not a list");
        }
        for (const json& value : rows) {
            BucketCandidate candidate;
            if (!read_candidate(value, candidate)) {
                return fail("a candidate of the loot bucket " + name + " does not have the form of the tool");
            }
            bucket.candidates.push_back(std::move(candidate));
        }
        data.buckets.push_back(std::move(bucket));
    }
    // The base rows: [id, creature type, can drop loot, drop chance, ignore the modifier, table, [tags], no experience
    // and no coin, kill event].
    if (!document["bases"].is_array()) {
        return fail("the base vitals rows are not a list");
    }
    for (const json& row : document["bases"]) {
        LootVitals base;
        std::string table;
        if (!row.is_array() || row.size() != 9 || !read_text(row[0], base.id) || !read_text(row[1], base.creatureType)
            || !row[2].is_boolean() || !read_float(row[3], base.dropChance) || !row[4].is_boolean() || !read_text(row[5], table)
            || !row[6].is_array() || !row[7].is_boolean() || !read_text(row[8], base.event)) {
            return fail("a base vitals row does not have the form of the tool");
        }
        base.canDropLoot = row[2].get<bool>();
        base.ignoreDropModifier = row[4].get<bool>();
        base.noExperienceNoCoin = row[7].get<bool>();
        base.event = lower_text(base.event);
        for (const json& tag : row[6]) {
            std::string text;
            if (!read_text(tag, text)) {
                return fail("a loot tag of the vitals row " + base.id + " is not a text");
            }
            base.tags.push_back(std::move(text));
        }
        if (!table.empty()) {
            const auto found = tableKeys.find(lower_text(table));
            if (found == tableKeys.end()) {
                return fail("the vitals row " + base.id + " names the loot table " + table + ", which the file does not have");
            }
            base.table = found->second;
        }
        data.bases.push_back(std::move(base));
    }
    if (!document["vitals"].is_object()) {
        return fail("the vitals rows are not an object");
    }
    for (const auto& [name, base] : document["vitals"].items()) {
        std::uint32_t index = 0;
        if (!read_whole(base, index)) {
            return fail("the vitals row " + name + " has no base row index");
        }
        if (!data.vitals.emplace(lower_text(name), index).second) {
            return fail("the vitals row " + name + " is in the file two times");
        }
    }
    std::string error;
    if (!check(data, error)) {
        return fail(error);
    }
    m_data = std::move(data);
    m_error.clear();
    build_keys();
    return true;
}

const KillReward* LootTables::reward(std::uint32_t level) const {
    return level >= 1 && level <= m_data.rewards.size() ? &m_data.rewards[level - 1] : nullptr;
}

const RewardModifier* LootTables::modifier(std::string_view creatureType) const {
    const auto found = m_data.modifiers.find(std::string(creatureType));
    return found == m_data.modifiers.end() ? nullptr : &found->second;
}

const LevelDisparity* LootTables::disparity(std::int64_t difference) const {
    for (const LevelDisparity& row : m_data.disparity) {
        if (row.difference == difference) {
            return &row;
        }
    }
    return nullptr;
}

std::pair<std::int32_t, std::int32_t> LootTables::disparity_range() const {
    if (m_data.disparity.empty()) {
        return {0, 0};
    }
    return {m_data.disparity.front().difference, m_data.disparity.back().difference};
}

float LootTables::coin_chance(std::string_view event) const {
    const auto found = m_data.events.find(event.empty() ? std::string(kKillEvent) : lower_text(event));
    if (found != m_data.events.end()) {
        return found->second;
    }
    return m_data.events.at(kKillEvent);
}

const LootVitals* LootTables::vitals_of(std::string_view vitalsId) const {
    const auto found = m_data.vitals.find(lower_text(vitalsId));
    return found == m_data.vitals.end() ? nullptr : &m_data.bases[found->second];
}

std::optional<std::pair<std::uint32_t, std::uint32_t>> LootTables::gear_score(std::uint32_t level, std::uint32_t tier) const {
    if (level == 0 || level > m_data.gearScore.size() || tier == 0 || tier > kGearScoreTiers) {
        return std::nullopt;
    }
    return m_data.gearScore[level - 1][tier - 1];
}

std::optional<std::uint32_t> LootTables::loot_ceiling(std::uint32_t level) const {
    const auto found = m_data.lootCeiling.find(level);
    return found == m_data.lootCeiling.end() ? std::nullopt : std::optional<std::uint32_t>(found->second);
}

std::optional<std::uint32_t> LootTables::container_floor(std::uint32_t level) const {
    const auto found = m_data.containerFloor.find(level);
    return found == m_data.containerFloor.end() ? std::nullopt : std::optional<std::uint32_t>(found->second);
}

std::optional<std::uint32_t> LootTables::table_index(std::string_view id) const {
    const auto found = m_tableKeys.find(lower_text(id));
    return found == m_tableKeys.end() ? std::nullopt : std::optional<std::uint32_t>(found->second);
}

const LootTable* LootTables::table(std::string_view id) const {
    const auto found = m_tableKeys.find(lower_text(id));
    return found == m_tableKeys.end() ? nullptr : &m_data.tables[found->second];
}

} // namespace soulwarden::backend::gamedata

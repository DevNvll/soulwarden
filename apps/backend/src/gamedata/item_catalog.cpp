#include "gamedata/item_catalog.h"

#include <array>
#include <cmath>
#include <fstream>

#include <nlohmann/json.hpp>

#include "gamedata/progression_tables.h"
#include "server/hub/wire.h"

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

/** Columns of one item of the file, in this sequence (tools/gamedata/item_catalog.py). */
constexpr std::array<const char*, 17> kColumns{"id",         "name",         "type",         "class",
                                               "tier",       "weight",       "maxStack",     "durability",
                                               "minGearScore", "maxGearScore", "gearScoreOverride", "bindOnPickup",
                                               "nonRemovable", "notDroppable", "maxCraftUpgradeGS", "armorAppearanceM",
                                               "armorAppearanceF"};
/** A file of the tool from before stage 2.2 (format 1) has the first 12 columns only (no removal protection). */
constexpr std::size_t kColumnsBefore22 = 12;
/** A file of format 2 (stage 2.2) has the first 14 columns (no upgrade gear score: stage B0 adds it, format 3). */
constexpr std::size_t kColumnsBeforeB0 = 14;
/** A file of format 3 or 4 has the first 15 columns (no armor appearance: stage U4 adds the two columns, format 5). */
constexpr std::size_t kColumnsBeforeU4 = 15;

std::string lower_ascii(std::string_view text) {
    std::string lower(text);
    for (char& letter : lower) {
        if (letter >= 'A' && letter <= 'Z') {
            letter = static_cast<char>(letter + ('a' - 'A'));
        }
    }
    return lower;
}

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

} // namespace

std::uint32_t item_id_crc(std::string_view id) {
    const std::string lower = lower_ascii(id);
    return server::hub::crc32({reinterpret_cast<const std::uint8_t*>(lower.data()), lower.size()});
}

bool ItemCatalog::load(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    const json document = json::parse(input, nullptr, false);
    if (!document.is_object() || !document.contains("columns") || !document.contains("items")
        || !document["columns"].is_array() || !document["items"].is_array()) {
        return false;
    }
    // The file must have the columns that this code reads, in the same sequence: all of them (format 5, stage U4), the 15 columns of a file
    // of format 3 or 4 (no armor appearance), the 14 columns of a file of stage 2.2 (format 2: no upgrade gear score), or the 12 columns of a
    // file from before stage 2.2 (format 1: no removal protection).
    // The format of the file selects the layout (code review of stage 2.2, finding WT-R4): a file with fewer columns than its format is not
    // complete and does not load.
    // Format 4 (stage B4): the columns of format 3 and the member trinkets (the stat lines of the jewelry).
    // Format 5 (stage U4): the members of format 4 and two more columns, the armor appearance names of each item.
    std::uint32_t format = 1;
    if (document.contains("format") && (!read_number(document["format"], format) || format < 1 || format > 5)) {
        return false;
    }
    const json& columns = document["columns"];
    if (columns.size()
        != (format >= 5   ? kColumns.size()
            : format >= 3 ? kColumnsBeforeU4
            : format == 2 ? kColumnsBeforeB0
                          : kColumnsBefore22)) {
        return false;
    }
    for (std::size_t column = 0; column < columns.size(); ++column) {
        if (!columns[column].is_string() || columns[column].get<std::string>() != kColumns[column]) {
            return false;
        }
    }
    const bool protections = columns.size() >= kColumnsBeforeB0;
    const bool upgrades = columns.size() >= kColumnsBeforeU4;
    const bool appearances = columns.size() == kColumns.size();
    std::vector<ItemDefinition> items;
    items.reserve(document["items"].size());
    for (const json& row : document["items"]) {
        if (!row.is_array() || row.size() != columns.size() || !row[0].is_string() || !row[1].is_string()
            || !row[2].is_string() || !row[3].is_string() || !row[11].is_boolean()
            || (protections && (!row[12].is_boolean() || !row[13].is_boolean()))
            || (appearances && (!row[15].is_string() || !row[16].is_string()))) {
            return false;
        }
        ItemDefinition item;
        item.id = row[0].get<std::string>();
        item.nameKey = row[1].get<std::string>();
        item.type = row[2].get<std::string>();
        item.itemClass = row[3].get<std::string>();
        if (!read_number(row[4], item.tier) || !read_number(row[5], item.weight) || !read_number(row[6], item.maxStack)
            || !read_number(row[7], item.durability) || !read_number(row[8], item.minGearScore)
            || !read_number(row[9], item.maxGearScore) || !read_number(row[10], item.gearScoreOverride)) {
            return false;
        }
        item.bindOnPickup = row[11].get<bool>();
        if (protections) {
            item.nonRemovable = row[12].get<bool>();
            item.notDroppable = row[13].get<bool>();
        }
        if (upgrades && !read_number(row[14], item.maxCraftUpgradeGS)) {
            return false;
        }
        // The value of the client is the AZ::Crc32 of the lower-case name (25 of 25 entries of the create requests of the client).
        if (appearances) {
            const std::string& male = row[15].get_ref<const std::string&>();
            const std::string& female = row[16].get_ref<const std::string&>();
            item.armorAppearanceM = male.empty() ? 0 : item_id_crc(male);
            item.armorAppearanceF = female.empty() ? 0 : item_id_crc(female);
        }
        items.push_back(std::move(item));
    }
    if (items.empty()) {
        return false;
    }
    // The count of the file is the length of the list of the client with entry 0.
    if (document.contains("count") && document["count"].is_number_unsigned()
        && document["count"].get<std::uint64_t>() != items.size() + 1) {
        return false;
    }
    // The items of the backstories: {"<backstory id>": [[item id, quantity, gear score], ...]}.
    // A file of an earlier tool does not have the member: the catalog then has no backstory.
    std::unordered_map<std::string, std::vector<BackstoryItem>> backstories;
    if (document.contains("backstories")) {
        const json& member = document["backstories"];
        if (!member.is_object()) {
            return false;
        }
        for (const auto& [name, list] : member.items()) {
            if (!list.is_array()) {
                return false;
            }
            std::vector<BackstoryItem> entries;
            for (const json& row : list) {
                BackstoryItem entry;
                if (!row.is_array() || row.size() != 3 || !row[0].is_string() || !read_number(row[1], entry.quantity)
                    || !read_number(row[2], entry.gearScore)) {
                    return false;
                }
                entry.itemId = row[0].get<std::string>();
                entries.push_back(std::move(entry));
            }
            backstories.emplace(lower_ascii(name), std::move(entries));
        }
    }
    // The backstories of the character creation: ["<backstory id>", ...]. A file of an earlier
    // tool does not have the member: the catalog then has no such list.
    std::vector<std::string> creation;
    const bool hasCreation = document.contains("creationBackstories");
    if (hasCreation) {
        const json& member = document["creationBackstories"];
        if (!member.is_array()) {
            return false;
        }
        for (const json& name : member) {
            if (!name.is_string()) {
                return false;
            }
            creation.push_back(name.get<std::string>());
        }
    }
    // The artifact objectives (format 3, stage B0): [[item id, objective id, achievement id, achievement index], ...]. A file of an
    // earlier format has none: the client then shows the perks 2 to 5 of an artifact locked (its achievement bit is not known).
    std::vector<ArtifactObjective> objectives;
    // A file of format 3 has the member (code review of stage B0, finding B0-R3): a file with no objectives would give artifacts whose perks
    // 2 to 5 the client shows locked.
    if (format >= 3 && !document.contains("artifactObjectives")) {
        return false;
    }
    if (document.contains("artifactObjectives")) {
        const json& member = document["artifactObjectives"];
        if (!member.is_array()) {
            return false;
        }
        std::unordered_set<std::string> ids;
        for (const ItemDefinition& item : items) {
            ids.insert(lower_ascii(item.id));
        }
        for (const json& row : member) {
            ArtifactObjective objective;
            if (!row.is_array() || row.size() != 4 || !row[0].is_string() || !row[1].is_string() || !row[2].is_string()
                || !read_number(row[3], objective.achievementIndex)) {
                return false;
            }
            objective.itemId = row[0].get<std::string>();
            objective.objectiveId = row[1].get<std::string>();
            objective.achievementId = row[2].get<std::string>();
            // An objective of an item that the list does not have is a file that does not fit together.
            if (objective.achievementId.empty() || ids.count(lower_ascii(objective.itemId)) == 0) {
                return false;
            }
            // The bit of the achievement is in the bit vector of the client (code review of stage B0, finding B0-R4: the limit of the
            // achievement state, kAchievementBitLimit, as for the progression and the interaction data).
            if (objective.achievementIndex >= kAchievementBitLimit) {
                return false;
            }
            objectives.push_back(std::move(objective));
        }
        // One objective for each item and for each objective id, and one bit for each achievement and one achievement for each bit (code
        // review of stage B0, finding B0-R5: two rows of one achievement with two bits loaded, and the first one won).
        std::unordered_map<std::string, std::uint32_t> bitOf;
        std::unordered_map<std::uint32_t, std::string> achievementOf;
        std::unordered_set<std::string> objectiveItems;
        std::unordered_set<std::string> objectiveIds;
        for (const ArtifactObjective& objective : objectives) {
            if (!objectiveItems.insert(lower_ascii(objective.itemId)).second || !objectiveIds.insert(lower_ascii(objective.objectiveId)).second) {
                return false;
            }
            const auto bit = bitOf.emplace(objective.achievementId, objective.achievementIndex);
            const auto achievement = achievementOf.emplace(objective.achievementIndex, objective.achievementId);
            if (bit.first->second != objective.achievementIndex || achievement.first->second != objective.achievementId) {
                return false;
            }
        }
    }
    // The trinket stat lines (format 4, stage B4): [[item id, TrinketBonus, ScaleHealthPerGS, ScaleDamagePerGS, ScaleCDRPerGS], ...]. A file
    // of format 4 has the member (the rule of finding B0-R3: a file with no lines would give jewelry with no line); a file of an earlier format
    // has none (the jewelry then gives no line). Each value is a finite number of 0 or more, each item is an item of the list, one row for
    // each item.
    std::unordered_map<std::string, TrinketStats> trinkets;
    if (format >= 4 && !document.contains("trinkets")) {
        return false;
    }
    if (document.contains("trinkets")) {
        const json& member = document["trinkets"];
        if (!member.is_array()) {
            return false;
        }
        std::unordered_set<std::string> ids;
        for (const ItemDefinition& item : items) {
            ids.insert(lower_ascii(item.id));
        }
        for (const json& row : member) {
            if (!row.is_array() || row.size() != 5 || !row[0].is_string()) {
                return false;
            }
            std::array<float, 4> values{};
            for (std::size_t index = 0; index < values.size(); ++index) {
                if (!row[index + 1].is_number()) {
                    return false;
                }
                values[index] = row[index + 1].get<float>();
                if (!std::isfinite(values[index]) || values[index] < 0.0F) {
                    return false;
                }
            }
            const std::string id = lower_ascii(row[0].get<std::string>());
            if (ids.count(id) == 0 || !trinkets.emplace(id, TrinketStats{values[0], values[1], values[2], values[3]}).second) {
                return false;
            }
        }
    }
    set(std::move(items));
    m_hasArmorAppearances = appearances;
    set_artifact_objectives(std::move(objectives));
    m_trinkets = std::move(trinkets);
    m_backstories = std::move(backstories);
    m_creationBackstories.clear();
    m_hasCreationBackstories = false;
    if (hasCreation) {
        set_creation_backstories(creation);
    }
    return true;
}

const TrinketStats* ItemCatalog::trinket(std::string_view itemId) const {
    const auto found = m_trinkets.find(lower_ascii(itemId));
    return found == m_trinkets.end() ? nullptr : &found->second;
}

void ItemCatalog::set_trinkets(std::unordered_map<std::string, TrinketStats> trinkets) {
    m_trinkets = std::move(trinkets);
}

void ItemCatalog::set(std::vector<ItemDefinition> items) {
    m_items = std::move(items);
    m_hasArmorAppearances = true;
    build_keys();
}

void ItemCatalog::build_keys() {
    m_byId.clear();
    m_byId.reserve(m_items.size());
    for (std::size_t position = 0; position < m_items.size(); ++position) {
        m_items[position].index = static_cast<std::uint32_t>(position + 1);
        // The first row with an id keeps it: the client looks an item up by the CRC of its id.
        m_byId.emplace(lower_ascii(m_items[position].id), position);
    }
    m_searchTexts.clear();
    m_searchTexts.reserve(m_items.size());
    for (const ItemDefinition& item : m_items) {
        m_searchTexts.push_back(lower_ascii(item.id) + "\n" + lower_ascii(item.type) + "\n" + lower_ascii(item.itemClass));
    }
}

std::vector<const ItemDefinition*> ItemCatalog::search(std::string_view text,
                                                       std::size_t offset,
                                                       std::size_t limit,
                                                       std::size_t* total) const {
    std::vector<std::string> words;
    const std::string lower = lower_ascii(text);
    std::size_t start = 0;
    while (start < lower.size()) {
        const std::size_t end = std::min(lower.find(' ', start), lower.size());
        if (end > start) {
            words.push_back(lower.substr(start, end - start));
        }
        start = end + 1;
    }
    std::vector<const ItemDefinition*> found;
    std::size_t count = 0;
    for (std::size_t position = 0; position < m_items.size(); ++position) {
        if (m_items[position].type.empty()) {
            continue;
        }
        const std::string& candidate = m_searchTexts[position];
        bool match = true;
        for (const std::string& word : words) {
            if (candidate.find(word) == std::string::npos) {
                match = false;
                break;
            }
        }
        if (!match) {
            continue;
        }
        if (count >= offset && found.size() < limit) {
            found.push_back(&m_items[position]);
        }
        ++count;
    }
    if (total != nullptr) {
        *total = count;
    }
    return found;
}

const ItemDefinition* ItemCatalog::find(std::string_view id) const {
    const auto found = m_byId.find(lower_ascii(id));
    return found == m_byId.end() ? nullptr : &m_items[found->second];
}

const std::vector<BackstoryItem>* ItemCatalog::backstory_items(std::string_view backstory) const {
    const auto found = m_backstories.find(lower_ascii(backstory));
    return found == m_backstories.end() ? nullptr : &found->second;
}

void ItemCatalog::set_backstory(std::string_view backstory, std::vector<BackstoryItem> items) {
    m_backstories[lower_ascii(backstory)] = std::move(items);
}

bool ItemCatalog::creation_backstory(std::string_view backstory) const {
    return m_creationBackstories.count(lower_ascii(backstory)) != 0;
}

void ItemCatalog::set_creation_backstories(const std::vector<std::string>& backstories) {
    m_creationBackstories.clear();
    for (const std::string& name : backstories) {
        m_creationBackstories.insert(lower_ascii(name));
    }
    m_hasCreationBackstories = true;
}

void ItemCatalog::set_artifact_objectives(std::vector<ArtifactObjective> objectives) {
    m_objectives = std::move(objectives);
    m_objectiveByItem.clear();
    m_objectiveByAchievement.clear();
    for (std::size_t position = 0; position < m_objectives.size(); ++position) {
        m_objectiveByItem.emplace(lower_ascii(m_objectives[position].itemId), position);
        m_objectiveByAchievement.emplace(m_objectives[position].achievementId, position);
    }
}

const ArtifactObjective* ItemCatalog::artifact_objective(std::string_view itemId) const {
    const auto found = m_objectiveByItem.find(lower_ascii(itemId));
    return found == m_objectiveByItem.end() ? nullptr : &m_objectives[found->second];
}

std::optional<std::uint32_t> ItemCatalog::objective_achievement_index(std::string_view achievementId) const {
    const auto found = m_objectiveByAchievement.find(std::string(achievementId));
    return found == m_objectiveByAchievement.end() ? std::nullopt : std::optional(m_objectives[found->second].achievementIndex);
}

const ItemDefinition* ItemCatalog::by_index(std::uint32_t index) const {
    return index == 0 || index > m_items.size() ? nullptr : &m_items[index - 1];
}

} // namespace soulwarden::backend::gamedata

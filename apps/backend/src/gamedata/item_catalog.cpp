#include "gamedata/item_catalog.h"

#include <array>
#include <fstream>

#include <nlohmann/json.hpp>

#include "server/hub/wire.h"

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

/** Columns of one item of the file, in this sequence (tools/gamedata/item_catalog.py). */
constexpr std::array<const char*, 14> kColumns{"id",         "name",         "type",         "class",
                                               "tier",       "weight",       "maxStack",     "durability",
                                               "minGearScore", "maxGearScore", "gearScoreOverride", "bindOnPickup",
                                               "nonRemovable", "notDroppable"};
/** A file of the tool from before stage 2.2 has the first 12 columns only (no removal protection). */
constexpr std::size_t kColumnsBefore22 = 12;

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
    // The file must have the columns that this code reads, in the same sequence: all of them (format 2), or the 12 columns of a file from
    // before stage 2.2 (format 1: no removal protection). The format of the file selects the layout (code review of stage 2.2, finding
    // WT-R4): a format 2 file with no protection columns is not complete and does not load.
    std::uint32_t format = 1;
    if (document.contains("format") && (!read_number(document["format"], format) || (format != 1 && format != 2))) {
        return false;
    }
    const json& columns = document["columns"];
    if (columns.size() != (format == 2 ? kColumns.size() : kColumnsBefore22)) {
        return false;
    }
    for (std::size_t column = 0; column < columns.size(); ++column) {
        if (!columns[column].is_string() || columns[column].get<std::string>() != kColumns[column]) {
            return false;
        }
    }
    const bool protections = columns.size() == kColumns.size();
    std::vector<ItemDefinition> items;
    items.reserve(document["items"].size());
    for (const json& row : document["items"]) {
        if (!row.is_array() || row.size() != columns.size() || !row[0].is_string() || !row[1].is_string()
            || !row[2].is_string() || !row[3].is_string() || !row[11].is_boolean()
            || (protections && (!row[12].is_boolean() || !row[13].is_boolean()))) {
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
    set(std::move(items));
    m_backstories = std::move(backstories);
    m_creationBackstories.clear();
    m_hasCreationBackstories = false;
    if (hasCreation) {
        set_creation_backstories(creation);
    }
    return true;
}

void ItemCatalog::set(std::vector<ItemDefinition> items) {
    m_items = std::move(items);
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

const ItemDefinition* ItemCatalog::by_index(std::uint32_t index) const {
    return index == 0 || index > m_items.size() ? nullptr : &m_items[index - 1];
}

} // namespace soulwarden::backend::gamedata

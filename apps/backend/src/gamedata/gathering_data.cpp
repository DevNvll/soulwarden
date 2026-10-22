#include "gamedata/gathering_data.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

#include <mbedtls/sha256.h>
#include <nlohmann/json.hpp>

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

/** The form of the file that this code reads (tools/gamedata/gathering.py). */
constexpr std::uint64_t kFormat = 1;
constexpr std::uint32_t kRegionFormat = 1;
/** The action of a gather (uidatabase.uidb: GatherResource). */
constexpr std::uint64_t kGatherAction = 14;
/** The limits of the values of a row: the work amount (seconds) and a respawn bound (a week). */
constexpr double kLongestWork = 3600.0;
constexpr double kLongestRespawn = 7.0 * 24.0 * 3600.0;
constexpr std::uint64_t kLargestLevel = 1000;
/** The size of a record of a region file, and of its header. */
constexpr std::size_t kRecordSize = 14;
constexpr std::size_t kHeaderSize = 24;
/** The largest coordinate of a node place (the world limits of world/world.h). */
constexpr float kLargestCoordinate = 200000.0F;
/** The largest ItemRestrictionQuantity of a row. */
constexpr std::uint64_t kLargestItemCount = 1000000;
/** The restrictions of a row (GatherableData: Restriction) that the backend can check, and the restriction of an item. */
constexpr std::string_view kNoToolNecessary = "NoToolNecessary";
constexpr std::string_view kToolRequired = "ToolRequired";
constexpr std::string_view kItemRequired = "ItemRequired";

/** @return The first 16 hexadecimal digits (lower case) of the SHA-256 of the data, or an empty text when the hash failed. */
std::string sha256_prefix(const std::string& data) {
    unsigned char hash[32] = {};
    if (mbedtls_sha256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash, 0) != 0) {
        return {};
    }
    std::string text;
    for (std::size_t index = 0; index < 8; ++index) {
        static constexpr char kDigits[] = "0123456789abcdef";
        text += kDigits[hash[index] >> 4];
        text += kDigits[hash[index] & 0x0F];
    }
    return text;
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

bool hex_digit(char letter, unsigned& value) {
    if (letter >= '0' && letter <= '9') {
        value = static_cast<unsigned>(letter - '0');
    } else if (letter >= 'a' && letter <= 'f') {
        value = static_cast<unsigned>(letter - 'a' + 10);
    } else if (letter >= 'A' && letter <= 'F') {
        value = static_cast<unsigned>(letter - 'A' + 10);
    } else {
        return false;
    }
    return true;
}

bool hex_u64(const json& value, std::uint64_t& out) {
    if (!value.is_string() || value.get_ref<const std::string&>().size() != 16) {
        return false;
    }
    out = 0;
    for (const char letter : value.get_ref<const std::string&>()) {
        unsigned digit = 0;
        if (!hex_digit(letter, digit)) {
            return false;
        }
        out = (out << 4) | digit;
    }
    return true;
}

bool hex_bytes(const std::string& text, std::array<std::uint8_t, 16>& bytes) {
    if (text.size() != 32) {
        return false;
    }
    for (std::size_t index = 0; index < 16; ++index) {
        unsigned high = 0;
        unsigned low = 0;
        if (!hex_digit(text[index * 2], high) || !hex_digit(text[index * 2 + 1], low)) {
            return false;
        }
        bytes[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

bool number_in(const json& value, double low, double high, double& out) {
    if (!value.is_number()) {
        return false;
    }
    out = value.get<double>();
    return std::isfinite(out) && out >= low && out <= high;
}

bool whole_in(const json& value, std::uint64_t high, std::uint64_t& out) {
    if (!value.is_number_unsigned()) {
        return false;
    }
    out = value.get<std::uint64_t>();
    return out <= high;
}

bool index_in(const json& value, std::size_t size, std::int32_t& out, bool allowNone) {
    if (!value.is_number_integer()) {
        return false;
    }
    const std::int64_t number = value.get<std::int64_t>();
    if (number == -1 && allowNone) {
        out = -1;
        return true;
    }
    if (number < 0 || static_cast<std::uint64_t>(number) >= size) {
        return false;
    }
    out = static_cast<std::int32_t>(number);
    return true;
}

bool text_of(const json& object, const char* key, std::string& out) {
    if (!object.contains(key) || !object[key].is_string()) {
        return false;
    }
    out = object[key].get<std::string>();
    return true;
}

bool flag_of(const json& object, const char* key, bool& out) {
    if (!object.contains(key) || !object[key].is_boolean()) {
        return false;
    }
    out = object[key].get<bool>();
    return true;
}

} // namespace

const char* gathering_type_text(GatheringType type) {
    switch (type) {
    case GatheringType::none:
        return "None";
    case GatheringType::chopping:
        return "Chopping";
    case GatheringType::cutting:
        return "Cutting";
    case GatheringType::dressing:
        return "Dressing";
    case GatheringType::mining:
        return "Mining";
    case GatheringType::other:
        return "other";
    }
    return "?";
}

GatheringType gathering_type_of(std::string_view text) {
    const std::string lower = lower_ascii(text);
    if (lower.empty() || lower == "none") {
        return GatheringType::none;
    }
    if (lower == "chopping") {
        return GatheringType::chopping;
    }
    if (lower == "cutting") {
        return GatheringType::cutting;
    }
    if (lower == "dressing") {
        return GatheringType::dressing;
    }
    if (lower == "mining") {
        return GatheringType::mining;
    }
    return GatheringType::other;
}

std::uint32_t tool_slot(GatheringType type) {
    switch (type) {
    case GatheringType::chopping:
        return 33;
    case GatheringType::cutting:
        return 34;
    case GatheringType::dressing:
        return 35;
    case GatheringType::mining:
        return 36;
    case GatheringType::none:
    case GatheringType::other:
        return 0;
    }
    return 0;
}

std::uint64_t GatheringData::natural_group(std::int32_t regionX, std::int32_t regionY, std::uint32_t counter) {
    return (static_cast<std::uint64_t>(counter & 0x7FFFFU) << 13) | (static_cast<std::uint64_t>(regionY & 0x1F) << 8)
           | (static_cast<std::uint64_t>(regionX & 0x1F) << 3) | 1U;
}

bool GatheringData::load(const std::string& directory) {
    std::ifstream input(std::filesystem::path(directory) / kGatheringDataFile, std::ios::binary);
    if (!input) {
        *this = GatheringData{};
        m_error = "the file is not there";
        return false;
    }
    std::ostringstream text;
    text << input.rdbuf();
    return set(text.str(), {}, directory);
}

bool GatheringData::set(const std::string& document, std::unordered_map<std::string, std::vector<NaturalRecord>> records,
                        const std::string& directory) {
    *this = GatheringData{};
    m_directory = directory;
    if (!parse(document)) {
        const std::string error = m_error;
        *this = GatheringData{};
        m_error = error;
        return false;
    }
    for (auto& [name, list] : records) {
        const std::optional<std::uint32_t> index = region_index(name);
        if (!index || list.size() != m_regions[*index].records) {
            m_error = "the records of the region " + name + " do not fit its entry";
            m_loaded = false;
            return false;
        }
        m_loadedRecords[*index] = std::move(list);
    }
    m_loaded = true;
    return true;
}

bool GatheringData::parse(const std::string& text) {
    const json document = json::parse(text, nullptr, false);
    const auto fail = [&](std::string cause) {
        m_error = std::move(cause);
        return false;
    };
    if (!document.is_object()) {
        return fail("the file is not a JSON object");
    }
    std::uint64_t format = 0;
    if (!document.contains("format") || !whole_in(document["format"], 1000, format) || format != kFormat) {
        return fail("the file does not have the format 1");
    }
    for (const char* member : {"option", "rows", "events", "ranks", "tools", "templates", "capitals", "regions", "creatures"}) {
        if (!document.contains(member)) {
            return fail(std::string("the file has no member ") + member);
        }
    }
    const json& option = document["option"];
    std::uint64_t optionId = 0;
    std::uint64_t action = 0;
    if (!option.is_object() || !option.contains("id") || !whole_in(option["id"], 0xFFFFFFFFULL, optionId) || !option.contains("action")
        || !whole_in(option["action"], 255, action) || action != kGatherAction) {
        return fail("the option of a gather is not an option id with the action 14");
    }
    m_optionId = static_cast<std::uint32_t>(optionId);
    if (!document["rows"].is_array()) {
        return fail("the member rows is not a list");
    }
    for (const json& entry : document["rows"]) {
        GatherableRow row;
        std::string type;
        double time = 0.0;
        double low = 0.0;
        double high = 0.0;
        std::uint64_t level = 0;
        std::uint64_t required = 0;
        if (!entry.is_object() || !text_of(entry, "id", row.id) || row.id.empty() || !text_of(entry, "table", row.table)
            || !text_of(entry, "type", type) || !text_of(entry, "restriction", row.restriction) || !text_of(entry, "tradeskill", row.tradeskill)
            || !entry.contains("level") || !whole_in(entry["level"], kLargestLevel, level) || !entry.contains("time")
            || !number_in(entry["time"], 0.0, kLongestWork, time) || !(time > 0.0) || !entry.contains("respawn") || !entry["respawn"].is_array()
            || entry["respawn"].size() != 2 || !number_in(entry["respawn"][0], 0.0, kLongestRespawn, low)
            || !number_in(entry["respawn"][1], low, kLongestRespawn, high) || !text_of(entry, "loot", row.loot)
            || !text_of(entry, "material", row.material) || !text_of(entry, "event", row.event) || !flag_of(entry, "reset", row.resetOnEnd)
            || !flag_of(entry, "destroy", row.destroy) || !text_of(entry, "depleted", row.depleted) || !entry.contains("required")
            || !whole_in(entry["required"], 1000, required) || required == 0 || !flag_of(entry, "waitForImpact", row.waitForImpact)
            || !entry.contains("unsupported") || !entry["unsupported"].is_array()) {
            return fail("a gatherable row has a member with a wrong form or out of its range: " + (entry.is_object() && entry.contains("id")
                                                                                                         ? entry["id"].dump()
                                                                                                         : std::string("?")));
        }
        for (const json& cause : entry["unsupported"]) {
            if (!cause.is_string()) {
                return fail("an unsupported cause of the row " + row.id + " is not a text");
            }
            row.unsupported.push_back(cause.get<std::string>());
        }
        // Stage 1.8c: a quest gatherable (a member of the tool of stage 1.8c; a file of before has none).
        if (entry.contains("quest") && !flag_of(entry, "quest", row.quest)) {
            return fail("the member quest of the row " + row.id + " is not a flag");
        }
        // The item of the restriction (code review of stage 3.6a, finding GA-R5). A file of before the review can have no member: then
        // the row names no item that the loader can check.
        const bool itemKnown = entry.contains("item");
        std::uint64_t itemCount = 0;
        if ((itemKnown && !text_of(entry, "item", row.item))
            || (entry.contains("itemCount") && !whole_in(entry["itemCount"], kLargestItemCount, itemCount))) {
            return fail("the item of the row " + row.id + " is not a text, or its count is not a whole number of 0 to 1,000,000");
        }
        row.itemCount = static_cast<std::uint32_t>(itemCount);
        row.type = gathering_type_of(type);
        row.level = static_cast<std::uint32_t>(level);
        row.time = static_cast<float>(time);
        row.respawnMin = static_cast<float>(low);
        row.respawnMax = static_cast<float>(high);
        row.required = static_cast<std::uint32_t>(required);
        if (row.type == GatheringType::other && row.unsupported.empty()) {
            row.unsupported.push_back("the gathering type " + type);
        }
        // The mark "supported" (no cause) must agree with the gates of the row (finding GA-R5): an empty list of causes does not remove
        // a restriction that the backend cannot check, an item, or more than one gatherer. Such a row is refused, with the cause of the
        // loader. ItemRequired with an empty item is the one exception (BushSmall, BushWithered: the item names nothing to check).
        if (row.unsupported.empty()) {
            if (row.restriction == kItemRequired && !itemKnown) {
                row.unsupported.push_back("the restriction ItemRequired with no item member (the file marks the row supported)");
            } else if (row.restriction == kItemRequired && !row.item.empty()) {
                row.unsupported.push_back("the restriction ItemRequired with the item " + row.item + " (the file marks the row supported)");
            } else if (row.restriction != kNoToolNecessary && row.restriction != kToolRequired && row.restriction != kItemRequired) {
                row.unsupported.push_back("the restriction " + (row.restriction.empty() ? std::string("(none)") : row.restriction)
                                          + " (the file marks the row supported)");
            }
            if (row.required > 1) {
                row.unsupported.push_back("more than one gatherer: " + std::to_string(row.required) + " (the file marks the row supported)");
            }
        }
        if (!m_rowIndex.emplace(lower_ascii(row.id), static_cast<std::uint32_t>(m_rows.size())).second) {
            return fail("the row " + row.id + " is two times in the file");
        }
        m_rows.push_back(std::move(row));
    }
    if (!document["events"].is_object()) {
        return fail("the member events is not an object");
    }
    for (const auto& [id, entry] : document["events"].items()) {
        GatheringEvent event;
        event.id = id;
        std::uint64_t amount = 0;
        if (!entry.is_array() || entry.size() != 2 || !entry[0].is_string() || !entry[1].is_number() || !(entry[1].get<double>() >= 0.0)
            || !std::isfinite(entry[1].get<double>()) || entry[1].get<double>() > 1.0e7) {
            return fail("the event " + id + " is not a progression and an amount");
        }
        amount = static_cast<std::uint64_t>(std::llround(entry[1].get<double>()));
        event.progression = entry[0].get<std::string>();
        event.amount = static_cast<std::uint32_t>(amount);
        m_events.emplace(lower_ascii(id), std::move(event));
    }
    for (const GatherableRow& row : m_rows) {
        if (!row.event.empty() && !row.quest && m_events.count(lower_ascii(row.event)) == 0) {
            return fail("the row " + row.id + " names the event " + row.event + ", and the file does not have it");
        }
    }
    if (!document["ranks"].is_object()) {
        return fail("the member ranks is not an object");
    }
    for (const auto& [skill, list] : document["ranks"].items()) {
        if (!list.is_array() || list.empty() || list.size() > 10000) {
            return fail("the ranks of " + skill + " are not a list");
        }
        std::vector<GatheringRank> ranks;
        for (const json& entry : list) {
            double values[3] = {};
            if (!entry.is_array() || entry.size() != 3 || !number_in(entry[0], 0.0, 1.0e6, values[0]) || !number_in(entry[1], -10.0, 1000.0, values[1])
                || !number_in(entry[2], -10.0, 1000.0, values[2])) {
                return fail("a rank of " + skill + " is not three numbers in their range");
            }
            ranks.push_back(GatheringRank{static_cast<float>(values[0]), static_cast<float>(values[1]), static_cast<float>(values[2])});
        }
        m_ranks.emplace(lower_ascii(skill), std::move(ranks));
    }
    if (!document["tools"].is_object()) {
        return fail("the member tools is not an object");
    }
    for (const auto& [item, entry] : document["tools"].items()) {
        GatheringTool tool;
        double values[4] = {};
        if (!entry.is_object() || !entry.contains("types") || !entry["types"].is_array() || !entry.contains("min") || !entry.contains("max")
            || !entry.contains("efficiency") || !entry.contains("multiplier") || !number_in(entry["min"], 0.0, 1000.0, values[0])
            || !number_in(entry["max"], values[0], 1000.0, values[1]) || !number_in(entry["efficiency"], -1000.0, 1000.0, values[2])
            || !number_in(entry["multiplier"], -1000.0, 1000.0, values[3])) {
            return fail("the tool " + item + " has a member with a wrong form or out of its range");
        }
        for (const json& type : entry["types"]) {
            if (!type.is_string()) {
                return fail("a type of the tool " + item + " is not a text");
            }
            tool.types.push_back(gathering_type_of(type.get<std::string>()));
        }
        tool.minEfficiency = static_cast<float>(values[0]);
        tool.maxEfficiency = static_cast<float>(values[1]);
        tool.efficiency = static_cast<float>(values[2]);
        tool.multiplier = static_cast<float>(values[3]);
        m_tools.emplace(lower_ascii(item), std::move(tool));
    }
    if (!document["templates"].is_array()) {
        return fail("the member templates is not a list");
    }
    for (const json& entry : document["templates"]) {
        GatheringTemplate slice;
        std::string asset;
        std::uint64_t sub = 0;
        std::uint64_t slot = 0;
        if (!entry.is_object() || !text_of(entry, "slice", slice.slice) || !text_of(entry, "asset", asset) || !hex_bytes(asset, slice.asset)
            || !entry.contains("sub") || !whole_in(entry["sub"], 0xFFFFFFFFULL, sub) || !entry.contains("root") || !hex_u64(entry["root"], slice.root)
            || !entry.contains("controllerSlot") || !whole_in(entry["controllerSlot"], 4095, slot) || slot == 0 || !text_of(entry, "table", slice.table)
            || !entry.contains("bound") || !entry["bound"].is_object()) {
            return fail("a template has a member with a wrong form");
        }
        slice.sub = static_cast<std::uint32_t>(sub);
        slice.controllerSlot = static_cast<std::uint32_t>(slot);
        const json& bound = entry["bound"];
        std::uint64_t boundSlot = 0;
        double rangeSq = 0.0;
        if (!bound.contains("entity") || !hex_u64(bound["entity"], slice.bound.entity) || !bound.contains("component")
            || !hex_u64(bound["component"], slice.bound.component) || !bound.contains("option") || !hex_u64(bound["option"], slice.bound.option)
            || !bound.contains("slot") || !whole_in(bound["slot"], 4095, boundSlot) || boundSlot == 0 || !bound.contains("rangeSq")
            || !number_in(bound["rangeSq"], 0.0, 1.0e6, rangeSq) || !(rangeSq > 0.0) || !bound.contains("capacity")
            || !bound["capacity"].is_number_integer() || !bound.contains("translation") || !bound["translation"].is_array()
            || bound["translation"].size() != 3 || !flag_of(bound, "lineOfSight", slice.bound.lineOfSight)
            || !flag_of(bound, "arcRequired", slice.bound.arcRequired) || !bound.contains("requires") || !bound["requires"].is_array()) {
            return fail("the bound of the template " + slice.slice + " has a member with a wrong form or out of its range");
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            double value = 0.0;
            if (!number_in(bound["translation"][axis], -1000.0, 1000.0, value)) {
                return fail("the bound of the template " + slice.slice + " has a translation that is not finite");
            }
            slice.bound.translation[axis] = static_cast<float>(value);
        }
        slice.bound.slot = static_cast<std::uint32_t>(boundSlot);
        slice.bound.rangeSq = static_cast<float>(rangeSq);
        slice.bound.capacity = static_cast<std::int32_t>(bound["capacity"].get<std::int64_t>());
        slice.bound.requirement = !bound["requires"].empty();
        m_templates.push_back(std::move(slice));
    }
    const auto link_of = [&](const json& entry, GatheringLink& link) {
        std::int32_t templateIndex = 0;
        std::int32_t row = 0;
        if (!entry.is_array() || entry.size() < 2 || !index_in(entry[0], m_templates.size(), templateIndex, false)
            || !index_in(entry[1], m_rows.size(), row, false)) {
            return false;
        }
        link = GatheringLink{static_cast<std::uint32_t>(templateIndex), static_cast<std::uint32_t>(row), {}, false};
        // Stage 1.8c: [template, row, interactTag, onlyOnQuest] for a quest object.
        if (entry.size() >= 4) {
            if (!entry[2].is_string() || !entry[3].is_boolean()) {
                return false;
            }
            link.interactTag = lower_ascii(entry[2].get<std::string>());
            link.onlyOnQuest = entry[3].get<bool>();
        }
        return true;
    };
    if (!document["capitals"].is_object()) {
        return fail("the member capitals is not an object");
    }
    for (const auto& [key, entry] : document["capitals"].items()) {
        GatheringLink link;
        if (key.size() != 36 || !link_of(entry, link)) {
            return fail("the capital node " + key + " is not a placement UUID with a template and a row");
        }
        m_capitals.emplace(lower_ascii(key), link);
    }
    if (!document["creatures"].is_object()) {
        return fail("the member creatures is not an object");
    }
    for (const auto& [key, entry] : document["creatures"].items()) {
        const std::size_t colon = key.find(':');
        std::array<std::uint8_t, 16> asset{};
        std::int32_t templateIndex = 0;
        std::int32_t row = 0;
        if (colon != 32 || !hex_bytes(key.substr(0, 32), asset) || !entry.is_object() || !entry.contains("template") || !entry.contains("row")
            || !index_in(entry["template"], m_templates.size(), templateIndex, false) || !index_in(entry["row"], m_rows.size(), row, false)) {
            return fail("the creature slice " + key + " is not an asset with a template and a row");
        }
        std::uint32_t sub = 0;
        try {
            sub = static_cast<std::uint32_t>(std::stoul(key.substr(colon + 1)));
        } catch (const std::exception&) {
            return fail("the creature slice " + key + " has no sub id");
        }
        m_creatures[{asset, sub}] = GatheringLink{static_cast<std::uint32_t>(templateIndex), static_cast<std::uint32_t>(row), {}, false};
    }
    if (!document["regions"].is_array()) {
        return fail("the member regions is not a list");
    }
    for (const json& entry : document["regions"]) {
        GatheringRegion region;
        std::uint64_t records = 0;
        std::uint64_t tails = 0;
        if (!entry.is_object() || !text_of(entry, "name", region.name) || !entry.contains("x") || !entry["x"].is_number_integer()
            || !entry.contains("y") || !entry["y"].is_number_integer() || !text_of(entry, "file", region.file) || !entry.contains("records")
            || !whole_in(entry["records"], 0x7FFFFULL, records) || !entry.contains("tails") || !whole_in(entry["tails"], 0x7FFFFULL, tails)
            || !text_of(entry, "version", region.version) || !entry.contains("types") || !entry["types"].is_array()) {
            return fail("a region has a member with a wrong form or out of its range");
        }
        region.x = static_cast<std::int32_t>(entry["x"].get<std::int64_t>());
        region.y = static_cast<std::int32_t>(entry["y"].get<std::int64_t>());
        if (region.x < 0 || region.x > 31 || region.y < 0 || region.y > 31 || region.file.find("..") != std::string::npos) {
            return fail("the region " + region.name + " has a place outside 0 to 31 or a file name with ..");
        }
        region.records = static_cast<std::uint32_t>(records);
        region.tails = static_cast<std::uint32_t>(tails);
        // The digest of its region file (finding GA-R6): 16 hexadecimal digits; a document of before the review has no member.
        if (entry.contains("digest")) {
            std::uint64_t digest = 0;
            if (!hex_u64(entry["digest"], digest)) {
                return fail("the region " + region.name + " has a digest that is not 16 hexadecimal digits");
            }
            region.digest = lower_ascii(entry["digest"].get<std::string>());
        }
        for (const json& type : entry["types"]) {
            std::int32_t templateIndex = -1;
            std::int32_t row = -1;
            if (!type.is_array() || type.size() != 3 || !index_in(type[0], m_templates.size(), templateIndex, true)
                || !index_in(type[1], m_rows.size(), row, true) || !type[2].is_string() || (templateIndex < 0 && row >= 0)) {
                return fail("a slice index of the region " + region.name + " is not a template, a row, and a variant");
            }
            region.templates.push_back(templateIndex);
            region.rows.push_back(templateIndex >= 0 ? row : -1);
            region.variants.push_back(type[2].get<std::string>());
        }
        if (region.templates.size() > 0xFFFF) {
            return fail("the region " + region.name + " has more than 65,535 slice indexes");
        }
        if (!m_regionByPlace.emplace(std::make_pair(region.x, region.y), static_cast<std::uint32_t>(m_regions.size())).second) {
            return fail("two regions have the place of " + region.name);
        }
        m_naturalCount += region.records;
        m_regions.push_back(std::move(region));
    }
    return true;
}

std::optional<std::uint32_t> GatheringData::find_row(std::string_view id) const {
    const auto found = m_rowIndex.find(lower_ascii(id));
    return found == m_rowIndex.end() ? std::nullopt : std::optional<std::uint32_t>(found->second);
}

const GatheringEvent* GatheringData::event(std::string_view id) const {
    const auto found = m_events.find(lower_ascii(id));
    return found == m_events.end() ? nullptr : &found->second;
}

GatheringRank GatheringData::rank(std::string_view tradeskill, std::uint32_t rank) const {
    const auto found = m_ranks.find(lower_ascii(tradeskill));
    if (found == m_ranks.end() || found->second.empty()) {
        return {};
    }
    return found->second[std::min<std::size_t>(rank, found->second.size() - 1)];
}

const GatheringTool* GatheringData::tool(std::string_view itemId) const {
    const auto found = m_tools.find(lower_ascii(itemId));
    return found == m_tools.end() ? nullptr : &found->second;
}

std::optional<GatheringLink> GatheringData::capital(std::string_view key) const {
    const auto found = m_capitals.find(lower_ascii(key));
    return found == m_capitals.end() ? std::nullopt : std::optional<GatheringLink>(found->second);
}

std::optional<GatheringLink> GatheringData::creature(const std::array<std::uint8_t, 16>& asset, std::uint32_t sub) const {
    const auto found = m_creatures.find({asset, sub});
    return found == m_creatures.end() ? std::nullopt : std::optional<GatheringLink>(found->second);
}

std::optional<std::uint32_t> GatheringData::region_index(std::string_view name) const {
    for (std::uint32_t index = 0; index < m_regions.size(); ++index) {
        if (m_regions[index].name == name) {
            return index;
        }
    }
    return std::nullopt;
}

const std::vector<NaturalRecord>* GatheringData::records_of(std::uint32_t region) const {
    const auto found = m_loadedRecords.find(region);
    if (found != m_loadedRecords.end()) {
        return &found->second;
    }
    if (m_directory.empty() || region >= m_regions.size()) {
        return nullptr;
    }
    // The file of the region at its first use (MP-8: only the regions of the requests).
    const GatheringRegion& entry = m_regions[region];
    std::ifstream input(std::filesystem::path(m_directory) / entry.file, std::ios::binary);
    std::vector<NaturalRecord> records;
    bool good = false;
    if (input) {
        const std::string data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        std::uint32_t format = 0;
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::uint32_t count = 0;
        std::uint32_t tails = 0;
        if (data.size() >= kHeaderSize && std::memcmp(data.data(), "SWGN", 4) == 0) {
            std::memcpy(&format, data.data() + 4, 4);
            std::memcpy(&x, data.data() + 8, 4);
            std::memcpy(&y, data.data() + 12, 4);
            std::memcpy(&count, data.data() + 16, 4);
            std::memcpy(&tails, data.data() + 20, 4);
        }
        // The file of this document (finding GA-R6): its digest; a file of another export (one that stopped after it wrote the file) has
        // a different one.
        if (format == kRegionFormat && x == entry.x && y == entry.y && count == entry.records && tails == entry.tails
            && data.size() == kHeaderSize + kRecordSize * static_cast<std::size_t>(count)
            && (entry.digest.empty() || sha256_prefix(data) == entry.digest)) {
            good = true;
            records.resize(count);
            for (std::size_t index = 0; index < count && good; ++index) {
                const char* at = data.data() + kHeaderSize + kRecordSize * index;
                NaturalRecord& record = records[index];
                std::memcpy(&record.type, at, 2);
                std::memcpy(&record.x, at + 2, 4);
                std::memcpy(&record.y, at + 6, 4);
                std::memcpy(&record.z, at + 10, 4);
                good = record.type < entry.templates.size() && std::isfinite(record.x) && std::isfinite(record.y) && std::isfinite(record.z)
                       && std::fabs(record.x) <= kLargestCoordinate && std::fabs(record.y) <= kLargestCoordinate
                       && std::fabs(record.z) <= kLargestCoordinate;
            }
        }
    }
    ++m_work.regionLoads;
    // A file that is not there, not complete, or not the file of the document: an empty list (no node of the region; the next lookup
    // does not read it again), and the region is not valid (finding GA-R2: a stored depletion of the region stays).
    if (!good) {
        records.clear();
        m_badRegions.insert(region);
    }
    m_work.recordsRead += records.size();
    return &m_loadedRecords.emplace(region, std::move(records)).first->second;
}

bool GatheringData::region_valid(std::uint32_t region) const {
    return region < m_regions.size() && records_of(region) != nullptr && m_badRegions.count(region) == 0;
}

std::optional<std::uint32_t> GatheringData::region_of(std::uint64_t group) const {
    if ((group & 0x7U) != 1U || (group >> 32) != 0) {
        return std::nullopt;
    }
    const auto region = m_regionByPlace.find({static_cast<std::int32_t>((group >> 3) & 0x1FU), static_cast<std::int32_t>((group >> 8) & 0x1FU)});
    return region == m_regionByPlace.end() ? std::nullopt : std::optional<std::uint32_t>(region->second);
}

std::optional<NaturalNode> GatheringData::natural_at(std::uint32_t region, std::uint32_t record) const {
    if (region >= m_regions.size()) {
        return std::nullopt;
    }
    const std::vector<NaturalRecord>* records = records_of(region);
    if (records == nullptr || record >= records->size()) {
        return std::nullopt;
    }
    const GatheringRegion& entry = m_regions[region];
    const NaturalRecord& found = (*records)[record];
    if (entry.templates[found.type] < 0 || entry.rows[found.type] < 0) {
        return std::nullopt;
    }
    NaturalNode node;
    node.region = region;
    node.record = record;
    node.templateIndex = static_cast<std::uint32_t>(entry.templates[found.type]);
    node.row = static_cast<std::uint32_t>(entry.rows[found.type]);
    node.position = state::WorldPosition{found.x, found.y, found.z};
    node.group = natural_group(entry.x, entry.y, record + 1);
    return node;
}

std::optional<NaturalNode> GatheringData::natural(std::uint64_t group) const {
    ++m_work.lookups;
    if ((group & 0x7U) != 1U || (group >> 32) != 0) {
        return std::nullopt; // Not an offline id of a distribution record.
    }
    const auto x = static_cast<std::int32_t>((group >> 3) & 0x1FU);
    const auto y = static_cast<std::int32_t>((group >> 8) & 0x1FU);
    const auto counter = static_cast<std::uint32_t>((group >> 13) & 0x7FFFFU);
    const auto region = m_regionByPlace.find({x, y});
    if (region == m_regionByPlace.end() || counter == 0) {
        return std::nullopt;
    }
    // Counter 1 is record 0; a counter above the primary records names a record of the tails (no slice: not a node).
    return natural_at(region->second, counter - 1);
}

} // namespace soulwarden::backend::gamedata

#include "gamedata/spell_data.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <set>

#include <nlohmann/json.hpp>

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

constexpr std::uint64_t kFormat = 1;
constexpr std::uint32_t kSpellStateType = 2912;
constexpr std::uint32_t kDelayedEventStateType = 3139;
/** One record has 255 fragments at most (the fragment count is one byte): the metadata, the Spell state, and the events. */
constexpr std::size_t kMostEvents = 250;

std::string lower_ascii(std::string_view text) {
    std::string lower(text);
    for (char& letter : lower) {
        if (letter >= 'A' && letter <= 'Z') {
            letter = static_cast<char>(letter + ('a' - 'A'));
        }
    }
    return lower;
}

bool text_of(const json& value, std::string& out) {
    if (!value.is_string()) {
        return false;
    }
    out = value.get<std::string>();
    return true;
}

bool number_of(const json& value, float& out) {
    if (!value.is_number() || !std::isfinite(value.get<double>())) {
        return false;
    }
    out = value.get<float>();
    return true;
}

bool whole_of(const json& value, std::uint32_t& out) {
    if (!value.is_number_unsigned() || value.get<std::uint64_t>() > 0xFFFFFFFFULL) {
        return false;
    }
    out = static_cast<std::uint32_t>(value.get<std::uint64_t>());
    return true;
}

bool id_of(const json& value, std::uint64_t& out) {
    if (!value.is_string()) {
        return false;
    }
    const std::string text = value.get<std::string>();
    if (text.size() != 16 || text.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
        return false;
    }
    out = std::strtoull(text.c_str(), nullptr, 16);
    return true;
}

bool asset_of(const json& value, std::array<std::uint8_t, 16>& asset) {
    if (!value.is_string()) {
        return false;
    }
    std::string digits;
    for (const char letter : value.get<std::string>()) {
        if (letter != '-') {
            digits += letter;
        }
    }
    if (digits.size() != 32 || digits.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
        return false;
    }
    for (std::size_t index = 0; index < asset.size(); ++index) {
        asset[index] = static_cast<std::uint8_t>(std::strtoul(digits.substr(index * 2, 2).c_str(), nullptr, 16));
    }
    return true;
}

bool read_slice(const json& entry, SpellSlice& slice) {
    if (!entry.is_object() || !entry.contains("path") || !text_of(entry["path"], slice.path) || !entry.contains("asset")
        || !asset_of(entry["asset"], slice.asset) || !entry.contains("sub") || !whole_of(entry["sub"], slice.subId)
        || !entry.contains("root") || !id_of(entry["root"], slice.root) || !entry.contains("interestRadius")
        || !number_of(entry["interestRadius"], slice.interestRadius) || !entry.contains("spellSlot")
        || !whole_of(entry["spellSlot"], slice.spellSlot) || !entry.contains("slots") || !entry["slots"].is_array()
        || !entry.contains("events") || !entry["events"].is_array() || !entry.contains("components") || !entry["components"].is_array()) {
        return false;
    }
    for (const json& row : entry["slots"]) {
        SpellSlot& slot = slice.slots.emplace_back();
        if (!row.is_array() || row.size() != 3 || !whole_of(row[0], slot.slot) || !text_of(row[1], slot.name) || !whole_of(row[2], slot.type)) {
            return false;
        }
    }
    for (const json& row : entry["events"]) {
        SpellEvent& event = slice.events.emplace_back();
        if (!row.is_array() || row.size() != 6 || !whole_of(row[0], event.slot) || !number_of(row[1], event.delay)
            || !whole_of(row[2], event.type) || !text_of(row[3], event.target) || !text_of(row[4], event.offense) || !row[5].is_boolean()) {
            return false;
        }
        event.destroys = row[5].get<bool>();
    }
    for (const json& row : entry["components"]) {
        SpellComponent& component = slice.components.emplace_back();
        if (!row.is_array() || row.size() != 3 || !id_of(row[0], component.entity) || !id_of(row[1], component.component)
            || !text_of(row[2], component.name)) {
            return false;
        }
    }
    return true;
}

} // namespace

std::optional<float> SpellSlice::end_delay() const {
    for (const SpellEvent& event : events) {
        if (event.destroys && event.type == spell_event::kTrigger) {
            return event.delay;
        }
    }
    return std::nullopt;
}

std::optional<float> SpellSlice::activation_delay(std::string_view row) const {
    for (const SpellEvent& event : events) {
        if (event.type == spell_event::kActivate && !event.offense.empty() && lower_ascii(event.offense) == lower_ascii(row)) {
            return event.delay;
        }
    }
    return std::nullopt;
}

bool SpellData::load(const std::string& path) {
    m_tables = {};
    m_spells.clear();
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        m_error = "the file is not there";
        return false;
    }
    const json document = json::parse(input, nullptr, false);
    if (!document.is_object() || !document.contains("format") || !document["format"].is_number_unsigned()
        || document["format"].get<std::uint64_t>() != kFormat) {
        m_error = "the file is not a JSON object of the format 1";
        return false;
    }
    if (!document.contains("slices") || !document["slices"].is_array() || !document.contains("spells") || !document["spells"].is_array()) {
        m_error = "the file does not have the lists slices and spells";
        return false;
    }
    SpellTables tables;
    for (const json& entry : document["slices"]) {
        if (!read_slice(entry, tables.slices.emplace_back())) {
            m_error = "slice " + std::to_string(tables.slices.size() - 1) + " does not have the form of the tool";
            return false;
        }
    }
    for (const json& row : document["spells"]) {
        std::uint32_t index = 0;
        std::string id;
        if (!row.is_array() || row.size() != 2 || !text_of(row[0], id) || !whole_of(row[1], index)) {
            m_error = "a spell row does not have the form of the tool";
            return false;
        }
        tables.spells.emplace_back(id, index);
    }
    if (document.contains("counts")) {
        const json& counts = document["counts"];
        const auto count = [&](const char* name, std::size_t size) {
            return counts.is_object() && counts.contains(name) && counts[name].is_number_unsigned() && counts[name].get<std::uint64_t>() == size;
        };
        if (!count("slices", tables.slices.size()) || !count("spells", tables.spells.size())) {
            m_error = "a list does not have the count of the file";
            return false;
        }
    }
    return set(std::move(tables));
}

bool SpellData::set(SpellTables tables) {
    m_tables = {};
    m_spells.clear();
    std::string error;
    for (std::size_t index = 0; index < tables.slices.size() && error.empty(); ++index) {
        const SpellSlice& slice = tables.slices[index];
        const std::string name = "slice " + slice.path;
        if (std::all_of(slice.asset.begin(), slice.asset.end(), [](std::uint8_t byte) { return byte == 0; })) {
            error = name + " has the null asset id";
            break;
        }
        if (!std::isfinite(slice.interestRadius) || slice.interestRadius < 0.0F) {
            error = name + " has an interest radius that is not a distance";
            break;
        }
        std::set<std::uint32_t> slots;
        for (const SpellSlot& slot : slice.slots) {
            if (slot.slot == 0 || !slots.insert(slot.slot).second) {
                error = name + " has the slot 0 or two components with one slot";
                break;
            }
        }
        const auto type_of = [&](std::uint32_t wanted) -> std::uint32_t {
            const auto found = std::find_if(slice.slots.begin(), slice.slots.end(), [&](const SpellSlot& slot) { return slot.slot == wanted; });
            return found == slice.slots.end() ? 0 : found->type;
        };
        if (error.empty() && (slice.spellSlot == 0 || type_of(slice.spellSlot) != kSpellStateType)) {
            error = name + " has no Spell slot of the type 2912";
        }
        if (error.empty() && slice.events.size() > kMostEvents) {
            error = name + " has more delayed events than one record holds";
        }
        std::set<std::uint32_t> eventSlots;
        for (const SpellEvent& event : slice.events) {
            if (!error.empty()) {
                break;
            }
            if (type_of(event.slot) != kDelayedEventStateType || !eventSlots.insert(event.slot).second) {
                error = name + " has a delayed event whose slot is not one DelayedEvent slot of the slice";
            } else if (!std::isfinite(event.delay)) {
                error = name + " has a delayed event whose delay is not a number";
            }
        }
    }
    std::unordered_map<std::string, std::size_t> spells;
    for (const auto& [id, index] : tables.spells) {
        if (!error.empty()) {
            break;
        }
        if (id.empty() || index >= tables.slices.size()) {
            error = "the spell " + id + " has no slice";
        } else if (!spells.emplace(lower_ascii(id), index).second) {
            error = "two spells have the id " + id;
        }
    }
    if (!error.empty()) {
        m_error = error;
        return false;
    }
    m_tables = std::move(tables);
    m_spells = std::move(spells);
    m_error.clear();
    return true;
}

const SpellSlice* SpellData::slice_of(std::string_view spellId) const {
    const auto found = m_spells.find(lower_ascii(spellId));
    return found == m_spells.end() ? nullptr : &m_tables.slices[found->second];
}

} // namespace soulwarden::backend::gamedata

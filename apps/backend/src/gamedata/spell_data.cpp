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

/** Format 1 (stage 3.3b-S2a) and format 2 (stage 3.3b-S2b: the values of the Pylon, the trap, and the Beacon). */
constexpr std::uint64_t kFormat = 1;
constexpr std::uint64_t kFormat2 = 2;
/** Format 3 (stage 3.3b-M2): also the AreaStatusEffectComponents of each slice (the fortify in the well of Unyielding). */
constexpr std::uint64_t kFormat3 = 3;
constexpr std::uint32_t kSpellStateType = 2912;
constexpr std::uint32_t kPositionStateType = 13;
constexpr std::uint32_t kVitalsStateType = 15;
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

bool texts_of(const json& value, std::vector<std::string>& out) {
    if (!value.is_array()) {
        return false;
    }
    for (const json& item : value) {
        if (!text_of(item, out.emplace_back())) {
            return false;
        }
    }
    return true;
}

/** The members of format 2 of a slice (stage 3.3b-S2b), and of format 3 (stage 3.3b-M2: `areaStatuses`). */
bool read_slice_values(const json& entry, SpellSlice& slice, bool format3) {
    if (format3) {
        if (!entry.contains("areaStatuses") || !entry["areaStatuses"].is_array()) {
            return false;
        }
        for (const json& row : entry["areaStatuses"]) {
            SpellAreaStatus& area = slice.areaStatuses.emplace_back();
            if (!row.is_array() || row.size() != 6 || !text_of(row[0], area.entity) || !text_of(row[1], area.detection)
                || !number_of(row[2], area.radius) || area.radius < 0.0F || !row[3].is_array() || row[3].empty() || !row[4].is_boolean()
                || !whole_of(row[5], area.targets)) {
                return false;
            }
            for (const json& status : row[3]) {
                std::string text;
                if (!text_of(status, text) || text.empty()) {
                    return false;
                }
                area.statuses.push_back(std::move(text));
            }
            area.removeOnExit = row[4].get<bool>();
        }
    }
    if (!entry.contains("positionSlot") || !whole_of(entry["positionSlot"], slice.positionSlot) || !entry.contains("vitals")
        || !entry.contains("autoSpell") || !entry.contains("volumes") || !entry["volumes"].is_array() || !entry.contains("offenses")
        || !entry["offenses"].is_array() || !entry.contains("detectionEvents") || !entry["detectionEvents"].is_array()
        || !entry.contains("timelines") || !entry["timelines"].is_array()) {
        return false;
    }
    if (const json& row = entry["vitals"]; !row.is_null()) {
        SpellVitals& vitals = slice.vitals.emplace();
        if (!row.is_array() || row.size() != 3 || !whole_of(row[0], vitals.slot) || !text_of(row[1], vitals.row) || !text_of(row[2], vitals.deathTarget)) {
            return false;
        }
    }
    if (const json& row = entry["autoSpell"]; !row.is_null()) {
        SpellAutoCast& cast = slice.autoCast.emplace();
        if (!row.is_array() || row.size() != 8 || !text_of(row[0], cast.spell) || !number_of(row[1], cast.lifetime)
            || !number_of(row[2], cast.lifetimeOnHit) || !number_of(row[3], cast.initialDelay) || !number_of(row[4], cast.cooldown)
            || !text_of(row[5], cast.expireTarget) || !text_of(row[6], cast.castTarget) || !texts_of(row[7], cast.exclude)) {
            return false;
        }
    }
    for (const json& row : entry["volumes"]) {
        SpellVolume& volume = slice.volumes.emplace_back();
        if (!row.is_array() || row.size() != 3 || !text_of(row[0], volume.entity) || !number_of(row[1], volume.radius)
            || !number_of(row[2], volume.activeAfter)) {
            return false;
        }
    }
    for (const json& row : entry["offenses"]) {
        SpellOffense& offense = slice.offenses.emplace_back();
        if (!row.is_array() || row.size() != 3 || !text_of(row[0], offense.entity) || !text_of(row[1], offense.row)
            || !number_of(row[2], offense.radius)) {
            return false;
        }
    }
    for (const json& row : entry["detectionEvents"]) {
        SpellDetectionEvent& event = slice.detectionEvents.emplace_back();
        if (!row.is_array() || row.size() != 2 || !text_of(row[0], event.target) || !whole_of(row[1], event.type)) {
            return false;
        }
    }
    for (const json& row : entry["timelines"]) {
        SpellTimeline& timeline = slice.timelines.emplace_back();
        if (!row.is_array() || row.size() != 2 || !text_of(row[0], timeline.owner) || !number_of(row[1], timeline.destroyAfter)) {
            return false;
        }
    }
    return true;
}

bool read_slice(const json& entry, SpellSlice& slice, bool format2, bool format3) {
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
        const std::size_t size = format2 ? 9 : 6;
        if (!row.is_array() || row.size() != size || !whole_of(row[0], event.slot) || !number_of(row[1], event.delay)
            || !whole_of(row[2], event.type) || !text_of(row[3], event.target) || !text_of(row[4], event.offense) || !row[5].is_boolean()) {
            return false;
        }
        event.destroys = row[5].get<bool>();
        if (format2) {
            if (!text_of(row[6], event.owner) || !row[7].is_boolean() || !row[8].is_boolean()) {
                return false;
            }
            event.onInit = row[7].get<bool>();
            event.onTrigger = row[8].get<bool>();
        }
    }
    for (const json& row : entry["components"]) {
        SpellComponent& component = slice.components.emplace_back();
        if (!row.is_array() || row.size() != 3 || !id_of(row[0], component.entity) || !id_of(row[1], component.component)
            || !text_of(row[2], component.name)) {
            return false;
        }
    }
    return !format2 || read_slice_values(entry, slice, format3);
}

bool read_vitals_row(const json& row, SpellVitalsRow& vitals) {
    return row.is_array() && row.size() == 6 && text_of(row[0], vitals.id) && number_of(row[1], vitals.healthBaseMax)
           && number_of(row[2], vitals.ownerHealthPart) && number_of(row[3], vitals.physicalArmor) && number_of(row[4], vitals.elementalArmor)
           && number_of(row[5], vitals.gearScore);
}

} // namespace

std::optional<float> SpellSlice::end_delay() const {
    for (const SpellEvent& event : events) {
        // An event that starts at a trigger is the end of a trigger (the death chain of the Pylon), not of the life.
        if (event.destroys && event.type == spell_event::kTrigger && event.onInit) {
            return event.delay;
        }
    }
    return std::nullopt;
}

std::optional<float> SpellSlice::destroy_after_trigger(std::string_view owner) const {
    for (const SpellEvent& event : events) {
        if (event.destroys && event.type == spell_event::kTrigger && event.onTrigger && lower_ascii(event.owner) == lower_ascii(owner)) {
            return event.delay;
        }
    }
    for (const SpellTimeline& timeline : timelines) {
        if (timeline.destroyAfter >= 0.0F && lower_ascii(timeline.owner) == lower_ascii(owner)) {
            return timeline.destroyAfter;
        }
    }
    return std::nullopt;
}

const SpellVolume* SpellSlice::volume(std::string_view entity) const {
    const SpellVolume* found = nullptr;
    for (const SpellVolume& candidate : volumes) {
        if (!entity.empty() ? lower_ascii(candidate.entity) == lower_ascii(entity) : (found == nullptr || candidate.radius > found->radius)) {
            found = &candidate;
            if (!entity.empty()) {
                break;
            }
        }
    }
    return found;
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
    m_vitals.clear();
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        m_error = "the file is not there";
        return false;
    }
    const json document = json::parse(input, nullptr, false);
    if (!document.is_object() || !document.contains("format") || !document["format"].is_number_unsigned()
        || document["format"].get<std::uint64_t>() < kFormat || document["format"].get<std::uint64_t>() > kFormat3) {
        m_error = "the file is not a JSON object of the format 1, 2, or 3";
        return false;
    }
    const bool format2 = document["format"].get<std::uint64_t>() >= kFormat2;
    const bool format3 = document["format"].get<std::uint64_t>() >= kFormat3;
    if (!document.contains("slices") || !document["slices"].is_array() || !document.contains("spells") || !document["spells"].is_array()) {
        m_error = "the file does not have the lists slices and spells";
        return false;
    }
    SpellTables tables;
    tables.format = format3 ? 3 : format2 ? 2 : 1;
    for (const json& entry : document["slices"]) {
        if (!read_slice(entry, tables.slices.emplace_back(), format2, format3)) {
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
    if (format2) {
        if (!document.contains("vitals") || !document["vitals"].is_array()) {
            m_error = "the file of the format 2 has no list vitals";
            return false;
        }
        for (const json& row : document["vitals"]) {
            if (!read_vitals_row(row, tables.vitals.emplace_back())) {
                m_error = "a vitals row does not have the form of the tool";
                return false;
            }
        }
        if (document.contains("vitalsNotInData")) {
            if (!document["vitalsNotInData"].is_array()) {
                m_error = "the list vitalsNotInData is not a list";
                return false;
            }
            for (const json& row : document["vitalsNotInData"]) {
                if (!text_of(row, tables.vitalsNotInData.emplace_back())) {
                    m_error = "an entry of vitalsNotInData is not a text";
                    return false;
                }
            }
        }
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
    m_vitals.clear();
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
        // A Spell slot of the type 2912, or (format 2: the trap) a PositionInTheWorld slot of the type 13.
        const bool spellSlot = slice.spellSlot != 0 && type_of(slice.spellSlot) == kSpellStateType;
        const bool positionSlot = slice.positionSlot != 0 && type_of(slice.positionSlot) == kPositionStateType;
        if (error.empty() && ((slice.spellSlot != 0 && !spellSlot) || (slice.positionSlot != 0 && !positionSlot) || (!spellSlot && !positionSlot))) {
            error = name + " has no Spell slot of the type 2912 and no PositionInTheWorld slot of the type 13";
        }
        if (error.empty() && slice.vitals && (slice.vitals->slot == 0 || type_of(slice.vitals->slot) != kVitalsStateType)) {
            error = name + " has a vitals row with no Vitals slot of the type 15";
        }
        // A Vitals slot needs its vitals row (format 2; code review of stage 1.1b, finding F2): with no row the entity of the
        // slice would take no damage.
        if (error.empty() && tables.format >= 2 && !slice.vitals
            && std::any_of(slice.slots.begin(), slice.slots.end(), [](const SpellSlot& slot) { return slot.type == kVitalsStateType; })) {
            error = name + " has a Vitals slot of the type 15 with no vitals row";
        }
        const auto time = [](float value) { return std::isfinite(value) && value >= 0.0F; };
        if (error.empty() && slice.autoCast
            && (!time(slice.autoCast->lifetime) || !time(slice.autoCast->lifetimeOnHit) || !time(slice.autoCast->initialDelay)
                || !time(slice.autoCast->cooldown) || slice.autoCast->spell.empty())) {
            error = name + " has an AutoSpell with a time that is not a time or with no spell";
        }
        for (const SpellVolume& volume : slice.volumes) {
            if (error.empty() && (!time(volume.radius) || !std::isfinite(volume.activeAfter))) {
                error = name + " has a detection volume with a radius or a time that is not correct";
            }
        }
        for (const SpellOffense& offense : slice.offenses) {
            if (error.empty() && !time(offense.radius)) {
                error = name + " has an offense with a radius that is not a distance";
            }
        }
        for (const SpellTimeline& timeline : slice.timelines) {
            if (error.empty() && !std::isfinite(timeline.destroyAfter)) {
                error = name + " has a timeline with a destroy time that is not a number";
            }
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
        // Each DelayedEvent slot has its event row (code review of stage 3.3b-S2a, finding F3): with no row the record has
        // no delayed event state for the slot, a late observer then starts the full delay of the slice at its entry, and
        // the end of the slice could be lost.
        for (const SpellSlot& slot : slice.slots) {
            if (error.empty() && slot.type == kDelayedEventStateType && eventSlots.count(slot.slot) == 0) {
                error = name + " has a DelayedEvent slot with no delayed event";
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
    std::unordered_map<std::string, std::size_t> vitals;
    for (std::size_t index = 0; index < tables.vitals.size() && error.empty(); ++index) {
        const SpellVitalsRow& row = tables.vitals[index];
        const auto value = [](float number) { return std::isfinite(number) && number >= 0.0F; };
        if (row.id.empty() || !value(row.healthBaseMax) || !value(row.ownerHealthPart) || !value(row.physicalArmor)
            || !value(row.elementalArmor) || !value(row.gearScore)) {
            error = "the vitals row " + row.id + " has a value that is not correct";
        } else if (!vitals.emplace(lower_ascii(row.id), index).second) {
            error = "two vitals rows have the id " + row.id;
        }
    }
    // The vitals row of each slice with a Vitals component: a row of the list, or a row that the tool names as a row of a
    // different table (code review of stage 3.3b-S2b, finding F8: a missing row gave a Pylon with no health and no damage).
    std::set<std::string> elsewhere;
    for (const std::string& row : tables.vitalsNotInData) {
        elsewhere.insert(lower_ascii(row));
    }
    for (const SpellSlice& slice : tables.slices) {
        if (error.empty() && slice.vitals && !vitals.contains(lower_ascii(slice.vitals->row)) && !elsewhere.contains(lower_ascii(slice.vitals->row))) {
            error = "the slice " + slice.path + " names the vitals row " + slice.vitals->row + ", which the file does not have";
        }
    }
    if (!error.empty()) {
        m_error = error;
        return false;
    }
    m_tables = std::move(tables);
    m_spells = std::move(spells);
    m_vitals = std::move(vitals);
    m_error.clear();
    return true;
}

const SpellVitalsRow* SpellData::vitals_row(std::string_view id) const {
    const auto found = m_vitals.find(lower_ascii(id));
    return found == m_vitals.end() ? nullptr : &m_tables.vitals[found->second];
}

const SpellSlice* SpellData::slice_of(std::string_view spellId) const {
    const auto found = m_spells.find(lower_ascii(spellId));
    return found == m_spells.end() ? nullptr : &m_tables.slices[found->second];
}

} // namespace soulwarden::backend::gamedata

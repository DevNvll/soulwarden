#include "gamedata/stamina_data.h"

#include <algorithm>
#include <cmath>
#include <fstream>

#include <nlohmann/json.hpp>

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

constexpr std::uint64_t kFormat = 1;
/** The largest value of a stamina maximum, a cost, a rate, or a time of the file (a check of the form, not a rule). */
constexpr float kLargest = 100000.0F;
constexpr std::array<const char*, kLoadClassCount> kClassNames{"light", "medium", "heavy", "overburdened"};

bool number_of(const json& object, const char* member, float& out) {
    if (!object.is_object() || !object.contains(member) || !object[member].is_number() || !std::isfinite(object[member].get<double>())) {
        return false;
    }
    out = static_cast<float>(object[member].get<double>());
    return std::isfinite(out);
}

bool text_of(const json& object, const char* member, std::string& out) {
    if (!object.is_object() || !object.contains(member) || !object[member].is_string()) {
        return false;
    }
    out = object[member].get<std::string>();
    return !out.empty();
}

bool in_range(float value) {
    return std::isfinite(value) && value >= 0.0F && value <= kLargest;
}

bool read_fragment(const std::string& name, const json& item, DodgeFragment& fragment) {
    fragment.name = name;
    if (!item.is_object() || !number_of(item, "pay", fragment.pay) || !number_of(item, "canTransition", fragment.canTransition)
        || !item.contains("immune") || !item["immune"].is_array() || item["immune"].size() != 2 || !item["immune"][0].is_number()
        || !item["immune"][1].is_number() || !item.contains("cancel") || !item["cancel"].is_array()) {
        return false;
    }
    fragment.immuneStart = static_cast<float>(item["immune"][0].get<double>());
    fragment.immuneEnd = static_cast<float>(item["immune"][1].get<double>());
    for (const json& window : item["cancel"]) {
        if (!window.is_array() || window.size() != 3 || !window[0].is_number() || !(window[1].is_number() || window[1].is_null())
            || !window[2].is_boolean()) {
            return false;
        }
        DodgeCancel cancel;
        cancel.start = static_cast<float>(window[0].get<double>());
        if (window[1].is_number()) {
            cancel.end = static_cast<float>(window[1].get<double>());
        }
        cancel.buffered = window[2].get<bool>();
        fragment.cancel.push_back(cancel);
    }
    return true;
}

} // namespace

bool StaminaData::load(const std::string& path) {
    m_tables = {};
    m_loaded = false;
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
    StaminaTables tables;
    const json empty = json::object();
    const json& component = document.contains("component") ? document["component"] : empty;
    const json& vitals = document.contains("vitals") ? document["vitals"] : empty;
    const json& load = document.contains("load") ? document["load"] : empty;
    const json& dodge = document.contains("dodge") ? document["dodge"] : empty;
    const json& exhausted = document.contains("exhausted") ? document["exhausted"] : empty;
    if (!number_of(component, "min", tables.minimum) || !number_of(component, "max", tables.maximum)
        || !number_of(component, "regenRate", tables.regenRate) || !number_of(component, "blockRegenRate", tables.blockRegenRate)
        || !number_of(component, "maxWindedTime", tables.maxWindedTime) || !number_of(vitals, "regenDelay", tables.regenDelay)
        || !number_of(vitals, "regenDelayOnEmpty", tables.regenDelayOnEmpty)
        || !number_of(vitals, "fullyDepletedDelay", tables.fullyDepletedDelay) || !number_of(dodge, "regenDelayOnExit", tables.dodgeExitDelay)
        || !number_of(exhausted, "enter", tables.exhaustedEnter) || !number_of(exhausted, "exit", tables.exhaustedExit)) {
        m_error = "a value of the members \"component\", \"vitals\", \"dodge\", or \"exhausted\" is not there or is not a finite number";
        return false;
    }
    if (!load.is_object() || !load.contains("costMult") || !load.contains("regenMult")) {
        m_error = "the member \"load\" has no factors";
        return false;
    }
    for (std::size_t index = 0; index < kLoadClassCount; ++index) {
        if (!number_of(load["costMult"], kClassNames[index], tables.costMult[index])
            || !number_of(load["regenMult"], kClassNames[index], tables.regenMult[index])) {
            m_error = std::string("the load class ") + kClassNames[index] + " has no stamina factors";
            return false;
        }
    }
    if (!text_of(dodge, "action", tables.dodgeAction)) {
        m_error = "the member \"dodge\" has no action";
        return false;
    }
    if (!dodge.contains("classes") || !dodge["classes"].is_object() || !dodge.contains("fragments") || !dodge["fragments"].is_object()) {
        m_error = "the member \"dodge\" has no classes or no fragments";
        return false;
    }
    for (std::size_t index = 0; index < tables.dodge.size(); ++index) {
        const json& entry = dodge["classes"].contains(kClassNames[index]) ? dodge["classes"][kClassNames[index]] : empty;
        DodgeClass& item = tables.dodge[index];
        if (!text_of(entry, "fragment", item.fragment) || !text_of(entry, "costId", item.costId) || !number_of(entry, "cost", item.cost)) {
            m_error = std::string("the dodge of the load class ") + kClassNames[index] + " is not complete";
            return false;
        }
    }
    for (const auto& [name, item] : dodge["fragments"].items()) {
        DodgeFragment fragment;
        if (!read_fragment(name, item, fragment)) {
            m_error = "the dodge fragment " + name + " is not complete";
            return false;
        }
        tables.fragments.push_back(std::move(fragment));
    }
    if (dodge.contains("costsNotApplied") && dodge["costsNotApplied"].is_array()) {
        tables.costsNotApplied = dodge["costsNotApplied"].size();
    }
    return set(std::move(tables));
}

bool StaminaData::set(StaminaTables tables) {
    m_loaded = false;
    const auto refuse = [this](const std::string& cause) {
        m_error = cause;
        m_tables = {};
        return false;
    };
    if (!in_range(tables.minimum) || tables.minimum != 0.0F || !in_range(tables.maximum) || tables.maximum <= 0.0F
        || !in_range(tables.regenRate) || !in_range(tables.blockRegenRate) || !in_range(tables.maxWindedTime)
        || !in_range(tables.regenDelay) || !in_range(tables.regenDelayOnEmpty) || !in_range(tables.fullyDepletedDelay)
        || !in_range(tables.dodgeExitDelay)) {
        return refuse("a value of the stamina component or of the base vitals is out of its range");
    }
    if (tables.exhaustedEnter != 0.0F || tables.exhaustedExit <= 0.0F || tables.exhaustedExit > 1.0F) {
        return refuse("the exhausted state does not start at 0 and end at a part of 1 or less");
    }
    for (std::size_t index = 0; index < kLoadClassCount; ++index) {
        if (!in_range(tables.costMult[index]) || !in_range(tables.regenMult[index])) {
            return refuse("a stamina factor of a load class is out of its range");
        }
    }
    for (const DodgeFragment& fragment : tables.fragments) {
        bool correct = in_range(fragment.pay) && in_range(fragment.canTransition) && fragment.pay <= fragment.canTransition
                       && fragment.immuneStart == 0.0F && in_range(fragment.immuneEnd) && fragment.immuneEnd > 0.0F;
        for (const DodgeCancel& cancel : fragment.cancel) {
            correct = correct && in_range(cancel.start) && (!cancel.end || (in_range(*cancel.end) && *cancel.end >= cancel.start))
                      && (!cancel.buffered || cancel.end.has_value());
            // A next dodge of a window starts at the end of a buffered window and at the start of an instant one: not before the payment
            // of the dodge that plays (code review of stage 3.4f-B, finding CS-R5: a chain of replacements before the mark paid nothing).
            const float replaces = cancel.buffered && cancel.end ? *cancel.end : cancel.start;
            correct = correct && replaces >= fragment.pay;
        }
        if (!correct) {
            return refuse("the times of the dodge fragment " + fragment.name + " do not fit together");
        }
    }
    m_tables = std::move(tables);
    for (const DodgeClass& dodge : m_tables.dodge) {
        if (dodge.fragment.empty() || dodge.costId.empty() || !in_range(dodge.cost) || fragment_of(dodge) == nullptr) {
            return refuse("the dodge class " + dodge.costId + " has no cost or no fragment of the file");
        }
    }
    m_error.clear();
    m_loaded = true;
    return true;
}

const DodgeClass* StaminaData::dodge_of(LoadClass load) const {
    const auto index = static_cast<std::size_t>(load);
    return m_loaded && index < m_tables.dodge.size() ? &m_tables.dodge[index] : nullptr;
}

const DodgeFragment* StaminaData::fragment_of(const DodgeClass& dodge) const {
    const auto found = std::find_if(m_tables.fragments.begin(), m_tables.fragments.end(),
                                    [&](const DodgeFragment& fragment) { return fragment.name == dodge.fragment; });
    return found != m_tables.fragments.end() ? &*found : nullptr;
}

} // namespace soulwarden::backend::gamedata

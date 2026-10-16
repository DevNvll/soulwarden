#include "gamedata/weapon_actions.h"

#include <algorithm>
#include <cmath>
#include <fstream>

#include <nlohmann/json.hpp>

#include "gamedata/progression_tables.h"

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

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
    return std::isfinite(out);
}

/** A number, or null for "no value" (`absent`). */
bool optional_number(const json& value, float& out, float absent) {
    if (value.is_null()) {
        out = absent;
        return true;
    }
    return number_of(value, out);
}

bool texts_of(const json& value, std::vector<std::string>& out) {
    if (!value.is_array()) {
        return false;
    }
    for (const json& item : value) {
        std::string text;
        if (!text_of(item, text) || text.empty()) {
            return false;
        }
        out.push_back(text);
    }
    return true;
}

bool read_transition(const json& value, AttackTransition& out) {
    std::string kind;
    std::string input;
    if (!value.is_array() || value.size() != 6 || !text_of(value[0], kind) || !number_of(value[1], out.start)
        || !optional_number(value[2], out.end, -1.0F) || !text_of(value[3], input) || !text_of(value[4], out.toName)
        || !text_of(value[5], out.condition)) {
        return false;
    }
    if (kind == "buffered") {
        out.kind = TransitionKind::buffered;
    } else if (kind == "instant") {
        out.kind = TransitionKind::instant;
    } else {
        return false;
    }
    if (input == "always") {
        out.input = AttackInput::always;
    } else if (input == "virtual") {
        out.input = AttackInput::virtualInput;
    } else if (input == "press") {
        out.input = AttackInput::press;
    } else if (input == "notHeld") {
        out.input = AttackInput::notHeld;
    } else if (input == "released") {
        out.input = AttackInput::released;
    } else {
        return false;
    }
    return !out.toName.empty();
}

bool read_shape(const json& value, AttackShape& out) {
    if (!value.is_array() || value.size() != 13 || !text_of(value[0], out.type) || !number_of(value[1], out.radius)
        || !number_of(value[2], out.halfHeight) || !text_of(value[12], out.joint)) {
        return false;
    }
    for (int axis = 0; axis < 3; ++axis) {
        if (!number_of(value[3 + axis], out.box[axis]) || !number_of(value[6 + axis], out.offset[axis])
            || !number_of(value[9 + axis], out.scale[axis])) {
            return false;
        }
    }
    return true;
}

bool read_strike(const json& value, AttackStrike& out) {
    if (!value.is_array() || value.size() != 5 || !text_of(value[0], out.key) || !number_of(value[1], out.start)
        || !number_of(value[2], out.end) || !value[3].is_array() || !value[4].is_array()) {
        return false;
    }
    for (const json& item : value[3]) {
        StrikeRow row;
        if (!item.is_array() || item.size() != 3 || !text_of(item[0], row.row) || row.row.empty() || !texts_of(item[1], row.needNames)
            || !texts_of(item[2], row.forbidNames)) {
            return false;
        }
        for (const std::string& name : row.needNames) {
            row.needs.push_back(progression_id(name));
        }
        for (const std::string& name : row.forbidNames) {
            row.forbids.push_back(progression_id(name));
        }
        out.rows.push_back(std::move(row));
    }
    for (const json& item : value[4]) {
        AttackShape shape;
        if (!read_shape(item, shape)) {
            return false;
        }
        out.shapes.push_back(std::move(shape));
    }
    out.key = lower_ascii(out.key);
    return true;
}

bool read_fragment(const json& value, AttackFragment& out) {
    if (!value.is_object()) {
        return false;
    }
    for (const char* member : {"name", "tags", "length", "canTransition", "transitions", "strikes", "payments", "dodge"}) {
        if (!value.contains(member)) {
            return false;
        }
    }
    if (!text_of(value["name"], out.name) || out.name.empty() || !text_of(value["tags"], out.tags) || !number_of(value["length"], out.length)
        || !optional_number(value["canTransition"], out.canTransition, -1.0F) || !value["transitions"].is_array()
        || !value["strikes"].is_array() || !value["payments"].is_array() || !value["dodge"].is_array()) {
        return false;
    }
    for (const json& item : value["transitions"]) {
        AttackTransition transition;
        if (!read_transition(item, transition)) {
            return false;
        }
        out.transitions.push_back(std::move(transition));
    }
    for (const json& item : value["strikes"]) {
        AttackStrike strike;
        if (!read_strike(item, strike)) {
            return false;
        }
        out.strikes.push_back(std::move(strike));
    }
    for (const json& item : value["payments"]) {
        AttackPayment payment;
        std::string kind;
        if (!item.is_array() || item.size() != 5 || !number_of(item[0], payment.time) || !text_of(item[1], kind)
            || !text_of(item[2], payment.cost) || payment.cost.empty() || !number_of(item[3], payment.multiplier)
            || !text_of(item[4], payment.condition) || (kind != "stamina" && kind != "mana")) {
            return false;
        }
        payment.mana = kind == "mana";
        out.payments.push_back(std::move(payment));
    }
    for (const json& item : value["dodge"]) {
        DodgeWindow window;
        if (!item.is_array() || item.size() != 3 || !number_of(item[0], window.start) || !optional_number(item[1], window.end, -1.0F)
            || !item[2].is_boolean()) {
            return false;
        }
        window.buffered = item[2].get<bool>();
        out.dodge.push_back(window);
    }
    return true;
}

bool read_weapon(const json& value, WeaponActionSet& out, std::string& startName) {
    if (!value.is_object() || !value.contains("tags") || !value.contains("start") || !value.contains("fragments")
        || !texts_of(value["tags"], out.tags) || out.tags.empty() || !text_of(value["start"], startName) || !value["fragments"].is_array()) {
        return false;
    }
    for (const json& item : value["fragments"]) {
        AttackFragment fragment;
        if (!read_fragment(item, fragment)) {
            return false;
        }
        out.fragments.push_back(std::move(fragment));
    }
    return true;
}

} // namespace

std::string weapon_tag_key(std::string_view tags) {
    std::vector<std::string> parts;
    std::size_t begin = 0;
    while (begin <= tags.size()) {
        std::size_t end = tags.find_first_of(",+", begin);
        if (end == std::string_view::npos) {
            end = tags.size();
        }
        std::string part = lower_ascii(tags.substr(begin, end - begin));
        part.erase(0, part.find_first_not_of(' '));
        part.erase(part.find_last_not_of(' ') + 1);
        if (!part.empty()) {
            parts.push_back(std::move(part));
        }
        begin = end + 1;
    }
    std::sort(parts.begin(), parts.end());
    parts.erase(std::unique(parts.begin(), parts.end()), parts.end());
    std::string key;
    for (const std::string& part : parts) {
        key += (key.empty() ? "" : ",") + part;
    }
    return key;
}

std::int32_t WeaponActionSet::fragment_index(std::string_view name) const {
    for (std::size_t index = 0; index < fragments.size(); ++index) {
        if (fragments[index].name == name) {
            return static_cast<std::int32_t>(index);
        }
    }
    return -1;
}

bool WeaponActions::load(const std::string& path) {
    m_tables = {};
    m_index.clear();
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        m_error = "the file is not there";
        return false;
    }
    const json document = json::parse(input, nullptr, false);
    if (!document.is_object() || !document.contains("format") || !document["format"].is_number_unsigned()
        || document["format"].get<std::uint64_t>() != kFormat || !document.contains("weapons") || !document["weapons"].is_array()) {
        m_error = "the file is not a JSON object of the format 1 with a list \"weapons\"";
        return false;
    }
    WeaponActionTables tables;
    std::size_t fragments = 0;
    std::size_t strikes = 0;
    std::size_t transitions = 0;
    for (const json& item : document["weapons"]) {
        WeaponActionSet weapon;
        std::string startName;
        if (!read_weapon(item, weapon, startName)) {
            m_error = "a weapon of the list \"weapons\" is not complete";
            return false;
        }
        const std::int32_t start = weapon.fragment_index(startName);
        if (start < 0) {
            m_error = "a weapon has no fragment " + startName;
            return false;
        }
        weapon.start = static_cast<std::uint32_t>(start);
        fragments += weapon.fragments.size();
        for (const AttackFragment& fragment : weapon.fragments) {
            strikes += fragment.strikes.size();
            transitions += fragment.transitions.size();
        }
        tables.weapons.push_back(std::move(weapon));
    }
    if (document.contains("counts")) {
        const json& counts = document["counts"];
        const auto count = [&](const char* name, std::size_t size) {
            return counts.is_object() && counts.contains(name) && counts[name].is_number_unsigned() && counts[name].get<std::uint64_t>() == size;
        };
        if (!count("weapons", tables.weapons.size()) || !count("fragments", fragments) || !count("strikes", strikes)
            || !count("transitions", transitions)) {
            m_error = "a list does not have the count of the file";
            return false;
        }
    }
    return set(std::move(tables));
}

bool WeaponActions::set(WeaponActionTables tables) {
    m_tables = {};
    m_index.clear();
    std::unordered_map<std::string, std::size_t> index;
    const auto window = [](float start, float end) { return std::isfinite(start) && start >= 0.0F && (end < 0.0F || end >= start); };
    for (std::size_t number = 0; number < tables.weapons.size(); ++number) {
        WeaponActionSet& weapon = tables.weapons[number];
        std::string tags;
        for (const std::string& tag : weapon.tags) {
            tags += (tags.empty() ? "" : ",") + tag;
        }
        weapon.key = weapon_tag_key(tags);
        if (weapon.key.empty() || !index.emplace(weapon.key, number).second) {
            m_error = "two weapons have the tags " + weapon.key + ", or a weapon has no tag";
            return false;
        }
        if (weapon.start >= weapon.fragments.size()) {
            m_error = "the first fragment of the weapon " + weapon.key + " is not in its list";
            return false;
        }
        for (AttackFragment& fragment : weapon.fragments) {
            if (!std::isfinite(fragment.length) || fragment.length < 0.0F || weapon.fragment_index(fragment.name) != &fragment - weapon.fragments.data()) {
                m_error = "the fragment " + fragment.name + " of the weapon " + weapon.key + " has a wrong length or a second entry";
                return false;
            }
            for (AttackTransition& transition : fragment.transitions) {
                const std::int32_t to = weapon.fragment_index(transition.toName);
                if (to < 0 || !window(transition.start, transition.end)) {
                    m_error = "a transition of " + fragment.name + " (" + weapon.key + ") goes to " + transition.toName
                              + ", which the weapon does not have, or its window is wrong";
                    return false;
                }
                transition.to = static_cast<std::uint32_t>(to);
            }
            for (const AttackStrike& strike : fragment.strikes) {
                if (strike.key.empty() || strike.rows.empty() || !window(strike.start, strike.end) || strike.end < 0.0F) {
                    m_error = "a strike of " + fragment.name + " (" + weapon.key + ") has no key, no row, or a wrong window";
                    return false;
                }
            }
            for (const DodgeWindow& dodge : fragment.dodge) {
                if (!window(dodge.start, dodge.end)) {
                    m_error = "a dodge window of " + fragment.name + " (" + weapon.key + ") is wrong";
                    return false;
                }
            }
            for (const AttackPayment& payment : fragment.payments) {
                if (!std::isfinite(payment.time) || payment.time < 0.0F || !std::isfinite(payment.multiplier)) {
                    m_error = "a payment of " + fragment.name + " (" + weapon.key + ") is wrong";
                    return false;
                }
            }
        }
    }
    m_tables = std::move(tables);
    m_index = std::move(index);
    m_error.clear();
    return true;
}

const WeaponActionSet* WeaponActions::of_tags(std::string_view tags) const {
    const auto found = m_index.find(weapon_tag_key(tags));
    return found == m_index.end() ? nullptr : &m_tables.weapons[found->second];
}

} // namespace soulwarden::backend::gamedata

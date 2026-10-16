#include "gamedata/weapon_actions.h"

#include <algorithm>
#include <cmath>
#include <fstream>

#include <nlohmann/json.hpp>

#include "gamedata/progression_tables.h"

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

/** Format 1: the basic attacks (stage 3.3b-M1). Format 2 (stage 3.3b-M2): also the abilities of each weapon. */
constexpr std::uint64_t kFormat = 2;

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

bool read_literal(const json& value, ConditionLiteral& out) {
    std::string kind;
    if (!value.is_array() || value.size() != 3 || !text_of(value[0], kind) || !value[2].is_boolean()) {
        return false;
    }
    out.negated = value[2].get<bool>();
    if (kind == "hits") {
        if (!value[1].is_number_unsigned()) {
            return false;
        }
        out.kind = LiteralKind::hits;
        out.count = static_cast<std::uint32_t>(value[1].get<std::uint64_t>());
        return true;
    }
    if (!text_of(value[1], out.text) || out.text.empty()) {
        return false;
    }
    if (kind == "node" || kind == "perk") {
        out.kind = kind == "node" ? LiteralKind::node : LiteralKind::perk;
        out.id = progression_id(out.text);
    } else if (kind == "input") {
        if (out.text == "attack") {
            out.kind = LiteralKind::attack;
        } else if (out.text == "block") {
            out.kind = LiteralKind::block;
        } else if (out.text == "collided") {
            out.kind = LiteralKind::collided;
        } else {
            return false;
        }
    } else if (kind == "playing") {
        out.kind = LiteralKind::playing;
    } else {
        return false;
    }
    return true;
}

bool read_transition(const json& value, AttackTransition& out, bool format2) {
    std::string kind;
    std::string input;
    if (!value.is_array() || value.size() != (format2 ? 7U : 6U) || !text_of(value[0], kind) || !number_of(value[1], out.start)
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
    } else if (format2 && input == "ability") {
        out.input = AttackInput::ability;
        if (!text_of(value[6], out.ability) || out.ability.empty()) {
            return false;
        }
        out.abilityId = progression_id(out.ability);
    } else if (format2 && input == "terms") {
        out.input = AttackInput::terms;
        if (!value[6].is_array()) {
            return false;
        }
        for (const json& term : value[6]) {
            if (!term.is_array()) {
                return false;
            }
            std::vector<ConditionLiteral> literals;
            for (const json& item : term) {
                ConditionLiteral literal;
                if (!read_literal(item, literal)) {
                    return false;
                }
                literals.push_back(std::move(literal));
            }
            out.terms.push_back(std::move(literals));
        }
    } else {
        return false;
    }
    if (format2 && out.input != AttackInput::ability && out.input != AttackInput::terms && !value[6].is_null()) {
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

bool read_strike(const json& value, AttackStrike& out, bool format2) {
    if (!value.is_array() || value.size() != 5 || !text_of(value[0], out.key) || !number_of(value[1], out.start)
        || !number_of(value[2], out.end) || !value[3].is_array() || !value[4].is_array()) {
        return false;
    }
    for (const json& item : value[3]) {
        StrikeRow row;
        if (!item.is_array() || item.size() != (format2 ? 4U : 3U) || !text_of(item[0], row.row) || row.row.empty()
            || !texts_of(item[1], row.needNames) || !texts_of(item[2], row.forbidNames)) {
            return false;
        }
        if (format2) {
            if (!item[3].is_number_unsigned()) {
                return false;
            }
            row.shape = static_cast<std::uint32_t>(item[3].get<std::uint64_t>());
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
    // The shape of each row is a shape of the strike (format 2).
    return std::all_of(out.rows.begin(), out.rows.end(), [&](const StrikeRow& row) { return out.shapes.empty() || row.shape < out.shapes.size(); });
}

bool read_fragment(const json& value, AttackFragment& out, bool format2) {
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
        if (!read_transition(item, transition, format2)) {
            return false;
        }
        out.transitions.push_back(std::move(transition));
    }
    for (const json& item : value["strikes"]) {
        AttackStrike strike;
        if (!read_strike(item, strike, format2)) {
            return false;
        }
        out.strikes.push_back(std::move(strike));
    }
    if (format2) {
        if (!value.contains("casts") || !value.contains("markers") || !value["casts"].is_array() || !value["markers"].is_array()) {
            return false;
        }
        for (const json& item : value["casts"]) {
            FragmentCast cast;
            if (!item.is_array() || item.size() != 4 || !number_of(item[0], cast.time) || cast.time < 0.0F || !text_of(item[1], cast.spell)
                || cast.spell.empty() || !texts_of(item[2], cast.needs) || !texts_of(item[3], cast.forbids)) {
                return false;
            }
            out.casts.push_back(std::move(cast));
        }
        for (const json& item : value["markers"]) {
            FragmentMarker marker;
            if (!item.is_array() || item.size() != 2 || !text_of(item[0], marker.name) || marker.name.empty() || !number_of(item[1], marker.time)
                || marker.time < 0.0F) {
                return false;
            }
            out.markers.push_back(std::move(marker));
        }
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

bool read_selection_lists(const json& value, std::size_t from, std::vector<std::string>* lists[4]) {
    for (std::size_t index = 0; index < 4; ++index) {
        if (!texts_of(value[from + index], *lists[index])) {
            return false;
        }
    }
    return true;
}

bool read_ability(const json& value, AbilityAction& out) {
    if (!value.is_object() || !value.contains("ability") || !value.contains("action") || !value.contains("fragment")
        || !value.contains("selections") || !value.contains("casts") || !text_of(value["ability"], out.ability) || out.ability.empty()
        || !text_of(value["action"], out.action) || !text_of(value["fragment"], out.fragmentName) || out.fragmentName.empty()
        || !value["selections"].is_array() || !value["casts"].is_array()) {
        return false;
    }
    out.abilityId = progression_id(out.ability);
    for (const json& item : value["selections"]) {
        AbilityAction::Selection selection;
        std::vector<std::string>* lists[4] = {&selection.needs, &selection.forbids, &selection.perks, &selection.perkForbids};
        if (!item.is_array() || item.size() != 5 || !text_of(item[0], selection.fragmentName) || selection.fragmentName.empty()
            || !read_selection_lists(item, 1, lists)) {
            return false;
        }
        out.selections.push_back(std::move(selection));
    }
    for (const json& item : value["casts"]) {
        AbilityAction::Cast cast;
        std::vector<std::string>* lists[4] = {&cast.needs, &cast.forbids, &cast.perks, &cast.perkForbids};
        if (!item.is_array() || item.size() != 6 || !text_of(item[0], cast.spell) || cast.spell.empty() || !read_selection_lists(item, 1, lists)
            || !text_of(item[5], cast.marker)) {
            return false;
        }
        out.casts.push_back(std::move(cast));
    }
    return true;
}

bool read_weapon(const json& value, WeaponActionSet& out, std::string& startName, bool format2) {
    if (!value.is_object() || !value.contains("tags") || !value.contains("start") || !value.contains("fragments")
        || !texts_of(value["tags"], out.tags) || out.tags.empty() || !text_of(value["start"], startName) || !value["fragments"].is_array()) {
        return false;
    }
    for (const json& item : value["fragments"]) {
        AttackFragment fragment;
        if (!read_fragment(item, fragment, format2)) {
            return false;
        }
        out.fragments.push_back(std::move(fragment));
    }
    if (format2) {
        if (!value.contains("abilities") || !value["abilities"].is_array() || !value.contains("nodeUses") || !value["nodeUses"].is_array()) {
            return false;
        }
        for (const json& item : value["nodeUses"]) {
            NodeUse use;
            if (!item.is_array() || item.size() != 3 || !text_of(item[0], use.node) || use.node.empty() || !text_of(item[1], use.use)
                || !text_of(item[2], use.place)) {
                return false;
            }
            use.nodeId = progression_id(use.node);
            out.nodeUses.push_back(std::move(use));
        }
        for (const json& item : value["abilities"]) {
            AbilityAction ability;
            if (!read_ability(item, ability)) {
                return false;
            }
            out.abilities.push_back(std::move(ability));
        }
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
        || document["format"].get<std::uint64_t>() < 1 || document["format"].get<std::uint64_t>() > kFormat || !document.contains("weapons")
        || !document["weapons"].is_array()) {
        m_error = "the file is not a JSON object of the format 1 or 2 with a list \"weapons\"";
        return false;
    }
    const bool format2 = document["format"].get<std::uint64_t>() == 2;
    WeaponActionTables tables;
    std::size_t fragments = 0;
    std::size_t strikes = 0;
    std::size_t transitions = 0;
    for (const json& item : document["weapons"]) {
        WeaponActionSet weapon;
        std::string startName;
        if (!read_weapon(item, weapon, startName, format2)) {
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
            for (const FragmentCast& cast : fragment.casts) {
                if (!std::isfinite(cast.time) || cast.time < 0.0F || cast.spell.empty()) {
                    m_error = "a spell cast of " + fragment.name + " (" + weapon.key + ") is wrong";
                    return false;
                }
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
                // A strike that starts after the end of a fragment of a known length cannot be a strike of that fragment (code review
                // of stage 3.3b-M1, finding M1-R5). The length 0 is "not known".
                if (fragment.length > 0.0F && strike.start > fragment.length + 0.0005F) {
                    m_error = "a strike of " + fragment.name + " (" + weapon.key + ") starts after the end of the fragment";
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
    // The abilities (format 2): their fragments and the fragments of their selections are fragments of the weapon.
    for (WeaponActionSet& weapon : tables.weapons) {
        for (AbilityAction& ability : weapon.abilities) {
            const std::int32_t fragment = weapon.fragment_index(ability.fragmentName);
            if (fragment < 0 || ability.abilityId == 0) {
                m_error = "the ability " + ability.ability + " of the weapon " + weapon.key + " has no fragment " + ability.fragmentName;
                return false;
            }
            ability.fragment = static_cast<std::uint32_t>(fragment);
            for (AbilityAction::Selection& selection : ability.selections) {
                const std::int32_t selected = weapon.fragment_index(selection.fragmentName);
                if (selected < 0) {
                    m_error = "the ability " + ability.ability + " of the weapon " + weapon.key + " selects the fragment " + selection.fragmentName
                              + ", which the weapon does not have";
                    return false;
                }
                selection.fragment = static_cast<std::uint32_t>(selected);
            }
        }
    }
    m_tables = std::move(tables);
    m_index = std::move(index);
    m_error.clear();
    return true;
}

std::vector<const NodeUse*> WeaponActions::uses_of(std::uint32_t node) const {
    std::vector<const NodeUse*> uses;
    for (const WeaponActionSet& weapon : m_tables.weapons) {
        for (const NodeUse& use : weapon.nodeUses) {
            if (use.nodeId == node) {
                uses.push_back(&use);
            }
        }
    }
    return uses;
}

const AbilityAction* WeaponActionSet::ability(std::uint32_t abilityId) const {
    for (const AbilityAction& entry : abilities) {
        if (entry.abilityId == abilityId) {
            return &entry;
        }
    }
    return nullptr;
}

const WeaponActionSet* WeaponActions::of_tags(std::string_view tags) const {
    const auto found = m_index.find(weapon_tag_key(tags));
    return found == m_index.end() ? nullptr : &m_tables.weapons[found->second];
}

} // namespace soulwarden::backend::gamedata

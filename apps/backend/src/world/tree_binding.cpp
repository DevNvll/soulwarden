#include "world/tree_binding.h"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "gamedata/creature_data.h"

namespace soulwarden::backend::world::tree {
namespace {

using nlohmann::json;

constexpr std::array<const char*, kBindingKeys> kKeyNames = {"kind", "vitals", "slice", "serverTree", "creatureType", "family"};

/** @return The text in lower case (the letters A to Z): a value of a binding matches with no difference of case. */
void lower_into(std::string_view text, std::string& out) {
    out.assign(text);
    for (char& letter : out) {
        if (letter >= 'A' && letter <= 'Z') {
            letter = static_cast<char>(letter - 'A' + 'a');
        }
    }
}

} // namespace

std::string_view key_name(BindingKey key) {
    return key == BindingKey::none ? std::string_view("each creature") : std::string_view(kKeyNames[static_cast<std::size_t>(key)]);
}

std::string Selection::text() const {
    if (binding == nullptr) {
        return "no binding";
    }
    const std::string place = " (binding " + std::to_string(binding->place + 1) + ")";
    if (binding->key == BindingKey::none) {
        return "each creature" + place;
    }
    return std::string(key_name(binding->key)) + " " + std::string(value) + place;
}

bool BindingSet::parse(std::string_view text, const std::string& source, const TreeSet& trees, BindingSet& out, std::string& error) {
    json document;
    try {
        document = json::parse(text.begin(), text.end(), nullptr, true, true);
    } catch (const json::exception& failure) {
        error = std::string("not JSON with comments: ") + failure.what();
        return false;
    }
    if (!document.is_object()) {
        error = "a binding file must have one object";
        return false;
    }
    for (const auto& [member, item] : document.items()) {
        if (member != "$schema" && member != "format" && member != "bindings") {
            error = member + ": a binding file has no member \"" + member + "\" (format, bindings, $schema)";
            return false;
        }
    }
    const auto format = document.find("format");
    if (format == document.end() || !format->is_number_integer() || *format != kBindingFormat) {
        error = "format: the file must have \"format\": " + std::to_string(kBindingFormat);
        return false;
    }
    const auto list = document.find("bindings");
    if (list == document.end() || !list->is_array() || list->size() > kMaxBindings) {
        error = "bindings: the file must have \"bindings\": a list of " + std::to_string(kMaxBindings) + " bindings at most";
        return false;
    }
    BindingSet set;
    set.m_source = source;
    std::string lower;
    for (std::size_t place = 0; place < list->size(); ++place) {
        const json& item = (*list)[place];
        const std::string at = "bindings/" + std::to_string(place);
        if (!item.is_object()) {
            error = at + ": a binding must be an object with a key and \"tree\"";
            return false;
        }
        Binding binding;
        binding.place = place;
        for (const auto& [member, value] : item.items()) {
            if (member == "tree") {
                if (!value.is_string() || trees.find(value.get<std::string>()) == nullptr) {
                    error = at + "/tree: \"" + (value.is_string() ? value.get<std::string>() : value.dump()) + "\" is not a tree of the set";
                    return false;
                }
                binding.tree = value.get<std::string>();
                continue;
            }
            const auto key = std::find(kKeyNames.begin(), kKeyNames.end(), member);
            if (key == kKeyNames.end()) {
                error = at + "/" + member + ": a binding has no member \"" + member
                        + "\" (the keys kind, vitals, slice, serverTree, creatureType, family, and tree)";
                return false;
            }
            if (binding.key != BindingKey::none) {
                error = at + "/" + member + ": a binding has one key (it has \"" + std::string(key_name(binding.key)) + "\" too)";
                return false;
            }
            binding.key = static_cast<BindingKey>(key - kKeyNames.begin());
            const bool many = value.is_array();
            if ((!many && !value.is_string()) || (many && (value.empty() || value.size() > kMaxBindingValues))) {
                error = at + "/" + member + ": the value of a key must be a text or a list of 1 to " + std::to_string(kMaxBindingValues) + " texts";
                return false;
            }
            for (const json& entry : many ? value : json::array({value})) {
                if (!entry.is_string() || entry.get<std::string>().empty() || entry.get<std::string>().size() > 160) {
                    error = at + "/" + member + ": each value must be a text of 1 to 160 characters";
                    return false;
                }
                binding.values.push_back(entry.get<std::string>());
            }
        }
        if (binding.tree.empty()) {
            error = at + ": a binding must have \"tree\": the name of a tree";
            return false;
        }
        if (binding.key == BindingKey::none) {
            if (set.m_fallback) {
                error = at + ": the file has two bindings with no key (bindings " + std::to_string(*set.m_fallback + 1) + " and "
                        + std::to_string(place + 1) + ")";
                return false;
            }
            set.m_fallback = place;
        } else {
            std::unordered_map<std::string, std::size_t>& index = set.m_index[static_cast<std::size_t>(binding.key)];
            for (const std::string& value : binding.values) {
                lower_into(value, lower);
                const auto [found, added] = index.emplace(lower, place);
                if (!added) {
                    // A tie: two bindings of one key select different trees (or the same one twice) for one creature.
                    error = at + "/" + std::string(key_name(binding.key)) + ": \"" + value + "\" is in the binding " + std::to_string(found->second + 1)
                            + " too (a value has one binding of its key)";
                    return false;
                }
            }
        }
        set.m_bindings.push_back(std::move(binding));
    }
    out = std::move(set);
    return true;
}

const Binding* BindingSet::find(BindingKey key, std::string_view value, std::string& lower) const {
    const std::unordered_map<std::string, std::size_t>& index = m_index[static_cast<std::size_t>(key)];
    if (index.empty() || value.empty()) {
        return nullptr;
    }
    lower_into(value, lower);
    const auto found = index.find(lower);
    return found == index.end() ? nullptr : &m_bindings[found->second];
}

Selection BindingSet::select(const gamedata::CreatureKind& kind, const gamedata::CreatureSlice& slice) const {
    std::string lower;
    const auto match = [&](BindingKey key, std::string_view value) -> std::optional<Selection> {
        if (const Binding* binding = find(key, value, lower)) {
            return Selection{binding, value};
        }
        return std::nullopt;
    };
    if (auto found = match(BindingKey::kind, kind.id)) {
        return *found;
    }
    if (auto found = match(BindingKey::vitals, kind.vitals)) {
        return *found;
    }
    if (auto found = match(BindingKey::slice, slice.path)) {
        return *found;
    }
    for (const std::string& name : slice.serverTrees) {
        if (auto found = match(BindingKey::serverTree, name)) {
            return *found;
        }
    }
    if (auto found = match(BindingKey::creatureType, kind.creatureType)) {
        return *found;
    }
    if (auto found = match(BindingKey::family, kind.family)) {
        return *found;
    }
    if (m_fallback) {
        return Selection{&m_bindings[*m_fallback], {}};
    }
    return Selection{};
}

} // namespace soulwarden::backend::world::tree

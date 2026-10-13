#include "gamedata/slice_ids.h"

#include <cstdlib>
#include <fstream>

#include <nlohmann/json.hpp>

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

/** Reads a 64-bit id from its text of 16 hexadecimal digits: a JSON number cannot hold 64 bits. */
bool read_id(const json& parent, const char* key, std::uint64_t& value) {
    if (!parent.is_object() || !parent.contains(key) || !parent[key].is_string()) {
        return false;
    }
    const std::string text = parent[key].get<std::string>();
    if (text.size() != 16) {
        return false;
    }
    char* end = nullptr;
    value = std::strtoull(text.c_str(), &end, 16);
    return end == text.c_str() + text.size();
}

} // namespace

bool SliceIds::load(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    const json document = json::parse(input, nullptr, false);
    if (!document.is_object() || !document.contains("player") || !document["player"].is_object()) {
        return false;
    }
    const json& slice = document["player"];
    std::uint64_t root = 0;
    if (!read_id(slice, "root", root) || !slice.contains("components") || !slice["components"].is_array()) {
        return false;
    }
    std::vector<Component> components;
    for (const json& entry : slice["components"]) {
        Component component;
        if (!read_id(entry, "entity", component.entity) || !read_id(entry, "component", component.component)) {
            return false;
        }
        if (entry.contains("name") && entry["name"].is_string()) {
            component.name = entry["name"].get<std::string>();
        }
        components.push_back(std::move(component));
    }
    if (components.empty()) {
        return false;
    }
    m_root = root;
    m_components = std::move(components);
    return true;
}

const SliceIds::Component* SliceIds::find(std::uint64_t group, std::uint64_t target) const {
    const std::uint64_t key = m_root ^ group;
    for (const Component& component : m_components) {
        if ((component.entity ^ key) + component.component == target) {
            return &component;
        }
    }
    return nullptr;
}

} // namespace soulwarden::backend::gamedata

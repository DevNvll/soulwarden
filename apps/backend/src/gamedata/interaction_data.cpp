#include "gamedata/interaction_data.h"

#include <algorithm>
#include <fstream>

#include <nlohmann/json.hpp>

#include "world/position_index.h"

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

/** The form of the file that this code reads (tools/gamedata/interactions.py). */
constexpr std::uint64_t kFormat = 1;

constexpr std::pair<const char*, InteractionKind> kKindTexts[] = {
    {"lorePage", InteractionKind::lorePage},
    {"npc", InteractionKind::npc},
    {"storage", InteractionKind::storage},
    {"tradingPost", InteractionKind::tradingPost},
    {"shrine", InteractionKind::shrine},
    {"craftingStation", InteractionKind::craftingStation},
    {"camp", InteractionKind::camp},
    {"lootDrop", InteractionKind::lootDrop},
    {"gathering", InteractionKind::gathering},
    {"door", InteractionKind::door},
    {"restFurniture", InteractionKind::restFurniture},
    {"other", InteractionKind::other},
};

std::string lower_ascii(std::string_view text) {
    std::string lower(text);
    for (char& letter : lower) {
        if (letter >= 'A' && letter <= 'Z') {
            letter = static_cast<char>(letter + ('a' - 'A'));
        }
    }
    return lower;
}

std::uint64_t reference_key(std::uint64_t group, std::uint64_t target) {
    return group ^ ((target << 17) | (target >> 47));
}

/** The reading of one document. A member with a wrong form sets `error` and stops the reading. */
class Reader {
public:
    std::string error;

    [[nodiscard]] bool fail(std::string text) {
        if (error.empty()) {
            error = std::move(text);
        }
        return false;
    }

    bool text(const json& parent, const char* key, std::string& value, bool required = true) {
        if (!parent.contains(key)) {
            return !required || fail(std::string("no member ") + key);
        }
        if (!parent[key].is_string()) {
            return fail(std::string("the member ") + key + " is not a text");
        }
        value = parent[key].get<std::string>();
        return true;
    }

    bool unsigned_number(const json& parent, const char* key, std::uint64_t limit, std::uint64_t& value, bool required = true) {
        if (!parent.contains(key)) {
            return !required || fail(std::string("no member ") + key);
        }
        if (!parent[key].is_number_unsigned() || parent[key].get<std::uint64_t>() > limit) {
            return fail(std::string("the member ") + key + " is not a whole number in its range");
        }
        value = parent[key].get<std::uint64_t>();
        return true;
    }

    bool signed_number(const json& parent, const char* key, std::int64_t low, std::int64_t high, std::int64_t& value) {
        if (!parent.contains(key) || !parent[key].is_number_integer()) {
            return fail(std::string("the member ") + key + " is not a whole number");
        }
        value = parent[key].get<std::int64_t>();
        return value >= low && value <= high ? true : fail(std::string("the member ") + key + " is not in its range");
    }

    bool real(const json& parent, const char* key, float& value, bool required = true) {
        if (!parent.contains(key)) {
            return !required || fail(std::string("no member ") + key);
        }
        if (!parent[key].is_number()) {
            return fail(std::string("the member ") + key + " is not a number");
        }
        value = parent[key].get<float>();
        return std::isfinite(value) ? true : fail(std::string("the member ") + key + " is not a finite number");
    }

    bool flag(const json& parent, const char* key, bool& value, bool required = true) {
        if (!parent.contains(key)) {
            return !required || fail(std::string("no member ") + key);
        }
        if (!parent[key].is_boolean()) {
            return fail(std::string("the member ") + key + " is not true or false");
        }
        value = parent[key].get<bool>();
        return true;
    }

    bool hex64(const json& parent, const char* key, std::uint64_t& value) {
        std::string digits;
        if (!text(parent, key, digits)) {
            return false;
        }
        return parse_hex64(digits, value) ? true : fail(std::string("the member ") + key + " is not 16 hexadecimal digits");
    }

    static bool parse_hex64(const std::string& digits, std::uint64_t& value) {
        if (digits.size() != 16) {
            return false;
        }
        value = 0;
        for (const char digit : digits) {
            int nibble = -1;
            if (digit >= '0' && digit <= '9') {
                nibble = digit - '0';
            } else if (digit >= 'a' && digit <= 'f') {
                nibble = digit - 'a' + 10;
            } else if (digit >= 'A' && digit <= 'F') {
                nibble = digit - 'A' + 10;
            }
            if (nibble < 0) {
                return false;
            }
            value = (value << 4) | static_cast<std::uint64_t>(nibble);
        }
        return true;
    }

    bool hex128(const json& parent, const char* key, std::array<std::uint8_t, 16>& value) {
        std::string digits;
        if (!text(parent, key, digits)) {
            return false;
        }
        std::uint64_t high = 0;
        std::uint64_t low = 0;
        if (digits.size() != 32 || !parse_hex64(digits.substr(0, 16), high) || !parse_hex64(digits.substr(16), low)) {
            return fail(std::string("the member ") + key + " is not 32 hexadecimal digits");
        }
        for (int index = 0; index < 8; ++index) {
            value[static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(high >> (56 - 8 * index));
            value[static_cast<std::size_t>(8 + index)] = static_cast<std::uint8_t>(low >> (56 - 8 * index));
        }
        return true;
    }

    bool position(const json& parent, const char* key, state::WorldPosition& value) {
        if (!parent.contains(key) || !parent[key].is_array() || parent[key].size() != 3) {
            return fail(std::string("the member ") + key + " is not three numbers");
        }
        float* const axes[] = {&value.x, &value.y, &value.z};
        for (std::size_t index = 0; index < 3; ++index) {
            const json& number = parent[key][index];
            if (!number.is_number()) {
                return fail(std::string("the member ") + key + " is not three numbers");
            }
            *axes[index] = number.get<float>();
        }
        return world::valid_position(value) ? true : fail(std::string("the member ") + key + " is not a position of the world");
    }

    bool option(const json& entry, InteractionOption& row) {
        std::uint64_t number = 0;
        std::int64_t whole = 0;
        if (!entry.is_object() || !text(entry, "name", row.name) || !unsigned_number(entry, "id", 0xFFFFFFFFULL, number)) {
            return fail("an option has no name or no id");
        }
        row.id = static_cast<std::uint32_t>(number);
        if (!unsigned_number(entry, "action", 0xFF, number)) {
            return false;
        }
        row.action = static_cast<std::uint8_t>(number);
        if (!flag(entry, "committed", row.committed) || !real(entry, "delay", row.delay) || !real(entry, "hold", row.hold)
            || !flag(entry, "instantCancel", row.instantCancel) || !unsigned_number(entry, "cooldown", 0xFFFFFFFFULL, number)) {
            return false;
        }
        row.cooldown = static_cast<std::uint32_t>(number);
        if (!real(entry, "maxUsage", row.maxUsage) || !real(entry, "inactive", row.inactive)
            || !unsigned_number(entry, "requiredLevel", 0xFFFFFFFFULL, number)) {
            return false;
        }
        row.requiredLevel = static_cast<std::uint32_t>(number);
        if (!text(entry, "requiredItem", row.requiredItem) || !unsigned_number(entry, "requiredItemCount", 0xFFFFFFFFULL, number)) {
            return false;
        }
        row.requiredItemCount = static_cast<std::uint32_t>(number);
        if (!unsigned_number(entry, "requiredCurrency", 0xFFFFFFFFULL, number)
            || !signed_number(entry, "requiredAchievement", -1, 0x7FFFFFFF, whole)) {
            return false;
        }
        row.requiredCurrency = static_cast<std::uint32_t>(number);
        row.requiredAchievement = static_cast<std::int32_t>(whole);
        return text(entry, "unresolved", row.unresolved, false) && flag(entry, "statusRules", row.statusRules);
    }

    bool bound(const json& entry, InteractionBound& bound) {
        std::uint64_t number = 0;
        std::int64_t whole = 0;
        if (!entry.is_object() || !unsigned_number(entry, "slot", 4095, number)) {
            return fail("a bound has no slot from 0 to 4095");
        }
        bound.slot = static_cast<std::uint32_t>(number);
        if (!hex64(entry, "component", bound.component) || !hex64(entry, "target", bound.target)
            || !position(entry, "position", bound.position) || !real(entry, "rangeSq", bound.rangeSq)) {
            return false;
        }
        if (!(bound.rangeSq > 0.0F)) {
            return fail("a bound has a range that is not above 0");
        }
        if (!signed_number(entry, "capacity", -1, 0x7FFFFFFF, whole)) {
            return false;
        }
        bound.capacity = static_cast<std::int32_t>(whole);
        if (!flag(entry, "lineOfSight", bound.lineOfSight) || !flag(entry, "arcRequired", bound.arcRequired)
            || !real(entry, "arc", bound.arc) || !signed_number(entry, "lore", -1, 0x7FFFFFFF, whole)) {
            return false;
        }
        bound.lore = static_cast<std::int32_t>(whole);
        if (!entry.contains("options") || !entry["options"].is_array()) {
            return fail("a bound has no list options");
        }
        for (const json& option : entry["options"]) {
            BoundOption item;
            if (!option.is_array() || option.size() != 2 || !option[0].is_number_unsigned() || !option[1].is_string()
                || option[0].get<std::uint64_t>() > 0xFFFFFFFFULL || !parse_hex64(option[1].get<std::string>(), item.entity)) {
                return fail("an option of a bound is not [index, entity]");
            }
            item.option = option[0].get<std::uint32_t>();
            bound.options.push_back(item);
        }
        if (!entry.contains("requires") || !entry["requires"].is_object()) {
            return fail("a bound has no member requires");
        }
        const json& needs = entry["requires"];
        if (needs.contains("achievement")) {
            if (!signed_number(needs, "achievement", 0, 0x7FFFFFFF, whole)) {
                return false;
            }
            bound.requiredAchievement = static_cast<std::int32_t>(whole);
        }
        if (needs.contains("progression")) {
            const json& progression = needs["progression"];
            if (!progression.is_array() || progression.size() != 2 || !progression[0].is_string() || !progression[1].is_number_unsigned()
                || progression[1].get<std::uint64_t>() > 0xFFFFFFFFULL) {
                return fail("the requirement progression is not [id, rank]");
            }
            bound.progression = progression[0].get<std::string>();
            bound.progressionRank = progression[1].get<std::uint32_t>();
        }
        if (!flag(needs, "onlyOnQuest", bound.onlyOnQuest, false) || !flag(needs, "condition", bound.condition, false)) {
            return false;
        }
        if (needs.contains("unresolved")) {
            if (!needs["unresolved"].is_array()) {
                return fail("the requirement unresolved is not a list");
            }
            for (const json& name : needs["unresolved"]) {
                if (!name.is_string()) {
                    return fail("the requirement unresolved is not a list of texts");
                }
                bound.unresolved.push_back(name.get<std::string>());
            }
        }
        return text(entry, "tag", bound.tag, false);
    }

    bool object(const json& entry, InteractionObject& object) {
        std::string kind;
        std::uint64_t number = 0;
        if (!entry.is_object() || !text(entry, "key", object.key) || !text(entry, "kind", kind) || !text(entry, "slice", object.slice)
            || !hex128(entry, "asset", object.asset) || !unsigned_number(entry, "sub", 0xFFFFFFFFULL, number)) {
            return false;
        }
        const std::optional<InteractionKind> parsed = interaction_kind_of(kind);
        if (!parsed) {
            return fail("an object has the kind " + kind + ", which is not a kind of the file");
        }
        object.kind = *parsed;
        object.sub = static_cast<std::uint32_t>(number);
        if (!hex128(entry, "actor", object.actor) || !text(entry, "region", object.region) || !position(entry, "position", object.position)) {
            return false;
        }
        object.group = 0;
        for (int index = 7; index >= 0; --index) {
            object.group = (object.group << 8) | object.actor[static_cast<std::size_t>(index)];
        }
        if (!entry.contains("bounds") || !entry["bounds"].is_array() || entry["bounds"].empty()) {
            return fail("the object " + object.key + " has no bound");
        }
        for (const json& item : entry["bounds"]) {
            if (!bound(item, object.bounds.emplace_back())) {
                return fail("a bound of the object " + object.key + " is not correct");
            }
        }
        return true;
    }
};

} // namespace

const char* interaction_kind_text(InteractionKind kind) {
    for (const auto& [text, value] : kKindTexts) {
        if (value == kind) {
            return text;
        }
    }
    return "other";
}

std::uint32_t interaction_option_id(std::string_view name) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (char letter : name) {
        if (letter >= 'A' && letter <= 'Z') {
            letter = static_cast<char>(letter + ('a' - 'A'));
        }
        crc ^= static_cast<std::uint8_t>(letter);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1U) != 0 ? 0xEDB88320U : 0U);
        }
    }
    return ~crc;
}

std::optional<InteractionKind> interaction_kind_of(std::string_view text) {
    for (const auto& [name, value] : kKindTexts) {
        if (text == name) {
            return value;
        }
    }
    return std::nullopt;
}

bool InteractionData::load(const std::string& path) {
    *this = InteractionData{};
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        m_error = "the file is not there";
        return false;
    }
    const json document = json::parse(input, nullptr, false);
    if (!document.is_object()) {
        m_error = "the file is not a JSON object";
        return false;
    }
    if (!document.contains("format") || !document["format"].is_number_unsigned() || document["format"].get<std::uint64_t>() != kFormat) {
        m_error = "the file does not have the format 1";
        return false;
    }
    for (const char* list : {"options", "achievements", "objects"}) {
        if (!document.contains(list) || !document[list].is_array()) {
            m_error = std::string("the file does not have the list ") + list;
            return false;
        }
    }
    Reader reader;
    std::vector<InteractionOption> options;
    for (const json& entry : document["options"]) {
        if (!reader.option(entry, options.emplace_back())) {
            m_error = "option " + std::to_string(options.size() - 1) + ": " + reader.error;
            return false;
        }
    }
    std::vector<InteractionAchievement> achievements;
    for (const json& entry : document["achievements"]) {
        if (!entry.is_array() || entry.size() != 2 || !entry[0].is_string() || !entry[1].is_number_unsigned()
            || entry[1].get<std::uint64_t>() > 0x7FFFFFFFULL) {
            m_error = "achievement " + std::to_string(achievements.size()) + " is not [id, index]";
            return false;
        }
        achievements.push_back(InteractionAchievement{entry[0].get<std::string>(), entry[1].get<std::uint32_t>()});
    }
    std::vector<InteractionObject> objects;
    objects.reserve(document["objects"].size());
    for (const json& entry : document["objects"]) {
        if (!reader.object(entry, objects.emplace_back())) {
            m_error = "object " + std::to_string(objects.size() - 1) + ": " + reader.error;
            return false;
        }
    }
    // The counts of the tool: a file that lost a part of a list is not complete.
    if (document.contains("counts")) {
        const json& counts = document["counts"];
        std::size_t bounds = 0;
        for (const InteractionObject& object : objects) {
            bounds += object.bounds.size();
        }
        const std::pair<const char*, std::size_t> expected[] = {
            {"objects", objects.size()}, {"bounds", bounds}, {"options", options.size()}, {"achievements", achievements.size()}};
        for (const auto& [key, size] : expected) {
            if (!counts.is_object() || !counts.contains(key) || !counts[key].is_number_unsigned() || counts[key].get<std::uint64_t>() != size) {
                m_error = std::string("the list does not have the count ") + key + " of the file";
                return false;
            }
        }
    }
    return set(std::move(options), std::move(achievements), std::move(objects));
}

bool InteractionData::set(std::vector<InteractionOption> options,
                          std::vector<InteractionAchievement> achievements,
                          std::vector<InteractionObject> objects) {
    *this = InteractionData{};
    if (objects.empty()) {
        m_error = "the data has no object";
        return false;
    }
    InteractionData data;
    for (std::size_t index = 0; index < achievements.size(); ++index) {
        if (!data.m_achievementByLowerId.emplace(lower_ascii(achievements[index].id), static_cast<std::uint32_t>(index)).second) {
            m_error = "two achievements have the id " + achievements[index].id;
            return false;
        }
    }
    for (const InteractionOption& option : options) {
        if (option.requiredAchievement >= static_cast<std::int32_t>(achievements.size())) {
            m_error = "the option " + option.name + " names an achievement that the list does not have";
            return false;
        }
    }
    for (std::uint32_t index = 0; index < objects.size(); ++index) {
        const InteractionObject& object = objects[index];
        if (object.key.empty() || !data.m_byKey.emplace(lower_ascii(object.key), index).second) {
            m_error = "object " + std::to_string(index) + " has no key, or the key of a different object";
            return false;
        }
        if (!data.m_byGroup.emplace(object.group, index).second) {
            m_error = "the objects " + object.key + " and " + objects[data.m_byGroup[object.group]].key + " have one actor id";
            return false;
        }
        for (std::uint32_t boundIndex = 0; boundIndex < object.bounds.size(); ++boundIndex) {
            const InteractionBound& bound = object.bounds[boundIndex];
            if (!world::valid_position(bound.position) || !(bound.rangeSq > 0.0F) || bound.capacity < -1) {
                m_error = "a bound of the object " + object.key + " has a position, a range, or a capacity that is not correct";
                return false;
            }
            for (const BoundOption& item : bound.options) {
                if (item.option >= options.size()) {
                    m_error = "a bound of the object " + object.key + " names an option that the list does not have";
                    return false;
                }
            }
            if (bound.lore >= static_cast<std::int32_t>(achievements.size())
                || bound.requiredAchievement >= static_cast<std::int32_t>(achievements.size())) {
                m_error = "a bound of the object " + object.key + " names an achievement that the list does not have";
                return false;
            }
            const std::uint64_t key = reference_key(object.group, bound.target);
            const auto range = data.m_references.equal_range(key);
            for (auto other = range.first; other != range.second; ++other) {
                const InteractionObject& otherObject = objects[other->second.object];
                if (otherObject.group == object.group && otherObject.bounds[other->second.bound].target == bound.target) {
                    m_error = "two bounds have the reference of the object " + object.key;
                    return false;
                }
            }
            data.m_references.emplace(key, BoundRef{index, boundIndex});
            const auto cell = [](float value) { return static_cast<std::int32_t>(std::floor(value / kCellSize)); };
            data.m_cells[cell_key(cell(bound.position.x), cell(bound.position.y))].push_back(BoundRef{index, boundIndex});
        }
    }
    data.m_options = std::move(options);
    data.m_achievements = std::move(achievements);
    data.m_objects = std::move(objects);
    *this = std::move(data);
    return true;
}

std::optional<BoundRef> InteractionData::find(std::uint64_t group, std::uint64_t target) const {
    const auto range = m_references.equal_range(reference_key(group, target));
    for (auto found = range.first; found != range.second; ++found) {
        const InteractionObject& object = m_objects[found->second.object];
        if (object.group == group && object.bounds[found->second.bound].target == target) {
            return found->second;
        }
    }
    return std::nullopt;
}

const InteractionObject* InteractionData::find_key(std::string_view key) const {
    const std::optional<std::uint32_t> index = index_of(key);
    return index ? &m_objects[*index] : nullptr;
}

std::optional<std::uint32_t> InteractionData::index_of(std::string_view key) const {
    const auto found = m_byKey.find(lower_ascii(key));
    return found == m_byKey.end() ? std::nullopt : std::optional<std::uint32_t>(found->second);
}

std::optional<std::uint32_t> InteractionData::index_of_group(std::uint64_t group) const {
    const auto found = m_byGroup.find(group);
    return found == m_byGroup.end() ? std::nullopt : std::optional<std::uint32_t>(found->second);
}

std::optional<std::uint32_t> InteractionData::achievement_index(std::string_view id) const {
    const auto found = m_achievementByLowerId.find(lower_ascii(id));
    return found == m_achievementByLowerId.end() ? std::nullopt : std::optional<std::uint32_t>(m_achievements[found->second].index);
}

} // namespace soulwarden::backend::gamedata

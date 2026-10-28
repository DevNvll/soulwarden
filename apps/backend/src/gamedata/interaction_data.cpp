#include "gamedata/interaction_data.h"

#include <algorithm>
#include <fstream>
#include <set>

#include <nlohmann/json.hpp>

#include "gamedata/progression_tables.h"
#include "world/position_index.h"

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

/** The forms of the file that this code reads (tools/gamedata/interactions.py): 1 (stage 2.5a), 2 (stage 2.5b: the server children). */
constexpr std::uint64_t kFormatLevel = 1;
constexpr std::uint64_t kFormat = 2;
/** Format 3 (stage 3.5b.2): format 2 and the member lootContainers. */
constexpr std::uint64_t kFormatContainers = 3;
/** Format 4 (stage 2.5c): format 3 and the persons (the person children that the client makes, the member person of an object). */
constexpr std::uint64_t kFormatPersons = 4;

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

    /** @param local True: a place in the frame of a slice (finite numbers), not a position of the world. */
    bool position(const json& parent, const char* key, state::WorldPosition& value, bool local = false) {
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
        if (local) {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::abs(value.x) < 1000.0F
                           && std::abs(value.y) < 1000.0F && std::abs(value.z) < 1000.0F
                       ? true
                       : fail(std::string("the member ") + key + " is not a place in the frame of a slice (1000 m or less)");
        }
        return world::valid_position(value) ? true : fail(std::string("the member ") + key + " is not a position of the world");
    }

    /** A unit quaternion [x, y, z, w]. */
    bool rotation(const json& parent, const char* key, Rotation& value) {
        if (!parent.contains(key) || !parent[key].is_array() || parent[key].size() != 4) {
            return fail(std::string("the member ") + key + " is not four numbers");
        }
        float length = 0.0F;
        for (std::size_t index = 0; index < 4; ++index) {
            const json& number = parent[key][index];
            if (!number.is_number() || !std::isfinite(number.get<float>())) {
                return fail(std::string("the member ") + key + " is not four finite numbers");
            }
            value[index] = number.get<float>();
            length += value[index] * value[index];
        }
        return std::abs(length - 1.0F) < 0.001F ? true : fail(std::string("the member ") + key + " is not a unit quaternion");
    }

    bool assembly(const json& parent, std::optional<InteractionAssembly>& value) {
        if (!parent.contains("assembly") || parent["assembly"].is_null()) {
            value.reset();
            return true;
        }
        const json& entry = parent["assembly"];
        InteractionAssembly part;
        if (!entry.is_object() || !text(entry, "key", part.key) || !flag(entry, "fee", part.fee) || !hex64(entry, "entity", part.entity)
            || !hex64(entry, "component", part.component) || !flag(entry, "resolved", part.resolved)) {
            return fail("the member assembly is not {key, fee, entity, component, resolved}");
        }
        value = part;
        return true;
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

    /** @param local True: a bound of a template (the entity of the slice, a place in the frame of the slice, no target). */
    bool bound(const json& entry, InteractionBound& bound, bool local = false) {
        std::uint64_t number = 0;
        std::int64_t whole = 0;
        if (!entry.is_object() || !unsigned_number(entry, "slot", 4095, number)) {
            return fail("a bound has no slot from 0 to 4095");
        }
        bound.slot = static_cast<std::uint32_t>(number);
        if (!hex64(entry, "component", bound.component) || (local ? !hex64(entry, "entity", bound.entity) : !hex64(entry, "target", bound.target))
            || !position(entry, "position", bound.position, local) || !real(entry, "rangeSq", bound.rangeSq)) {
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
        if (!flag(needs, "onlyOnQuest", bound.onlyOnQuest, false) || !flag(needs, "condition", bound.condition, false)
            || !text(needs, "conditional", bound.conditional, false)) {
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
        return assembly(entry, object.assembly) && person(entry, object);
    }

    /** The member person of an object (stage 2.5c): the NPC row and the presence rule. */
    bool person(const json& entry, InteractionObject& object) {
        if (!entry.contains("person")) {
            return true;
        }
        const json& value = entry["person"];
        PersonBinding binding;
        if (!value.is_object() || !text(value, "npc", binding.npc) || !text(value, "variant", binding.variant, false)
            || !text(value, "table", binding.table, false) || !text(value, "spawner", binding.spawner, false)
            || !text(value, "unresolved", binding.unresolved, false) || !flag(value, "clientChild", binding.clientChild, false)) {
            return fail("the person of the object " + object.key + " is not correct");
        }
        if (value.contains("row")) {
            std::int64_t row = 0;
            if (!signed_number(value, "row", 0, 0xFFFF, row)) {
                return false;
            }
            binding.row = static_cast<std::int32_t>(row);
        }
        if (value.contains("presence")) {
            const json& presence = value["presence"];
            std::string mode;
            if (!presence.is_object() || !text(presence, "mode", mode) || !text(presence, "condition", binding.condition)) {
                return fail("the presence of the person " + object.key + " is not correct");
            }
            if (mode == "showWhen") {
                binding.presence = PersonPresence::showWhen;
            } else if (mode == "hideWhen") {
                binding.presence = PersonPresence::hideWhen;
            } else {
                return fail("the presence of the person " + object.key + " has the mode " + mode + ", which is not a mode of the file");
            }
            if (binding.condition.empty()) {
                return fail("the presence of the person " + object.key + " has no condition");
            }
        }
        if (binding.npc.empty() && binding.unresolved.empty()) {
            return fail("the person " + object.key + " has no NPC id and no cause");
        }
        binding.npcCrc = interaction_option_id(binding.npc);
        object.person = std::move(binding);
        return true;
    }

    bool slot(const json& slots, const char* key, std::uint32_t& value) {
        std::uint64_t number = 0;
        if (!unsigned_number(slots, key, 4095, number)) {
            return false;
        }
        value = static_cast<std::uint32_t>(number);
        return true;
    }

    bool template_entry(const json& entry, InteractionTemplate& value) {
        std::string kind;
        std::uint64_t number = 0;
        std::int64_t whole = 0;
        if (!entry.is_object() || !text(entry, "slice", value.slice) || !text(entry, "kind", kind) || !hex128(entry, "asset", value.asset)
            || !unsigned_number(entry, "sub", 0xFFFFFFFFULL, number) || !hex64(entry, "root", value.root)) {
            return fail("a template has no slice, kind, asset, sub id, or root");
        }
        const std::optional<InteractionKind> parsed = interaction_kind_of(kind);
        if (!parsed) {
            return fail("a template has the kind " + kind + ", which is not a kind of the file");
        }
        value.kind = *parsed;
        value.sub = static_cast<std::uint32_t>(number);
        if (!entry.contains("slots") || !entry["slots"].is_object()) {
            return fail("the template " + value.slice + " has no member slots");
        }
        const json& slots = entry["slots"];
        if (!slot(slots, "position", value.positionSlot) || !slot(slots, "buildable", value.buildableSlot)
            || !slot(slots, "ownership", value.ownershipSlot) || !slot(slots, "vitals", value.vitalsSlot)) {
            return false;
        }
        if (!signed_number(entry, "buildableState", -1, 255, whole)) {
            return false;
        }
        value.buildableState = static_cast<std::int32_t>(whole);
        if (!signed_number(entry, "permissionRule", -1, 255, whole)) {
            return false;
        }
        value.permissionRule = static_cast<std::int32_t>(whole);
        if (!assembly(entry, value.assembly) || !entry.contains("bounds") || !entry["bounds"].is_array() || !entry.contains("children")
            || !entry["children"].is_array()) {
            return fail("the template " + value.slice + " has no list bounds or children");
        }
        for (const json& item : entry["bounds"]) {
            if (!bound(item, value.bounds.emplace_back(), true)) {
                return fail("a bound of the template " + value.slice + " is not correct");
            }
        }
        for (const json& item : entry["children"]) {
            TemplateChild child;
            if (!item.is_object() || !unsigned_number(item, "template", 0xFFFFFFFFULL, number) || !position(item, "position", child.position, true)
                || !rotation(item, "rotation", child.rotation)) {
                return fail("a child of the template " + value.slice + " is not {template, position, rotation}");
            }
            child.templateIndex = static_cast<std::uint32_t>(number);
            value.children.push_back(child);
        }
        return true;
    }

    bool spawned(const json& entry, SpawnedPlacement& value) {
        std::uint64_t number = 0;
        if (!entry.is_object() || !text(entry, "key", value.key) || !text(entry, "parent", value.parent)
            || !hex128(entry, "parentActor", value.parentActor) || !text(entry, "parentSlice", value.parentSlice)
            || !text(entry, "spawner", value.spawner) || !unsigned_number(entry, "template", 0xFFFFFFFFULL, number)
            || !flag(entry, "spawnOnInitialize", value.spawnOnInitialize) || !text(entry, "region", value.region)
            || !position(entry, "position", value.position) || !rotation(entry, "rotation", value.rotation)) {
            return fail("a server child is not {key, parent, parentActor, parentSlice, spawner, template, spawnOnInitialize, region, "
                        "position, rotation}");
        }
        value.templateIndex = static_cast<std::uint32_t>(number);
        // Decision D1 of stage 3.7a: the override of a station with a key with no row (an optional member).
        if (entry.contains("override")) {
            const json& change = entry["override"];
            std::uint64_t authored = 0;
            if (!change.is_object() || !text(change, "from", value.overrideFrom) || !text(change, "to", value.overrideTo)
                || !text(change, "slice", value.overrideSlice) || !unsigned_number(entry, "authoredTemplate", 0xFFFFFFFFULL, authored)) {
                return fail("the override of the server child " + value.key + " is not {from, to, slice} with an authoredTemplate");
            }
            value.authoredTemplate = static_cast<std::uint32_t>(authored);
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
    const std::uint64_t format = document.contains("format") && document["format"].is_number_unsigned() ? document["format"].get<std::uint64_t>() : 0;
    if (format != kFormatLevel && format != kFormat && format != kFormatContainers && format != kFormatPersons) {
        m_error = "the file does not have the format 1, 2, 3, or 4";
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
    // Format 2 (stage 2.5b): the templates, the server children, and the fixtures.
    std::vector<InteractionTemplate> templates;
    std::vector<SpawnedPlacement> spawned;
    std::map<std::string, std::uint32_t> fixtures;
    std::map<std::string, LootContainer> containers;
    if (format >= kFormat) {
        for (const char* list : {"templates", "spawned"}) {
            if (!document.contains(list) || !document[list].is_array()) {
                m_error = std::string("the file does not have the list ") + list;
                return false;
            }
        }
        if (!document.contains("fixtures") || !document["fixtures"].is_object()) {
            m_error = "the file does not have the member fixtures";
            return false;
        }
        for (const json& entry : document["templates"]) {
            if (!reader.template_entry(entry, templates.emplace_back())) {
                m_error = "template " + std::to_string(templates.size() - 1) + ": " + reader.error;
                return false;
            }
        }
        for (const json& entry : document["spawned"]) {
            if (!reader.spawned(entry, spawned.emplace_back())) {
                m_error = "server child " + std::to_string(spawned.size() - 1) + ": " + reader.error;
                return false;
            }
        }
        for (const auto& [name, index] : document["fixtures"].items()) {
            if (!index.is_number_unsigned() || index.get<std::uint64_t>() > 0xFFFFFFFFULL) {
                m_error = "the fixture " + name + " is not a template index";
                return false;
            }
            fixtures[name] = index.get<std::uint32_t>();
        }
    }
    // Format 3 (stage 3.5b.2): the loot containers.
    if (format >= kFormatContainers) {
        if (!document.contains("lootContainers") || !document["lootContainers"].is_object()) {
            m_error = "the file does not have the member lootContainers";
            return false;
        }
        for (const auto& [name, entry] : document["lootContainers"].items()) {
            LootContainer value;
            std::uint64_t number = 0;
            if (!entry.is_object() || !reader.unsigned_number(entry, "template", 0xFFFFFFFFULL, number)) {
                m_error = "the loot container " + name + " has no template";
                return false;
            }
            value.templateIndex = static_cast<std::uint32_t>(number);
            if (!reader.unsigned_number(entry, "kind", 0xFFFFFFFFULL, number)) {
                m_error = "the loot container " + name + " has no kind";
                return false;
            }
            value.kind = static_cast<std::uint32_t>(number);
            if (!reader.unsigned_number(entry, "lootDropSlot", 4095, number)) {
                m_error = "the loot container " + name + " has no LootDrop slot";
                return false;
            }
            value.lootDropSlot = static_cast<std::uint32_t>(number);
            if (!reader.unsigned_number(entry, "rarityParticles", 255, number)) {
                m_error = "the loot container " + name + " has no count of rarity particles";
                return false;
            }
            value.rarityParticles = static_cast<std::uint32_t>(number);
            if (!reader.real(entry, "lifetime", value.lifetime) || !reader.real(entry, "emptyLifetime", value.emptyLifetime)
                || !reader.real(entry, "hideDelay", value.hideDelay)) {
                m_error = "the loot container " + name + " has no lifetime, emptyLifetime, or hideDelay";
                return false;
            }
            containers[name] = value;
        }
    }
    // The counts of the tool: a file that lost a part of a list is not complete.
    if (document.contains("counts")) {
        const json& counts = document["counts"];
        std::size_t bounds = 0;
        for (const InteractionObject& object : objects) {
            bounds += object.bounds.size();
        }
        std::vector<std::pair<const char*, std::size_t>> expected = {
            {"objects", objects.size()}, {"bounds", bounds}, {"options", options.size()}, {"achievements", achievements.size()}};
        if (format >= kFormat) {
            expected.insert(expected.end(), {{"templates", templates.size()}, {"spawned", spawned.size()}, {"fixtures", fixtures.size()}});
        }
        if (format >= kFormatContainers) {
            expected.push_back({"lootContainers", containers.size()});
        }
        for (const auto& [key, size] : expected) {
            if (!counts.is_object() || !counts.contains(key) || !counts[key].is_number_unsigned() || counts[key].get<std::uint64_t>() != size) {
                m_error = std::string("the list does not have the count ") + key + " of the file";
                return false;
            }
        }
    }
    if (!set(std::move(options), std::move(achievements), std::move(objects), std::move(templates), std::move(spawned),
             std::move(fixtures))) {
        return false;
    }
    if (!set_loot_containers(std::move(containers))) {
        const std::string cause = m_error;
        *this = InteractionData{};
        m_error = cause;
        return false;
    }
    // Stage QG4: the persons apart (a list of causes for the support manifest; a file of before stage QG4 has none).
    std::vector<PersonApart> apart;
    if (document.contains("personsApart")) {
        if (!document["personsApart"].is_array()) {
            *this = InteractionData{};
            m_error = "the member personsApart is not a list";
            return false;
        }
        for (const json& entry : document["personsApart"]) {
            PersonApart person;
            if (!entry.is_object() || !entry.contains("npc") || !entry["npc"].is_string() || !entry.contains("cause") || !entry["cause"].is_string()
                || !entry.contains("world") || !entry["world"].is_string()) {
                *this = InteractionData{};
                m_error = "a person of personsApart has no npc, cause, or world";
                return false;
            }
            person.npc = entry["npc"].get<std::string>();
            person.cause = entry["cause"].get<std::string>();
            person.world = entry["world"].get<std::string>();
            person.slice = entry.contains("slice") && entry["slice"].is_string() ? entry["slice"].get<std::string>() : std::string{};
            person.placement = entry.contains("placement") && entry["placement"].is_string() ? entry["placement"].get<std::string>() : std::string{};
            apart.push_back(std::move(person));
        }
    }
    set_persons_apart(std::move(apart));
    // Stage QG5c2: the start objects of the encounter recipes (a slice path to the template and the variation; a file of before has none).
    std::map<std::string, EncounterObject, std::less<>> encounterObjects;
    if (document.contains("encounterObjects")) {
        if (!document["encounterObjects"].is_object()) {
            *this = InteractionData{};
            m_error = "the member encounterObjects is not an object";
            return false;
        }
        for (const auto& [slice, entry] : document["encounterObjects"].items()) {
            const auto number = [&entry](const char* name, std::uint64_t limit, std::uint64_t& value) {
                if (!entry.contains(name) || !entry[name].is_number_unsigned() || entry[name].get<std::uint64_t>() > limit) {
                    return false;
                }
                value = entry[name].get<std::uint64_t>();
                return true;
            };
            std::uint64_t index = 0;
            std::uint64_t slot = 0;
            std::uint64_t variant = 0;
            if (!entry.is_object() || !number("template", 0xFFFFFFFFULL, index) || !number("variationSlot", 0xFFFFFFFFULL, slot)
                || !number("variant", 0xFFFFULL, variant)) {
                *this = InteractionData{};
                m_error = "the start object " + slice + " has no template index, no variation slot, or no variant from 0 to 65535";
                return false;
            }
            encounterObjects.emplace(lower_ascii(slice), EncounterObject{static_cast<std::uint32_t>(index), static_cast<std::uint32_t>(slot),
                                                                         static_cast<std::uint16_t>(variant)});
        }
    }
    if (!set_encounter_objects(std::move(encounterObjects))) {
        const std::string cause = m_error;
        *this = InteractionData{};
        m_error = cause;
        return false;
    }
    return true;
}

bool InteractionData::set_encounter_objects(std::map<std::string, EncounterObject, std::less<>> objects) {
    for (const auto& [slice, object] : objects) {
        if (object.templateIndex >= m_templates.size()) {
            m_error = "the start object " + slice + " names a template that the file does not have";
            return false;
        }
        const InteractionTemplate& entry = m_templates[object.templateIndex];
        bool used = object.variationSlot > 4095 || object.variationSlot == entry.positionSlot || object.variationSlot == entry.buildableSlot
                    || object.variationSlot == entry.ownershipSlot || object.variationSlot == entry.vitalsSlot;
        for (const InteractionBound& bound : entry.bounds) {
            used = used || object.variationSlot == bound.slot;
        }
        if (object.variationSlot != 0 && used) {
            m_error = "the variation slot " + std::to_string(object.variationSlot) + " of the start object " + slice
                      + " is above 4095 or is the slot of an other state of its template";
            return false;
        }
    }
    m_encounterObjects = std::move(objects);
    return true;
}

const EncounterObject* InteractionData::encounter_object(std::string_view slice) const {
    const auto found = m_encounterObjects.find(lower_ascii(slice));
    return found != m_encounterObjects.end() ? &found->second : nullptr;
}

void InteractionData::set_persons_apart(std::vector<PersonApart> persons) {
    m_personsApart = std::move(persons);
    m_apartByNpc.clear();
    for (std::size_t at = 0; at < m_personsApart.size(); ++at) {
        if (!m_personsApart[at].npc.empty()) {
            m_apartByNpc[lower_ascii(m_personsApart[at].npc)].push_back(at);
        }
    }
}

std::vector<const PersonApart*> InteractionData::persons_apart(std::string_view npc) const {
    std::vector<const PersonApart*> found;
    if (const auto entry = m_apartByNpc.find(lower_ascii(npc)); entry != m_apartByNpc.end()) {
        for (const std::size_t at : entry->second) {
            found.push_back(&m_personsApart[at]);
        }
    }
    return found;
}

bool InteractionData::set(std::vector<InteractionOption> options,
                          std::vector<InteractionAchievement> achievements,
                          std::vector<InteractionObject> objects,
                          std::vector<InteractionTemplate> templates,
                          std::vector<SpawnedPlacement> spawned,
                          std::map<std::string, std::uint32_t> fixtures) {
    *this = InteractionData{};
    if (objects.empty() && templates.empty()) {
        m_error = "the data has no object";
        return false;
    }
    InteractionData data;
    // One id and one bit for each achievement, in the limit of the achievement state (code review of stage 2.5a, finding INT-R4:
    // two lore pages with one journal bit cannot be told apart; the rule of the progression loader).
    std::map<std::uint32_t, std::string> bits;
    for (std::size_t index = 0; index < achievements.size(); ++index) {
        if (achievements[index].id.empty() || achievements[index].index >= kAchievementBitLimit) {
            m_error = "achievement " + std::to_string(index) + " has no id, or a bit above the limit of the achievement state";
            return false;
        }
        if (!data.m_achievementByLowerId.emplace(lower_ascii(achievements[index].id), static_cast<std::uint32_t>(index)).second) {
            m_error = "two achievements have the id " + achievements[index].id;
            return false;
        }
        const auto [other, added] = bits.emplace(achievements[index].index, achievements[index].id);
        if (!added) {
            m_error = "the achievements " + other->second + " and " + achievements[index].id + " have one bit ("
                      + std::to_string(achievements[index].index) + ")";
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
    // The templates (stage 2.5b): the option and the achievement indexes, the child indexes, and no cycle of children.
    const auto bound_indexes_ok = [&](const InteractionBound& bound) {
        for (const BoundOption& item : bound.options) {
            if (item.option >= options.size()) {
                return false;
            }
        }
        return bound.lore < static_cast<std::int32_t>(achievements.size()) && bound.requiredAchievement < static_cast<std::int32_t>(achievements.size())
               && std::isfinite(bound.position.x) && std::isfinite(bound.position.y) && std::isfinite(bound.position.z) && bound.rangeSq > 0.0F
               && bound.capacity >= -1;
    };
    data.m_templateSpread.assign(templates.size(), 0.0F);
    for (std::uint32_t index = 0; index < templates.size(); ++index) {
        const InteractionTemplate& entry = templates[index];
        if (entry.slice.empty() || entry.positionSlot == 0) {
            m_error = "template " + std::to_string(index) + " has no slice or no PositionInTheWorld slot";
            return false;
        }
        float spread = 0.0F;
        for (const InteractionBound& bound : entry.bounds) {
            if (!bound_indexes_ok(bound) || bound.slot == 0) {
                m_error = "a bound of the template " + entry.slice + " has an index, a place, a range, a capacity, or a slot that is not correct";
                return false;
            }
            spread = std::max(spread, std::hypot(bound.position.x, bound.position.y));
        }
        // One component for each slot of a slice (code review of stage 2.5b, finding ST-R5): the PositionInTheWorld, BuildableController,
        // Ownership, and Vitals slots and the slot of each Interact bound are different (0: the slice has no such component).
        std::vector<std::uint32_t> used;
        for (const std::uint32_t slot : {entry.positionSlot, entry.buildableSlot, entry.ownershipSlot, entry.vitalsSlot}) {
            if (slot != 0) {
                used.push_back(slot);
            }
        }
        for (const InteractionBound& bound : entry.bounds) {
            used.push_back(bound.slot);
        }
        std::sort(used.begin(), used.end());
        if (std::adjacent_find(used.begin(), used.end()) != used.end()) {
            m_error = "two components of the template " + entry.slice + " have the same slot";
            return false;
        }
        for (const TemplateChild& child : entry.children) {
            if (child.templateIndex >= templates.size() || child.templateIndex == index) {
                m_error = "a child of the template " + entry.slice + " names no other template";
                return false;
            }
            spread = std::max(spread, std::hypot(child.position.x, child.position.y));
        }
        data.m_templateSpread[index] = spread;
    }
    // No cycle of children: a template that is its own child (through others) would make an endless chain of entities.
    std::vector<std::uint8_t> mark(templates.size(), 0); // 0: not seen, 1: on the path, 2: done
    std::vector<std::pair<std::uint32_t, std::size_t>> path;
    for (std::uint32_t start = 0; start < templates.size(); ++start) {
        if (mark[start] != 0) {
            continue;
        }
        path.push_back({start, 0});
        mark[start] = 1;
        while (!path.empty()) {
            auto& [node, next] = path.back();
            if (next == templates[node].children.size()) {
                mark[node] = 2;
                path.pop_back();
                continue;
            }
            const std::uint32_t child = templates[node].children[next++].templateIndex;
            if (mark[child] == 1) {
                m_error = "the children of the template " + templates[child].slice + " make a cycle";
                return false;
            }
            if (mark[child] == 0) {
                mark[child] = 1;
                path.push_back({child, 0});
            }
        }
    }
    std::set<std::string> spawnedKeys;
    for (std::uint32_t index = 0; index < spawned.size(); ++index) {
        const SpawnedPlacement& entry = spawned[index];
        if (entry.templateIndex >= templates.size() || !world::valid_position(entry.position) || entry.key.empty()
            || !spawnedKeys.insert(lower_ascii(entry.key)).second
            || (entry.authoredTemplate && *entry.authoredTemplate >= templates.size())) {
            m_error = "server child " + std::to_string(index) + " has no template, a place that is not of the world, or the key of a "
                      "different child";
            return false;
        }
        data.m_spawnedCells[cell_of(entry.position)].push_back(index);
    }
    for (const auto& [name, index] : fixtures) {
        if (index >= templates.size() || templates[index].bounds.empty()) {
            m_error = "the fixture " + name + " names no template with a bound";
            return false;
        }
    }
    data.m_options = std::move(options);
    data.m_achievements = std::move(achievements);
    data.m_objects = std::move(objects);
    data.m_templates = std::move(templates);
    data.m_spawned = std::move(spawned);
    data.m_fixtures = std::move(fixtures);
    *this = std::move(data);
    return true;
}

bool InteractionData::set_loot_containers(std::map<std::string, LootContainer> containers) {
    // Each container names a template with a bound of the kind lootDrop (TakeAllOption, OpenStorageOption), the type AILoot (1), a
    // LootDrop slot, a lifetime above 0, an end time not below 0, and a rarity count (the selector of an entry is below it). A file
    // that does not fit is refused as a whole.
    for (const auto& [name, container] : containers) {
        const bool times = std::isfinite(container.lifetime) && container.lifetime > 0.0F && std::isfinite(container.emptyLifetime)
                           && container.emptyLifetime >= 0.0F && std::isfinite(container.hideDelay) && container.hideDelay >= 0.0F;
        if (container.templateIndex >= m_templates.size() || m_templates[container.templateIndex].bounds.empty()
            || m_templates[container.templateIndex].kind != InteractionKind::lootDrop || m_templates[container.templateIndex].positionSlot == 0
            || container.kind != 1 || container.lootDropSlot == 0 || container.rarityParticles == 0 || !times) {
            m_error = "the loot container " + name + " has no template of the kind lootDrop with a bound and a place, not the type AILoot, "
                      "no LootDrop slot, no rarity particle, or a time that is not correct";
            return false;
        }
        // One component for each slot of the slice (code review of stage 3.5b.2, finding LB-R6): the LootDrop slot is not the slot of the
        // PositionInTheWorld, BuildableController, Ownership, or Vitals component, or of an Interact bound of its template.
        const InteractionTemplate& entry = m_templates[container.templateIndex];
        bool used = container.lootDropSlot == entry.positionSlot || container.lootDropSlot == entry.buildableSlot
                    || container.lootDropSlot == entry.ownershipSlot || container.lootDropSlot == entry.vitalsSlot;
        for (const InteractionBound& bound : entry.bounds) {
            used = used || container.lootDropSlot == bound.slot;
        }
        if (used) {
            m_error = "the LootDrop slot " + std::to_string(container.lootDropSlot) + " of the loot container " + name
                      + " is the slot of a different component of the template " + entry.slice;
            return false;
        }
    }
    m_lootContainers = std::move(containers);
    return true;
}

const LootContainer* InteractionData::loot_container(std::string_view name) const {
    const auto found = m_lootContainers.find(std::string(name));
    return found == m_lootContainers.end() ? nullptr : &found->second;
}

std::optional<std::uint32_t> InteractionData::fixture(std::string_view name) const {
    const auto found = m_fixtures.find(std::string(name));
    return found == m_fixtures.end() ? std::nullopt : std::optional<std::uint32_t>(found->second);
}

float InteractionData::template_spread(std::uint32_t templateIndex) const {
    return templateIndex < m_templateSpread.size() ? m_templateSpread[templateIndex] : 0.0F;
}

state::WorldPosition transform_point(const state::WorldPosition& place, const Rotation& q, const state::WorldPosition& local) {
    // v' = v + 2 w (u x v) + 2 u x (u x v), with u = (x, y, z).
    const float ux = q[0];
    const float uy = q[1];
    const float uz = q[2];
    const float w = q[3];
    const float cx = uy * local.z - uz * local.y;
    const float cy = uz * local.x - ux * local.z;
    const float cz = ux * local.y - uy * local.x;
    const float dx = uy * cz - uz * cy;
    const float dy = uz * cx - ux * cz;
    const float dz = ux * cy - uy * cx;
    return state::WorldPosition{place.x + local.x + 2.0F * (w * cx + dx), place.y + local.y + 2.0F * (w * cy + dy),
                                place.z + local.z + 2.0F * (w * cz + dz)};
}

Rotation compose_rotation(const Rotation& a, const Rotation& b) {
    return Rotation{a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
                    a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
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

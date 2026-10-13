#include "gamedata/creature_data.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>

#include <nlohmann/json.hpp>

#include "server/rep/states/state_codecs.h"

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

/** The form of the file that this code reads (tools/gamedata/creature_data.py). */
constexpr std::uint64_t kFormat = 1;
/** Number of values of one spawn point: placement id, kind, level, x, y, z, yaw, respawnMin, respawnMax. */
constexpr std::size_t kSpawnPointColumns = 9;
/** Limits of a position of the world, in meters (the same as world/position_index.h). */
constexpr float kMaxHorizontal = 1000000.0F;
constexpr float kMaxVertical = 100000.0F;
/** Largest slot of a state record that the backend sends, and the largest level of the level table. */
constexpr std::uint32_t kMaxSlot = 4095;
constexpr std::uint32_t kMaxLevel = 1000;
/** Largest fragment index of an action that the backend reads. */
constexpr std::int64_t kMaxFragmentIndex = 1000000;
/** Names of the action that a creature plays when it dies, in the sequence of the search. */
constexpr const char* kDeathActions[] = {"Death", "Death_FB"};
/** The action that a creature plays when it takes a hit (live proof for the Grey Wolf: protocol notes 4.14). */
constexpr const char* kHitAction = "Reaction";

std::string lower_ascii(std::string_view text) {
    std::string lower(text);
    for (char& letter : lower) {
        if (letter >= 'A' && letter <= 'Z') {
            letter = static_cast<char>(letter + ('a' - 'A'));
        }
    }
    return lower;
}

bool read_number(const json& value, std::uint32_t& number) {
    if (!value.is_number_unsigned() && !(value.is_number_integer() && value.get<std::int64_t>() >= 0)) {
        return false;
    }
    const std::uint64_t wide = value.get<std::uint64_t>();
    if (wide > 0xFFFFFFFFULL) {
        return false;
    }
    number = static_cast<std::uint32_t>(wide);
    return true;
}

bool read_member(const json& parent, const char* key, std::uint32_t& number) {
    return parent.is_object() && parent.contains(key) && read_number(parent[key], number);
}

bool read_real(const json& value, float& number) {
    if (!value.is_number()) {
        return false;
    }
    number = value.get<float>();
    return std::isfinite(number);
}

bool read_text(const json& parent, const char* key, std::string& text) {
    if (!parent.is_object() || !parent.contains(key) || !parent[key].is_string()) {
        return false;
    }
    text = parent[key].get<std::string>();
    return true;
}

/** Reads a 64-bit id from its text of 16 hexadecimal digits: a JSON number cannot hold 64 bits. */
bool read_id(const json& value, std::uint64_t& id) {
    if (!value.is_string()) {
        return false;
    }
    const std::string text = value.get<std::string>();
    if (text.size() != 16 || text.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
        return false;
    }
    id = std::strtoull(text.c_str(), nullptr, 16);
    return true;
}

/** Reads an asset id: 32 hexadecimal digits with a dash between its five parts. */
bool read_asset(const std::string& text, std::array<std::uint8_t, 16>& asset) {
    std::string digits;
    for (const char letter : text) {
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

bool read_slice(const json& entry, CreatureSlice& slice) {
    std::string asset;
    float radius = 0.0F;
    if (!read_text(entry, "path", slice.path) || !read_text(entry, "asset", asset) || !read_asset(asset, slice.asset)
        || !read_member(entry, "sub", slice.subId) || !entry.contains("root") || !read_id(entry["root"], slice.root)
        || !entry.contains("interestRadius") || !read_real(entry["interestRadius"], radius)
        || !read_member(entry, "movement", slice.movementSlot) || !read_member(entry, "vitals", slice.vitalsSlot)
        || !read_member(entry, "marker", slice.markerSlot) || !entry.contains("slots") || !entry["slots"].is_array()
        || !entry.contains("components") || !entry["components"].is_array()) {
        return false;
    }
    slice.interestRadius = radius;
    for (const json& row : entry["slots"]) {
        CreatureSlice::Slot slot;
        if (!row.is_array() || row.size() != 3 || !read_number(row[0], slot.slot) || !row[1].is_string()
            || !read_number(row[2], slot.stateType)) {
            return false;
        }
        slot.component = row[1].get<std::string>();
        slice.slots.push_back(std::move(slot));
    }
    for (const json& row : entry["components"]) {
        CreatureSlice::Component component;
        if (!row.is_array() || row.size() != 3 || !read_id(row[0], component.entity)
            || !read_id(row[1], component.component) || !row[2].is_string()) {
            return false;
        }
        component.name = row[2].get<std::string>();
        slice.components.push_back(std::move(component));
    }
    // The action table: [action name, fragment name, fragment index, server started]. A file
    // of a tool with no action table has no member: the slice then has no action.
    if (entry.contains("actions")) {
        if (!entry["actions"].is_array()) {
            return false;
        }
        for (const json& row : entry["actions"]) {
            CreatureSlice::Action action;
            if (!row.is_array() || row.size() != 4 || !row[0].is_string() || !row[1].is_string()
                || !row[2].is_number_integer() || !row[3].is_boolean()) {
                return false;
            }
            const std::int64_t fragment = row[2].get<std::int64_t>();
            if (fragment < -1 || fragment > kMaxFragmentIndex) {
                return false;
            }
            action.name = row[0].get<std::string>();
            action.fragment = row[1].get<std::string>();
            action.fragmentIndex = static_cast<std::int32_t>(fragment);
            action.serverStarted = row[3].get<bool>();
            slice.actions.push_back(std::move(action));
        }
    }
    return true;
}

bool read_kind(const json& entry, CreatureKind& kind) {
    if (!read_text(entry, "id", kind.id) || !read_text(entry, "vitals", kind.vitals)
        || !read_text(entry, "category", kind.category) || !read_member(entry, "vitalsId", kind.vitalsId)
        || !read_member(entry, "categoryId", kind.categoryId) || !read_text(entry, "name", kind.name)
        || !read_text(entry, "vitalsName", kind.vitalsName) || !read_text(entry, "family", kind.family)
        || !read_text(entry, "creatureType", kind.creatureType) || !entry.contains("health")
        || !read_real(entry["health"], kind.healthFactor) || !read_member(entry, "level", kind.level)
        || !entry.contains("bodies") || !entry["bodies"].is_array()) {
        return false;
    }
    for (const json& row : entry["bodies"]) {
        CreatureBody body;
        if (!row.is_array() || row.size() != 2 || !read_number(row[0], body.slice) || !read_number(row[1], body.percent)) {
            return false;
        }
        kind.bodies.push_back(body);
    }
    if (entry.contains("points") && !read_number(entry["points"], kind.points)) {
        return false;
    }
    return true;
}

bool read_spawn_point(const json& row, SpawnPoint& point) {
    if (!row.is_array() || row.size() != kSpawnPointColumns || !row[0].is_string() || !read_number(row[1], point.kind)
        || !read_number(row[2], point.level) || !read_real(row[3], point.position.x)
        || !read_real(row[4], point.position.y) || !read_real(row[5], point.position.z) || !read_real(row[6], point.yaw)
        || !read_real(row[7], point.respawnMin) || !read_real(row[8], point.respawnMax)) {
        return false;
    }
    point.id = row[0].get<std::string>();
    return true;
}

/**
 * @return True when the slot list of a slice has the slot of a state that the backend sends,
 *         for a component with the type of that state.
 */
bool has_slot(const CreatureSlice& slice, std::uint32_t slot, std::uint32_t stateType) {
    return std::any_of(slice.slots.begin(), slice.slots.end(), [slot, stateType](const CreatureSlice::Slot& entry) {
        return entry.slot == slot && entry.stateType == stateType;
    });
}

} // namespace

std::optional<std::uint32_t> CreatureSlice::find_action(std::string_view name) const {
    for (std::size_t index = 0; index < actions.size(); ++index) {
        if (actions[index].name == name && actions[index].fragmentIndex >= 0) {
            return static_cast<std::uint32_t>(index);
        }
    }
    return std::nullopt;
}

const CreatureSlice::Component* CreatureSlice::find(std::uint64_t group, std::uint64_t target) const {
    const std::uint64_t key = root ^ group;
    for (const Component& component : components) {
        if ((component.entity ^ key) + component.component == target) {
            return &component;
        }
    }
    return nullptr;
}

void CreatureData::clear() {
    m_slices.clear();
    m_kinds.clear();
    m_spawnPoints.clear();
    m_levels.clear();
    m_byId.clear();
    m_searchTexts.clear();
}

bool CreatureData::load(const std::string& path) {
    clear();
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        m_error = "the file is not there";
        return false;
    }
    const json document = json::parse(input, nullptr, false);
    const auto list = [&document](const char* key) {
        return document.is_object() && document.contains(key) && document[key].is_array();
    };
    if (!document.is_object()) {
        m_error = "the file is not a JSON object";
        return false;
    }
    if (!document.contains("format") || !document["format"].is_number_unsigned()
        || document["format"].get<std::uint64_t>() != kFormat) {
        m_error = "the file does not have the format 1";
        return false;
    }
    if (!list("levels") || !list("slices") || !list("kinds") || !list("spawnPoints")) {
        m_error = "the file does not have the lists levels, slices, kinds, and spawnPoints";
        return false;
    }
    std::vector<std::pair<std::uint32_t, float>> levels;
    for (const json& row : document["levels"]) {
        std::pair<std::uint32_t, float> level;
        if (!row.is_array() || row.size() != 2 || !read_number(row[0], level.first) || !read_real(row[1], level.second)) {
            m_error = "a row of the level table is not [level, health]";
            return false;
        }
        levels.push_back(level);
    }
    std::vector<CreatureSlice> slices;
    for (const json& entry : document["slices"]) {
        if (!read_slice(entry, slices.emplace_back())) {
            m_error = "slice " + std::to_string(slices.size() - 1) + " does not have each member";
            return false;
        }
    }
    std::vector<CreatureKind> kinds;
    for (const json& entry : document["kinds"]) {
        if (!read_kind(entry, kinds.emplace_back())) {
            m_error = "kind " + std::to_string(kinds.size() - 1) + " does not have each member";
            return false;
        }
    }
    std::vector<SpawnPoint> points;
    points.reserve(document["spawnPoints"].size());
    for (const json& row : document["spawnPoints"]) {
        if (!read_spawn_point(row, points.emplace_back())) {
            m_error = "spawn point " + std::to_string(points.size() - 1) + " does not have each value";
            return false;
        }
    }
    // The count of the tool: a file that lost a part of a list is not complete. A member
    // "counts" with a wrong form is a fault of the file: it is not the same as no member.
    if (document.contains("counts")) {
        const json& counts = document["counts"];
        std::uint32_t pointCount = 0;
        std::uint32_t kindCount = 0;
        std::uint32_t sliceCount = 0;
        if (!read_member(counts, "spawnPoints", pointCount) || !read_member(counts, "kinds", kindCount)
            || !read_member(counts, "slices", sliceCount) || pointCount != points.size() || kindCount != kinds.size()
            || sliceCount != slices.size()) {
            m_error = "the lists do not have the counts of the file";
            return false;
        }
    }
    return set(std::move(slices), std::move(kinds), std::move(points), std::move(levels));
}

bool CreatureData::set(std::vector<CreatureSlice> slices,
                       std::vector<CreatureKind> kinds,
                       std::vector<SpawnPoint> spawnPoints,
                       std::vector<std::pair<std::uint32_t, float>> levels) {
    clear();
    m_slices = std::move(slices);
    m_kinds = std::move(kinds);
    m_spawnPoints = std::move(spawnPoints);
    for (const auto& [level, health] : levels) {
        m_levels[level] = health;
    }
    if (!check()) {
        clear();
        return false;
    }
    m_error.clear();
    for (CreatureSlice& slice : m_slices) {
        slice.deathAction.reset();
        for (const char* name : kDeathActions) {
            if (!slice.deathAction) {
                slice.deathAction = slice.find_action(name);
            }
        }
        slice.hitAction = slice.find_action(kHitAction);
    }
    build_keys();
    return true;
}

bool CreatureData::check() {
    if (m_kinds.empty() || m_slices.empty() || m_levels.empty()) {
        m_error = "the data has no kind, no slice, or no level";
        return false;
    }
    // The smallest and the largest health of a level, for the health of each kind below.
    float lowestHealth = m_levels.begin()->second;
    float highestHealth = lowestHealth;
    for (const auto& [level, health] : m_levels) {
        if (level == 0 || level > kMaxLevel || !(health > 0.0F) || !std::isfinite(health)) {
            m_error = "the level table has a level or a health that is not correct";
            return false;
        }
        lowestHealth = std::min(lowestHealth, health);
        highestHealth = std::max(highestHealth, health);
    }
    for (std::size_t index = 0; index < m_slices.size(); ++index) {
        const CreatureSlice& slice = m_slices[index];
        // A creature has a position and a health: a slice with no such component is not a creature.
        if (slice.movementSlot == 0 || slice.vitalsSlot == 0 || slice.movementSlot > kMaxSlot || slice.vitalsSlot > kMaxSlot
            || slice.markerSlot > kMaxSlot || slice.movementSlot == slice.vitalsSlot
            || (slice.markerSlot != 0 && (slice.markerSlot == slice.movementSlot || slice.markerSlot == slice.vitalsSlot))
            || !(slice.interestRadius >= 0.0F)) {
            m_error = "slice " + std::to_string(index) + " has no movement slot or no vitals slot, or two equal slots";
            return false;
        }
        // The backend sends a state to each of these slots (server/rep/states): the slot list
        // of the slice must have the slot, for a component with the type of that state.
        if (!has_slot(slice, slice.movementSlot, server::rep::kTypeMovementState)
            || !has_slot(slice, slice.vitalsSlot, server::rep::kTypeVitalsState)
            || (slice.markerSlot != 0 && !has_slot(slice, slice.markerSlot, server::rep::kTypeMarkerState))) {
            m_error = "slice " + std::to_string(index)
                      + " has a movement slot, a vitals slot, or a marker slot that is not the slot of that component in its slot list";
            return false;
        }
        // The metadata state of a creature has the asset id of its slice: the null id names no asset.
        if (std::all_of(slice.asset.begin(), slice.asset.end(), [](std::uint8_t byte) { return byte == 0; })) {
            m_error = "slice " + std::to_string(index) + " has the null asset id";
            return false;
        }
    }
    for (std::size_t index = 0; index < m_kinds.size(); ++index) {
        const CreatureKind& kind = m_kinds[index];
        bool bodies = !kind.bodies.empty();
        std::uint64_t weight = 0;
        for (const CreatureBody& body : kind.bodies) {
            bodies = bodies && body.slice < m_slices.size();
            weight += body.percent;
        }
        if (kind.id.empty() || !bodies || weight == 0 || !(kind.healthFactor > 0.0F) || kind.level == 0
            || kind.level > kMaxLevel) {
            m_error = "kind " + std::to_string(index) + " has no id, no slice, no health, or no level";
            return false;
        }
        if (m_levels.count(kind.level) == 0) {
            m_error = "kind " + std::to_string(index) + " has the level " + std::to_string(kind.level)
                      + ", and the level table has no row for this level";
            return false;
        }
        // A command can give a creature each level of the table: the health of the kind (the
        // health of the level times its factor) must be a finite number above 0 for each one.
        // The product of two floats goes up with each of them: the two ends are sufficient.
        if (!(lowestHealth * kind.healthFactor > 0.0F) || !std::isfinite(highestHealth * kind.healthFactor)) {
            m_error = "kind " + std::to_string(index)
                      + " has a health factor that gives a health of 0 or a health that is not finite for a level of the table";
            return false;
        }
    }
    for (std::size_t index = 0; index < m_spawnPoints.size(); ++index) {
        const SpawnPoint& point = m_spawnPoints[index];
        if (point.id.empty() || point.kind >= m_kinds.size() || point.level == 0 || point.level > kMaxLevel
            || std::abs(point.position.x) > kMaxHorizontal || std::abs(point.position.y) > kMaxHorizontal
            || std::abs(point.position.z) > kMaxVertical) {
            m_error = "spawn point " + std::to_string(index) + " has no id, no kind, no level, or no position of the world";
            return false;
        }
        if (m_levels.count(point.level) == 0) {
            m_error = "spawn point " + std::to_string(index) + " has the level " + std::to_string(point.level)
                      + ", and the level table has no row for this level";
            return false;
        }
    }
    return true;
}

void CreatureData::build_keys() {
    m_byId.reserve(m_kinds.size());
    m_searchTexts.reserve(m_kinds.size());
    for (std::size_t position = 0; position < m_kinds.size(); ++position) {
        const CreatureKind& kind = m_kinds[position];
        m_byId.emplace(lower_ascii(kind.id), position);
        m_searchTexts.push_back(lower_ascii(kind.id) + "\n" + lower_ascii(kind.name) + "\n" + lower_ascii(kind.vitalsName)
                                + "\n" + lower_ascii(kind.category) + "\n" + lower_ascii(kind.family));
    }
}

const CreatureKind* CreatureData::find_kind(std::string_view id) const {
    const auto found = m_byId.find(lower_ascii(id));
    return found == m_byId.end() ? nullptr : &m_kinds[found->second];
}

float CreatureData::health(const CreatureKind& kind, std::uint32_t level) const {
    const auto found = m_levels.find(level);
    return found == m_levels.end() ? 0.0F : found->second * kind.healthFactor;
}

std::vector<const CreatureKind*> CreatureData::search(std::string_view text,
                                                      std::size_t offset,
                                                      std::size_t limit,
                                                      std::size_t* total) const {
    std::vector<std::string> words;
    const std::string lower = lower_ascii(text);
    std::size_t start = 0;
    while (start < lower.size()) {
        const std::size_t end = std::min(lower.find(' ', start), lower.size());
        if (end > start) {
            words.push_back(lower.substr(start, end - start));
        }
        start = end + 1;
    }
    std::vector<const CreatureKind*> found;
    std::size_t count = 0;
    for (std::size_t position = 0; position < m_kinds.size(); ++position) {
        const std::string& candidate = m_searchTexts[position];
        const bool match = std::all_of(words.begin(), words.end(), [&candidate](const std::string& word) {
            return candidate.find(word) != std::string::npos;
        });
        if (!match) {
            continue;
        }
        if (count >= offset && found.size() < limit) {
            found.push_back(&m_kinds[position]);
        }
        ++count;
    }
    if (total != nullptr) {
        *total = count;
    }
    return found;
}

} // namespace soulwarden::backend::gamedata

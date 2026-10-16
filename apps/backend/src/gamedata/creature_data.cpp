#include "gamedata/creature_data.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>

#include <nlohmann/json.hpp>

#include "server/rep/states/state_codecs.h"

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

/**
 * The forms of the file that this code reads (tools/gamedata/creature_data.py). Format 2 (stage
 * 3.4b) has the data for the behavior of a creature: a level row has the base damage and the
 * gear score too, a spawn point has the terrain height, a kind can have a damage factor, and a
 * slice can have behavior values, attacks, and tags.
 */
constexpr std::uint64_t kFirstFormat = 1;
constexpr std::uint64_t kBehaviorFormat = 2;
/** Format 3 (stage 3.4e): the patrol route of a spawn point, and the wander values and the ambient speed of a slice. */
constexpr std::uint64_t kPatrolFormat = 3;
/**
 * Number of values of one spawn point: placement id, kind, level, x, y, z, yaw, respawnMin,
 * respawnMax; format 2: and the terrain height; format 3: and the index of its route (null: none).
 */
constexpr std::size_t kSpawnPointColumns = 9;
constexpr std::size_t kSpawnPointColumnsWithGround = 10;
constexpr std::size_t kSpawnPointColumnsWithRoute = 11;
/** The longest task name of a waypoint. */
constexpr std::size_t kLongestTask = 128;
/** Limits of the behavior values: a distance and a reach in meters, a speed in meters in one second, a time in seconds. */
constexpr float kMaxSenseDistance = 1000.0F;
constexpr float kMaxSpeed = 100.0F;
constexpr float kMaxAttackTime = 60.0F;
constexpr float kMaxReach = 100.0F;
constexpr float kMaxDamageFactor = 1000000.0F;
/**
 * The largest damage and health of a level, and the largest health factor of a kind. With
 * these limits each product of the damage rule (the damage of the level, the factor of the
 * kind, the factor of a contact, and the factors of the combat tables) is a finite number:
 * 1e9 * 1e6 * 1e6 is 1e21, far below the largest number of 32 bits (3.4e38).
 */
constexpr float kMaxLevelValue = 1.0e9F;
/** The largest speed of a projectile and of the aim of a ranged attack (m/s), and the largest place of a damage row (stage 3.4f-A). */
constexpr float kMaxProjectileSpeed = 10000.0F;
constexpr std::uint32_t kMaxRowIndex = 65534;
/** The shortest attack: one millisecond, the step of the world clock of an attack. */
constexpr float kMinAttackTime = 0.001F;
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
/** The locomotion action of a slice (creature_data.h, CreatureSlice::moveAction). */
constexpr const char* kMoveAction = "Move_Combat";
/** The locomotion of a walk with no target (CreatureSlice::walkAction). */
constexpr const char* kWalkAction = "Move";
/** The action of a stunned creature (CreatureSlice::stunAction, live check L6 of stage 3.3a). */
constexpr const char* kStunAction = "Stun";
/** The state type of the status effect component (MB::StatusEffectsComponentReplicatedState). */
constexpr std::uint32_t kStatusEffectsType = 4236;

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

/** Reads a capsule [radius, height, [axis], [offset]] (a slice of a creature, the player slice of stage 3.4f-A). */
bool read_capsule(const json& values, CreatureCapsule& capsule) {
    const auto vector = [](const json& value, std::array<float, 3>& out) {
        return value.is_array() && value.size() == 3 && read_real(value[0], out[0]) && read_real(value[1], out[1])
               && read_real(value[2], out[2]);
    };
    return values.is_array() && values.size() == 4 && read_real(values[0], capsule.radius) && read_real(values[1], capsule.height)
           && vector(values[2], capsule.axis) && vector(values[3], capsule.offset) && capsule.radius > 0.0F && capsule.height >= 0.0F;
}

/** Reads the damage category of a row ("Physical", "Elemental", "True"). */
bool read_category(const std::string& text, DamageCategory& category) {
    if (text == "Physical") {
        category = DamageCategory::physical;
    } else if (text == "Elemental") {
        category = DamageCategory::elemental;
    } else if (text == "True") {
        category = DamageCategory::direct;
    } else {
        return false;
    }
    return true;
}

/**
 * Reads one release of a ranged attack (stage 3.4f-A, creature_behavior.py): an object with the joins of the release. A member that
 * is not there or that has a wrong form is a fault of the file.
 */
bool read_release(const json& cell, RangedRelease& release) {
    std::uint32_t wire = 0;
    std::string category;
    if (!cell.is_object() || !cell.contains("time") || !read_real(cell["time"], release.time) || !read_text(cell, "profile", release.profile)
        || !read_text(cell, "attack", release.attack) || !read_member(cell, "index", release.attackIndex)
        || !read_text(cell, "ammo", release.ammo) || !read_member(cell, "ammoKey", release.ammoKey)
        || !read_member(cell, "projectile", release.projectile) || !cell.contains("override") || !cell["override"].is_boolean()
        || !read_text(cell, "table", release.table) || !read_member(cell, "tableKey", release.tableKey)
        || !read_text(cell, "row", release.damage.row) || !read_member(cell, "rowIndex", release.rowIndex)
        || !cell.contains("factor") || !read_real(cell["factor"], release.damage.factor) || !cell.contains("added")
        || !read_real(cell["added"], release.damage.added) || !read_text(cell, "type", release.damage.type)
        || !read_member(cell, "wire", wire) || wire == 0 || wire > 255 || !read_text(cell, "category", category)
        || !read_category(category, release.damage.category) || !cell.contains("charge")
        || !read_real(cell["charge"], release.damage.hitCharge) || !cell.contains("lead") || !cell["lead"].is_boolean()
        || !cell.contains("aimSpeed") || !read_real(cell["aimSpeed"], release.aimSpeed) || !cell.contains("leadAngle")
        || !read_real(cell["leadAngle"], release.leadAngle) || !cell.contains("aimAngle") || !read_real(cell["aimAngle"], release.aimAngle)
        || !cell.contains("miss") || !cell["miss"].is_array() || cell["miss"].size() != 2 || !read_real(cell["miss"][0], release.missMin)
        || !read_real(cell["miss"][1], release.missMax) || !cell.contains("range") || !read_real(cell["range"], release.range)
        || !cell.contains("radius") || !read_real(cell["radius"], release.radius) || !cell.contains("accelDelay")
        || !read_real(cell["accelDelay"], release.accelDelay)) {
        return false;
    }
    release.sliceOverride = cell["override"].get<bool>();
    release.lead = cell["lead"].get<bool>();
    release.damage.wire = static_cast<std::uint8_t>(wire);
    release.damage.time = release.time;
    return true;
}

/** Reads one projectile slice [path, asset, sub, root, slot, speed, extra, time for max, [accel], flying, stuck] (stage 3.4f-A). */
bool read_projectile(const json& row, ProjectileSliceData& projectile) {
    std::string asset;
    if (!row.is_array() || row.size() != 11 || !row[0].is_string() || !row[1].is_string() || !read_number(row[2], projectile.subId)
        || !read_id(row[3], projectile.root) || !read_number(row[4], projectile.slot) || !read_real(row[5], projectile.speed)
        || !read_real(row[6], projectile.extraSpeed) || !read_real(row[7], projectile.timeForMaxSpeed) || !row[8].is_array()
        || row[8].size() != 3 || !read_real(row[8][0], projectile.accel[0]) || !read_real(row[8][1], projectile.accel[1])
        || !read_real(row[8][2], projectile.accel[2]) || !read_real(row[9], projectile.flyingLifetime)
        || !read_real(row[10], projectile.stuckLifetime)) {
        return false;
    }
    projectile.path = row[0].get<std::string>();
    asset = row[1].get<std::string>();
    return read_asset(asset, projectile.asset);
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
            // The reader keeps a whole number above the range of a signed 64-bit value as an
            // unsigned value, and its signed value is a different number (18446744073709551615
            // is -1, the value for "no fragment"). Thus an unsigned value has its own compare,
            // before the conversion.
            if (row[2].is_number_unsigned()
                && row[2].get<std::uint64_t>() > static_cast<std::uint64_t>(kMaxFragmentIndex)) {
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
    // The reaction fragments of each power level (stage 3.3a): a member of a file of a newer tool.
    if (entry.contains("reactions")) {
        const json& reactions = entry["reactions"];
        if (!reactions.is_array() || reactions.size() != slice.reactions.size()) {
            return false;
        }
        for (std::size_t power = 0; power < slice.reactions.size(); ++power) {
            if (!reactions[power].is_number_integer() || reactions[power].get<std::int64_t>() < -1
                || reactions[power].get<std::int64_t>() > kMaxFragmentIndex) {
                return false;
            }
            slice.reactions[power] = static_cast<std::int32_t>(reactions[power].get<std::int64_t>());
        }
    }
    // The spawn action: a member of a slice that needs it. A member with a wrong form is a fault.
    if (entry.contains("spawnAction") && (!entry["spawnAction"].is_string() || entry["spawnAction"].get<std::string>().empty())) {
        return false;
    }
    if (entry.contains("spawnAction")) {
        slice.spawnActionName = entry["spawnAction"].get<std::string>();
    }
    // The capsule of the character (stage 3.3b-S3): a member of a slice that has one. A member with a wrong form is a fault.
    if (entry.contains("capsule")) {
        CreatureCapsule capsule;
        if (!read_capsule(entry["capsule"], capsule)) {
            return false;
        }
        slice.capsule = capsule;
    }
    // The hit facts (stage 3.3b-S5): the two angles of a hit from behind (0 to pi), and the head volume.
    if (entry.contains("behind")) {
        const json& values = entry["behind"];
        CreatureBehind behind;
        constexpr float kPi = 3.14159274F;
        if (!values.is_array() || values.size() != 2 || !read_real(values[0], behind.hit) || !read_real(values[1], behind.critical)
            || behind.hit < 0.0F || behind.hit > kPi + 0.001F || behind.critical < 0.0F || behind.critical > kPi + 0.001F) {
            return false;
        }
        slice.behind = behind;
    }
    if (entry.contains("head")) {
        const json& values = entry["head"];
        CreatureHead head;
        const auto point = [](const json& value, std::array<float, 3>& out) {
            return value.is_array() && value.size() == 3 && read_real(value[0], out[0]) && read_real(value[1], out[1])
                   && read_real(value[2], out[2]);
        };
        if (!values.is_array() || values.size() != 3 || !point(values[0], head.a) || !point(values[1], head.b)
            || !read_real(values[2], head.radius) || !(head.radius > 0.0F)) {
            return false;
        }
        slice.head = head;
    }
    // The members of the behavior (format 2). Each one is a member of a slice that has it: a
    // member with a wrong form is a fault of the file.
    if (entry.contains("tags")) {
        const std::string digits = entry["tags"].is_string() ? entry["tags"].get<std::string>() : std::string();
        if (digits.size() != 2 * kFragmentTagBytes || digits.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
            return false;
        }
        for (std::size_t index = 0; index < slice.tags.size(); ++index) {
            slice.tags[index] = static_cast<std::uint8_t>(std::strtoul(digits.substr(index * 2, 2).c_str(), nullptr, 16));
        }
    }
    if (entry.contains("behavior")) {
        const json& values = entry["behavior"];
        const auto pair = [&values](const char* key, float& first, float& second) {
            return values.is_object() && values.contains(key) && values[key].is_array() && values[key].size() == 2
                   && read_real(values[key][0], first) && read_real(values[key][1], second);
        };
        CreatureBehavior behavior;
        if (!pair("sight", behavior.sightAngle, behavior.sightDistance)
            || !pair("combatSight", behavior.combatAngle, behavior.combatDistance)
            || !pair("leash", behavior.restLeash, behavior.combatLeash)
            || !pair("speed", behavior.walkSpeed, behavior.runSpeed)) {
            return false;
        }
        // Format 3: the ambient speed, and the wander [on, nearest, farthest]. A member with a
        // wrong form is a fault of the file; a slice with no member has no wander.
        if (values.contains("ambient") && !read_real(values["ambient"], behavior.ambientSpeed)) {
            return false;
        }
        if (values.contains("wander")) {
            const json& wander = values["wander"];
            std::uint32_t on = 0;
            if (!wander.is_array() || wander.size() != 3 || !read_number(wander[0], on) || on > 1
                || !read_real(wander[1], behavior.wanderNearest) || !read_real(wander[2], behavior.wanderFarthest)) {
                return false;
            }
            behavior.wander = on == 1;
        }
        slice.behavior = behavior;
    }
    if (entry.contains("attacks")) {
        if (!entry["attacks"].is_array()) {
            return false;
        }
        for (const json& row : entry["attacks"]) {
            // [action, time, [[contact time, reach, factor, added, type, type number, category, row, hit rune charge], ...]]
            // A file of a tool from before stage 1.3b has no rune charge (8 values): a hit then gives no charge.
            CreatureAttack attack;
            if (!row.is_array() || row.size() != 3 || !read_number(row[0], attack.action) || !read_real(row[1], attack.length)
                || !row[2].is_array()) {
                return false;
            }
            for (const json& cell : row[2]) {
                CreatureContact contact;
                std::uint32_t wire = 0;
                if (!cell.is_array() || (cell.size() != 8 && cell.size() != 9) || !read_real(cell[0], contact.time)
                    || !read_real(cell[1], contact.reach) || !read_real(cell[2], contact.factor) || !read_real(cell[3], contact.added)
                    || !cell[4].is_string() || !read_number(cell[5], wire) || wire == 0 || wire > 255 || !cell[6].is_string()
                    || !cell[7].is_string() || (cell.size() == 9 && (!read_real(cell[8], contact.hitCharge) || contact.hitCharge < 0.0F))) {
                    return false;
                }
                if (!read_category(cell[6].get<std::string>(), contact.category)) {
                    return false;
                }
                contact.type = cell[4].get<std::string>();
                contact.wire = static_cast<std::uint8_t>(wire);
                contact.row = cell[7].get<std::string>();
                attack.contacts.push_back(std::move(contact));
            }
            slice.attacks.push_back(std::move(attack));
        }
    }
    // The ranged attacks (stage 3.4f-A): [action, time, [release, ...]]. A file of an older tool has no member.
    if (entry.contains("ranged")) {
        if (!entry["ranged"].is_array()) {
            return false;
        }
        for (const json& row : entry["ranged"]) {
            CreatureRangedAttack attack;
            if (!row.is_array() || row.size() != 3 || !read_number(row[0], attack.action) || !read_real(row[1], attack.length)
                || !row[2].is_array()) {
                return false;
            }
            for (const json& cell : row[2]) {
                if (!read_release(cell, attack.releases.emplace_back())) {
                    return false;
                }
            }
            slice.ranged.push_back(std::move(attack));
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
    // The damage of the kind as a part of the base damage of its level (format 2).
    if (entry.contains("damage") && !read_real(entry["damage"], kind.damageFactor)) {
        return false;
    }
    return true;
}

/** Reads one route: [policy, [[x, y, z, yaw, min wait, max wait, task, align], ...]]. */
bool read_route(const json& row, PatrolRoute& route) {
    std::uint32_t policy = 0;
    if (!row.is_array() || row.size() != 2 || !read_number(row[0], policy) || policy > 3 || !row[1].is_array()) {
        return false;
    }
    route.policy = static_cast<PatrolPolicy>(policy);
    for (const json& cell : row[1]) {
        PatrolWaypoint point;
        std::uint32_t align = 0;
        if (!cell.is_array() || cell.size() != 8 || !read_real(cell[0], point.position.x) || !read_real(cell[1], point.position.y)
            || !read_real(cell[2], point.position.z) || !read_real(cell[3], point.yaw) || !read_real(cell[4], point.minWait)
            || !read_real(cell[5], point.maxWait) || !cell[6].is_string() || !read_number(cell[7], align) || align > 1) {
            return false;
        }
        point.task = cell[6].get<std::string>();
        point.align = align == 1;
        route.points.push_back(std::move(point));
    }
    return true;
}

bool read_spawn_point(const json& row, std::size_t columns, SpawnPoint& point) {
    if (!row.is_array() || row.size() != columns || !row[0].is_string() || !read_number(row[1], point.kind)
        || !read_number(row[2], point.level) || !read_real(row[3], point.position.x)
        || !read_real(row[4], point.position.y) || !read_real(row[5], point.position.z) || !read_real(row[6], point.yaw)
        || !read_real(row[7], point.respawnMin) || !read_real(row[8], point.respawnMax)) {
        return false;
    }
    point.id = row[0].get<std::string>();
    if (columns == kSpawnPointColumnsWithRoute && !row[10].is_null()) {
        std::uint32_t route = 0;
        if (!read_number(row[10], route)) {
            return false;
        }
        point.route = route;
    }
    if (columns >= kSpawnPointColumnsWithGround && !row[9].is_null()) {
        // The terrain height at the point. Null: the world has no height map there.
        float ground = 0.0F;
        if (!read_real(row[9], ground)) {
            return false;
        }
        point.ground = ground;
    }
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
    m_routes.clear();
    m_projectiles.clear();
    m_playerCapsule.reset();
    m_shootingSlices = 0;
    m_routedPoints = 0;
    m_wanderingKinds = 0;
    m_levels.clear();
    m_fightingSlices = 0;
    m_fightingKinds = 0;
    m_fightingPoints = 0;
    m_rootedPoints = 0;
    m_largestSight = 0.0F;
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
    const std::uint64_t format = document.contains("format") && document["format"].is_number_unsigned()
                                     ? document["format"].get<std::uint64_t>()
                                     : 0;
    if (format != kFirstFormat && format != kBehaviorFormat && format != kPatrolFormat) {
        m_error = "the file does not have the format 1, 2, or 3";
        return false;
    }
    const bool withBehavior = format >= kBehaviorFormat;
    const bool withRoutes = format >= kPatrolFormat;
    if (!list("levels") || !list("slices") || !list("kinds") || !list("spawnPoints") || (withRoutes && !list("routes"))) {
        m_error = withRoutes ? "the file does not have the lists levels, slices, kinds, spawnPoints, and routes"
                             : "the file does not have the lists levels, slices, kinds, and spawnPoints";
        return false;
    }
    std::vector<CreatureLevel> levels;
    for (const json& row : document["levels"]) {
        CreatureLevel level;
        if (!row.is_array() || row.size() != (withBehavior ? 4U : 2U) || !read_number(row[0], level.level)
            || !read_real(row[1], level.health)
            || (withBehavior && (!read_real(row[2], level.damage) || !read_real(row[3], level.gearScore)))) {
            m_error = withBehavior ? "a row of the level table is not [level, health, damage, gear score]"
                                   : "a row of the level table is not [level, health]";
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
    const std::size_t columns = withRoutes ? kSpawnPointColumnsWithRoute
                                : withBehavior ? kSpawnPointColumnsWithGround
                                               : kSpawnPointColumns;
    for (const json& row : document["spawnPoints"]) {
        if (!read_spawn_point(row, columns, points.emplace_back())) {
            m_error = "spawn point " + std::to_string(points.size() - 1) + " does not have each value";
            return false;
        }
    }
    std::vector<PatrolRoute> routes;
    if (withRoutes) {
        routes.reserve(document["routes"].size());
        for (const json& row : document["routes"]) {
            if (!read_route(row, routes.emplace_back())) {
                m_error = "route " + std::to_string(routes.size() - 1) + " does not have each value";
                return false;
            }
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
    // The projectile slices of the ranged attacks and the capsule of the character of a player (stage 3.4f-A): members of a file
    // of a newer tool. A member with a wrong form is a fault of the file.
    std::vector<ProjectileSliceData> projectiles;
    if (document.contains("projectiles")) {
        if (!list("projectiles")) {
            m_error = "the member projectiles is not a list";
            return false;
        }
        for (const json& row : document["projectiles"]) {
            if (!read_projectile(row, projectiles.emplace_back())) {
                m_error = "projectile slice " + std::to_string(projectiles.size() - 1) + " does not have each value";
                return false;
            }
        }
    }
    std::optional<CreatureCapsule> playerCapsule;
    if (document.contains("playerCapsule")) {
        CreatureCapsule capsule;
        if (!read_capsule(document["playerCapsule"], capsule)) {
            m_error = "the member playerCapsule is not [radius, height, [axis], [offset]]";
            return false;
        }
        playerCapsule = capsule;
    }
    return set_full(std::move(slices), std::move(kinds), std::move(points), std::move(levels), std::move(routes), std::move(projectiles),
                    playerCapsule);
}

bool CreatureData::set(std::vector<CreatureSlice> slices,
                       std::vector<CreatureKind> kinds,
                       std::vector<SpawnPoint> spawnPoints,
                       std::vector<std::pair<std::uint32_t, float>> levels) {
    std::vector<CreatureLevel> rows;
    for (const auto& [level, health] : levels) {
        rows.push_back(CreatureLevel{level, health, 0.0F, 0.0F});
    }
    return set_full(std::move(slices), std::move(kinds), std::move(spawnPoints), std::move(rows));
}

bool CreatureData::set_full(std::vector<CreatureSlice> slices,
                            std::vector<CreatureKind> kinds,
                            std::vector<SpawnPoint> spawnPoints,
                            std::vector<CreatureLevel> levels,
                            std::vector<PatrolRoute> routes,
                            std::vector<ProjectileSliceData> projectiles,
                            std::optional<CreatureCapsule> playerCapsule) {
    clear();
    m_slices = std::move(slices);
    m_kinds = std::move(kinds);
    m_spawnPoints = std::move(spawnPoints);
    m_routes = std::move(routes);
    m_projectiles = std::move(projectiles);
    m_playerCapsule = playerCapsule;
    for (const CreatureLevel& level : levels) {
        m_levels[level.level] = level;
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
        slice.moveAction = slice.find_action(kMoveAction);
        slice.walkAction = slice.find_action(kWalkAction);
        slice.stunAction = slice.find_action(kStunAction);
        slice.statusSlot = 0;
        for (const CreatureSlice::Slot& slot : slice.slots) {
            if (slot.stateType == kStatusEffectsType && slice.statusSlot == 0) {
                slice.statusSlot = slot.slot;
            }
        }
        // The hits of each attack, and its heavy attacks (project rules, creature_data.h).
        slice.lightAttacks.clear();
        slice.heavyAttacks.clear();
        float lightest = std::numeric_limits<float>::max();
        for (CreatureAttack& attack : slice.attacks) {
            std::stable_sort(attack.contacts.begin(), attack.contacts.end(),
                             [](const CreatureContact& a, const CreatureContact& b) { return a.time < b.time; });
            attack.weight = 0.0F;
            attack.hits = 0;
            float last = -std::numeric_limits<float>::max();
            for (CreatureContact& contact : attack.contacts) {
                contact.repeat = contact.time - last <= kContactGap;
                if (!contact.repeat) {
                    attack.weight += contact.factor;
                    ++attack.hits;
                    last = contact.time;
                }
            }
            lightest = std::min(lightest, attack.weight);
        }
        for (std::uint32_t index = 0; index < slice.attacks.size(); ++index) {
            CreatureAttack& attack = slice.attacks[index];
            attack.heavy = attack.weight > kHeavyWeight && attack.weight > lightest * kHeavyRatio;
            (attack.heavy ? slice.heavyAttacks : slice.lightAttacks).push_back(index);
        }
        // The ranged attacks (stage 3.4f-A): the releases in the sequence of their times, and the basic shot (kBasicShotAction).
        slice.basicShot.reset();
        for (std::uint32_t index = 0; index < slice.ranged.size(); ++index) {
            CreatureRangedAttack& attack = slice.ranged[index];
            std::stable_sort(attack.releases.begin(), attack.releases.end(),
                             [](const RangedRelease& a, const RangedRelease& b) { return a.time < b.time; });
            if (!slice.basicShot && slice.actions[attack.action].name == kBasicShotAction) {
                slice.basicShot = index;
            }
        }
        m_shootingSlices += slice.basicShot ? 1 : 0;
        slice.spawnAction = slice.spawnActionName.empty() ? std::nullopt : slice.find_action(slice.spawnActionName);
        if (slice.fights()) {
            ++m_fightingSlices;
            m_largestSight = std::max(m_largestSight, slice.behavior->sightDistance);
        }
    }
    // The kinds and the spawn points whose creature fights, and the points whose creature does
    // not leave its place: the numbers for the log line of the backend.
    std::vector<bool> fights(m_kinds.size(), false);
    for (std::size_t index = 0; index < m_kinds.size(); ++index) {
        bool wanders = false;
        for (const CreatureBody& body : m_kinds[index].bodies) {
            const CreatureSlice& slice = m_slices[body.slice];
            fights[index] = fights[index] || slice.fights();
            wanders = wanders || (slice.behavior && slice.behavior->wander && slice.behavior->wanderFarthest > 0.0F);
        }
        m_fightingKinds += fights[index] ? 1 : 0;
        m_wanderingKinds += wanders ? 1 : 0;
    }
    for (const SpawnPoint& point : m_spawnPoints) {
        if (fights[point.kind]) {
            ++m_fightingPoints;
            m_rootedPoints += on_terrain(point) ? 0 : 1;
        }
        m_routedPoints += point.route ? 1 : 0;
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
    float lowestHealth = m_levels.begin()->second.health;
    float highestHealth = lowestHealth;
    for (const auto& [level, row] : m_levels) {
        const float health = row.health;
        if (level == 0 || level > kMaxLevel || !(health > 0.0F) || !std::isfinite(health) || health > kMaxLevelValue) {
            m_error = "the level table has a level or a health that is not correct";
            return false;
        }
        // A damage above the limit gives a product that is not finite, and the damage rule
        // would make 0 of it: the file is not correct.
        if (!(row.damage >= 0.0F) || !std::isfinite(row.damage) || row.damage > kMaxLevelValue || !(row.gearScore >= 0.0F)
            || !std::isfinite(row.gearScore) || row.gearScore > kMaxLevelValue) {
            m_error = "the level table has a damage or a gear score that is not correct";
            return false;
        }
        lowestHealth = std::min(lowestHealth, health);
        highestHealth = std::max(highestHealth, health);
    }
    // The projectile slices (stage 3.4f-A): an asset, a slot of the Projectile component, a speed, and a life time of their ranges.
    for (std::size_t index = 0; index < m_projectiles.size(); ++index) {
        const ProjectileSliceData& projectile = m_projectiles[index];
        const auto finite = [](float value, float limit) { return std::isfinite(value) && std::abs(value) <= limit; };
        if (std::all_of(projectile.asset.begin(), projectile.asset.end(), [](std::uint8_t byte) { return byte == 0; })
            || projectile.slot == 0 || projectile.slot > kMaxSlot || !(projectile.speed > 0.0F) || !finite(projectile.speed, kMaxProjectileSpeed)
            || !finite(projectile.extraSpeed, kMaxProjectileSpeed) || !finite(projectile.timeForMaxSpeed, kMaxAttackTime)
            || !finite(projectile.accel[0], kMaxProjectileSpeed) || !finite(projectile.accel[1], kMaxProjectileSpeed)
            || !finite(projectile.accel[2], kMaxProjectileSpeed) || !(projectile.flyingLifetime > 0.0F)
            || !finite(projectile.flyingLifetime, kMaxAttackTime) || !(projectile.stuckLifetime >= 0.0F)
            || !finite(projectile.stuckLifetime, kMaxAttackTime)) {
            m_error = "projectile slice " + std::to_string(index) + " has no asset, no slot, or a flight value that is not of its range";
            return false;
        }
    }
    if (m_playerCapsule && (!(m_playerCapsule->radius <= kMaxReach) || !(m_playerCapsule->height <= kMaxReach))) {
        m_error = "the capsule of the character of a player is larger than the limit";
        return false;
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
        // The backend starts the spawn action of a slice that names one: the action table must
        // have it, with a fragment. Without it the creature of such a slice shows no body.
        if (!slice.spawnActionName.empty() && !slice.find_action(slice.spawnActionName)) {
            m_error = "slice " + std::to_string(index) + " names the spawn action " + slice.spawnActionName
                      + ", and its action table has no such action with a fragment";
            return false;
        }
        // The behavior values: each one is a number of its range. A slice with a value outside it
        // would give a creature that sees the whole world or that is at a different place in one step.
        if (slice.behavior) {
            const CreatureBehavior& values = *slice.behavior;
            const auto in_range = [](float value, float limit) { return std::isfinite(value) && value >= 0.0F && value <= limit; };
            if (!in_range(values.sightAngle, 360.0F) || !in_range(values.combatAngle, 360.0F)
                || !in_range(values.sightDistance, kMaxSenseDistance) || !in_range(values.combatDistance, kMaxSenseDistance)
                || !in_range(values.restLeash, kMaxSenseDistance) || !in_range(values.combatLeash, kMaxSenseDistance)
                || !in_range(values.walkSpeed, kMaxSpeed) || !in_range(values.runSpeed, kMaxSpeed)
                || !in_range(values.ambientSpeed, kMaxSpeed) || !in_range(values.wanderNearest, kMaxSenseDistance)
                || !in_range(values.wanderFarthest, kMaxSenseDistance) || values.wanderNearest > values.wanderFarthest) {
                m_error = "slice " + std::to_string(index) + " has a behavior value that is not a number of its range";
                return false;
            }
        }
        // The attacks: the backend starts the action of an attack, and it tests each contact at
        // its time. The action table must have the action with a fragment, and each contact
        // must be in the time of its attack.
        for (const CreatureAttack& attack : slice.attacks) {
            bool correct = attack.action < slice.actions.size() && slice.actions[attack.action].fragmentIndex >= 0
                           && std::isfinite(attack.length) && attack.length >= kMinAttackTime && attack.length <= kMaxAttackTime
                           && !attack.contacts.empty();
            for (const CreatureContact& contact : attack.contacts) {
                correct = correct && std::isfinite(contact.time) && contact.time >= 0.0F && contact.time <= attack.length
                          && std::isfinite(contact.reach) && contact.reach > 0.0F && contact.reach <= kMaxReach
                          && std::isfinite(contact.factor) && contact.factor >= 0.0F && contact.factor <= kMaxDamageFactor
                          && std::isfinite(contact.added) && contact.added >= 0.0F && contact.added <= kMaxDamageFactor
                          && contact.wire != 0 && !contact.type.empty();
            }
            if (!correct) {
                m_error = "slice " + std::to_string(index)
                          + " has an attack whose action, time, or contacts do not fit together";
                return false;
            }
        }
        // The ranged attacks (stage 3.4f-A): the action of the attack has a fragment, each release is in the time of its attack,
        // names a projectile slice of the file, and has values of their ranges. A release that does not fit is a fault of the file.
        for (const CreatureRangedAttack& attack : slice.ranged) {
            bool correct = attack.action < slice.actions.size() && slice.actions[attack.action].fragmentIndex >= 0
                           && std::isfinite(attack.length) && attack.length >= kMinAttackTime && attack.length <= kMaxAttackTime
                           && !attack.releases.empty();
            for (const RangedRelease& release : attack.releases) {
                const auto in_range = [](float value, float limit) { return std::isfinite(value) && value >= 0.0F && value <= limit; };
                correct = correct && in_range(release.time, attack.length) && release.projectile < m_projectiles.size()
                          && in_range(release.damage.factor, kMaxDamageFactor) && in_range(release.damage.added, kMaxDamageFactor)
                          && in_range(release.damage.hitCharge, kMaxDamageFactor) && release.damage.wire != 0 && !release.damage.type.empty()
                          && in_range(release.aimSpeed, kMaxProjectileSpeed) && in_range(release.leadAngle, 180.0F)
                          && in_range(release.aimAngle, 180.0F) && in_range(release.missMin, kMaxSenseDistance)
                          && in_range(release.missMax, kMaxSenseDistance) && release.range > 0.0F && in_range(release.range, kMaxSenseDistance)
                          && in_range(release.radius, kMaxReach) && in_range(release.accelDelay, kMaxSenseDistance)
                          && release.rowIndex <= kMaxRowIndex;
            }
            if (!correct) {
                m_error = "slice " + std::to_string(index) + " has a ranged attack whose action, time, or releases do not fit together";
                return false;
            }
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
        if (kind.id.empty() || !bodies || weight == 0 || !(kind.healthFactor > 0.0F) || !(kind.healthFactor <= kMaxDamageFactor)
            || kind.level == 0 || kind.level > kMaxLevel) {
            m_error = "kind " + std::to_string(index) + " has no id, no slice, no health, or no level";
            return false;
        }
        if (!std::isfinite(kind.damageFactor) || kind.damageFactor < 0.0F || kind.damageFactor > kMaxDamageFactor) {
            m_error = "kind " + std::to_string(index) + " has a damage factor that is not correct";
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
        if (point.ground && (!std::isfinite(*point.ground) || std::abs(*point.ground) > kMaxVertical)) {
            m_error = "spawn point " + std::to_string(index) + " has a terrain height that is not a height of the world";
            return false;
        }
        // The route of the point: a route of the list, with each waypoint near the point.
        if (point.route) {
            if (*point.route >= m_routes.size()) {
                m_error = "spawn point " + std::to_string(index) + " names a route that the list of the routes does not have";
                return false;
            }
            for (const PatrolWaypoint& waypoint : m_routes[*point.route].points) {
                if (std::hypot(waypoint.position.x - point.position.x, waypoint.position.y - point.position.y) > kLongestPatrol) {
                    m_error = "spawn point " + std::to_string(index) + " has a waypoint more than "
                              + std::to_string(static_cast<int>(kLongestPatrol)) + " m from it";
                    return false;
                }
            }
        }
    }
    for (std::size_t index = 0; index < m_routes.size(); ++index) {
        const PatrolRoute& route = m_routes[index];
        bool correct = !route.points.empty() && route.points.size() <= kMaxRoutePoints;
        for (const PatrolWaypoint& waypoint : route.points) {
            // A wait is a time of its range, or negative for both values (no wait, or for ever).
            const bool waits = waypoint.minWait >= 0.0F && waypoint.maxWait >= waypoint.minWait && waypoint.maxWait <= kLongestWait;
            const bool noWait = waypoint.minWait < 0.0F && waypoint.maxWait < 0.0F;
            correct = correct && std::isfinite(waypoint.position.x) && std::isfinite(waypoint.position.y)
                      && std::isfinite(waypoint.position.z) && std::abs(waypoint.position.x) <= kMaxHorizontal
                      && std::abs(waypoint.position.y) <= kMaxHorizontal && std::abs(waypoint.position.z) <= kMaxVertical
                      && std::isfinite(waypoint.yaw) && std::isfinite(waypoint.minWait) && std::isfinite(waypoint.maxWait)
                      && (waits || noWait) && waypoint.task.size() <= kLongestTask;
        }
        if (!correct) {
            m_error = "route " + std::to_string(index) + " has no waypoint, too many, or a waypoint whose values do not fit together";
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
    return found == m_levels.end() ? 0.0F : found->second.health * kind.healthFactor;
}

float CreatureData::damage(const CreatureKind& kind, std::uint32_t level) const {
    const auto found = m_levels.find(level);
    return found == m_levels.end() ? 0.0F : found->second.damage * kind.damageFactor;
}

float CreatureData::gear_score(std::uint32_t level) const {
    const auto found = m_levels.find(level);
    return found == m_levels.end() ? 0.0F : found->second.gearScore;
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

#include "gamedata/encounter_data.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "gamedata/loot_tables.h"
#include "gamedata/quest_data.h"

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

constexpr std::uint64_t kFormat = 1;
/** Stage QG9a: format 2: the instance recipes (members mode, map, daily), the spawns that the runner skips, and instanceObjects. */
constexpr std::uint64_t kFormatInstances = 2;
// The coordinates of the world (the rule of the other loaders of places).
constexpr double kMaxCoordinate = 100000.0;
// Stage QG5c3: the largest radius of a proximity object and the largest size of a detector shape (meters; the data has 35 m and 50 m).
constexpr float kMaxRadius = 200.0F;
constexpr double kMaxShape = 1000.0;

bool finite_number(const json& value) {
    return value.is_number() && std::isfinite(value.get<double>());
}

bool hex_bytes(const json& value, std::array<std::uint8_t, 16>& out) {
    if (!value.is_string() || value.get<std::string>().size() != 32) {
        return false;
    }
    const std::string text = value.get<std::string>();
    for (std::size_t index = 0; index < 16; ++index) {
        unsigned byte = 0;
        for (std::size_t digit = 0; digit < 2; ++digit) {
            const char letter = text[index * 2 + digit];
            byte <<= 4;
            if (letter >= '0' && letter <= '9') {
                byte |= static_cast<unsigned>(letter - '0');
            } else if (letter >= 'a' && letter <= 'f') {
                byte |= static_cast<unsigned>(letter - 'a' + 10);
            } else if (letter >= 'A' && letter <= 'F') {
                byte |= static_cast<unsigned>(letter - 'A' + 10);
            } else {
                return false;
            }
        }
        out[index] = static_cast<std::uint8_t>(byte);
    }
    return true;
}

bool hex_u64(const json& value, std::uint64_t& out) {
    if (!value.is_string() || value.get<std::string>().empty() || value.get<std::string>().size() > 16) {
        return false;
    }
    out = 0;
    for (const char letter : value.get<std::string>()) {
        out <<= 4;
        if (letter >= '0' && letter <= '9') {
            out |= static_cast<std::uint64_t>(letter - '0');
        } else if (letter >= 'a' && letter <= 'f') {
            out |= static_cast<std::uint64_t>(letter - 'a' + 10);
        } else if (letter >= 'A' && letter <= 'F') {
            out |= static_cast<std::uint64_t>(letter - 'A' + 10);
        } else {
            return false;
        }
    }
    return true;
}

std::string text_of(const json& object, const char* name) {
    return object.is_object() && object.contains(name) && object[name].is_string() ? object[name].get<std::string>() : std::string{};
}

bool flag_of(const json& object, const char* name) {
    return object.is_object() && object.contains(name) && object[name].is_boolean() && object[name].get<bool>();
}

bool whole(const json& object, const char* name, std::uint64_t high, std::uint64_t& out) {
    if (!object.is_object() || !object.contains(name) || !object[name].is_number_unsigned() || object[name].get<std::uint64_t>() > high) {
        return false;
    }
    out = object[name].get<std::uint64_t>();
    return true;
}

bool seconds(const json& object, const char* name, float& out) {
    if (!object.is_object() || !object.contains(name) || !finite_number(object[name]) || object[name].get<double>() < 0.0
        || object[name].get<double>() > EncounterData::kMaxSeconds) {
        return false;
    }
    out = object[name].get<float>();
    return true;
}

bool place_of(const json& entry, state::WorldPosition& position) {
    if (!entry.is_array() || entry.size() != 3) {
        return false;
    }
    for (const json& axis : entry) {
        if (!finite_number(axis) || std::fabs(axis.get<double>()) > kMaxCoordinate) {
            return false;
        }
    }
    position = state::WorldPosition{entry[0].get<float>(), entry[1].get<float>(), entry[2].get<float>()};
    return true;
}

std::chrono::milliseconds milliseconds_of(float value) {
    return std::chrono::milliseconds(static_cast<std::int64_t>(std::lround(static_cast<double>(value) * 1000.0)));
}

/** The result of one shape of a list: a shape, a shape that the runner does not test (skipped), or a size that is not good. */
enum class ShapeRead : std::uint8_t { good, skipped, bad };

/** Reads one shape (stage QG5c3: the detector; stage QG5c4: a trigger): "sphere", "cylinder", or "aabb" with its sizes and its center. */
ShapeRead read_shape(const json& item, EncounterShape& shape, std::string& kind) {
    kind = text_of(item, "shape");
    const auto size = [&](const char* name, float& out) {
        return item.contains(name) && finite_number(item[name]) && item[name].get<double>() > 0.0 && item[name].get<double>() <= kMaxShape
               && ((out = item[name].get<float>()), true);
    };
    const auto triple = [&](const char* name, std::array<float, 3>& out, bool positive) {
        if (!item.contains(name)) {
            return !positive;
        }
        const json& value = item[name];
        if (!value.is_array() || value.size() != 3) {
            return false;
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (!finite_number(value[axis]) || std::fabs(value[axis].get<double>()) > kMaxShape
                || (positive && !(value[axis].get<double>() > 0.0))) {
                return false;
            }
            out[axis] = value[axis].get<float>();
        }
        return true;
    };
    bool good = triple("center", shape.center, false);
    if (kind == "sphere") {
        shape.kind = EncounterShape::Kind::sphere;
        good = good && size("radius", shape.radius);
    } else if (kind == "cylinder") {
        shape.kind = EncounterShape::Kind::cylinder;
        good = good && size("radius", shape.radius) && size("height", shape.height);
    } else if (kind == "aabb") {
        shape.kind = EncounterShape::Kind::aabb;
        good = good && triple("extents", shape.extents, true);
    } else {
        return ShapeRead::skipped; // A shape that the runner does not test (the tool refused the recipe).
    }
    return good ? ShapeRead::good : ShapeRead::bad;
}

/** @return The distance from the place of a shape list that holds each of its shapes. */
float reach_of(const std::vector<EncounterShape>& shapes) {
    float reach = 0.0F;
    for (const EncounterShape& shape : shapes) {
        const float offset = std::sqrt(shape.center[0] * shape.center[0] + shape.center[1] * shape.center[1] + shape.center[2] * shape.center[2]);
        const float size = shape.kind == EncounterShape::Kind::sphere ? shape.radius
                           : shape.kind == EncounterShape::Kind::cylinder
                               ? std::sqrt(shape.radius * shape.radius + shape.height * shape.height * 0.25F)
                               : std::sqrt(shape.extents[0] * shape.extents[0] + shape.extents[1] * shape.extents[1]
                                           + shape.extents[2] * shape.extents[2]);
        reach = std::max(reach, offset + size);
    }
    return reach;
}

/** @return The lower-case texts of a list member (stage QG5c5: the slices of an alias); no list: none. */
std::vector<std::string> lower_list(const json& entry, const char* name) {
    std::vector<std::string> out;
    if (entry.is_object() && entry.contains(name) && entry[name].is_array()) {
        for (const json& value : entry[name]) {
            if (value.is_string()) {
                out.push_back(lower_text(value.get<std::string>()));
            }
        }
    }
    return out;
}

/**
 * Stage QG5c5: reads one part of a group member: its vitals row, its category, its body, its place in the member slice, and its patrol path.
 * @return False for a place, a yaw, or a waypoint that is not a finite number, a policy that the client does not have, or too many waypoints.
 */
bool read_part(const json& entry, EncounterPart& part) {
    part.name = text_of(entry, "name");
    part.vitals = text_of(entry, "vitals");
    part.category = text_of(entry, "category");
    part.body = lower_text(text_of(entry, "body"));
    part.bodySlices = lower_list(entry, "bodySlices");
    if (!entry.contains("offset") || !entry["offset"].is_array() || entry["offset"].size() != 3 || !entry.contains("yaw")
        || !finite_number(entry["yaw"])) {
        return false;
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (!finite_number(entry["offset"][axis]) || std::fabs(entry["offset"][axis].get<double>()) > 1000.0) {
            return false;
        }
        part.offset[axis] = entry["offset"][axis].get<float>();
    }
    part.yaw = entry["yaw"].get<float>();
    if (!entry.contains("route") || !entry["route"].is_object()) {
        return true;
    }
    const json& route = entry["route"];
    std::uint64_t policy = 0;
    if (!whole(route, "policy", 3, policy) || !route.contains("points") || !route["points"].is_array()
        || route["points"].size() > EncounterData::kMaxWaypoints) {
        return false;
    }
    part.route.policy = static_cast<PatrolPolicy>(policy);
    for (const json& point : route["points"]) {
        // [x, y, z, yaw, min wait, max wait, task, alignment] in the coordinates of the member slice.
        if (!point.is_array() || point.size() != 8) {
            return false;
        }
        for (std::size_t at = 0; at < 6; ++at) {
            if (!finite_number(point[at])) {
                return false;
            }
        }
        PatrolWaypoint waypoint;
        waypoint.position = {point[0].get<float>(), point[1].get<float>(), point[2].get<float>()};
        waypoint.yaw = point[3].get<float>();
        waypoint.minWait = point[4].get<float>();
        waypoint.maxWait = point[5].get<float>();
        waypoint.task = point[6].is_string() ? point[6].get<std::string>() : std::string();
        waypoint.align = point[7].is_number() && point[7].get<double>() != 0.0;
        part.route.points.push_back(std::move(waypoint));
    }
    return true;
}

EncounterObjectiveKind objective_kind(const std::string& type) {
    return type == "InvasionWaveObjective"               ? EncounterObjectiveKind::timed
           : type == "SliceDestroyedObjective"           ? EncounterObjectiveKind::destroyed
           : type == "SliceSpawnedObjective"             ? EncounterObjectiveKind::spawned
           : type == "EntityActiveStateChangedObjective" ? EncounterObjectiveKind::entityState
           : type == "WaitForParentObjective"            ? EncounterObjectiveKind::waitParent
           : type == "CutsceneFinishedObjective"         ? EncounterObjectiveKind::cutscene
           : type == "EncounterEventObjective"           ? EncounterObjectiveKind::event
                                                         : EncounterObjectiveKind::other;
}

/**
 * Reads one objective of a stage: its class, its time, its count and its tracked slice, its texts, and (stage QG5c4) the triggers of an
 * entity-state objective. @return False for a time limit that is not a number of 0 to an hour or a trigger with no place or a bad shape.
 */
bool read_objective(const json& objective, EncounterObjective& out) {
    out.type = text_of(objective, "type");
    out.kind = objective_kind(out.type);
    if (out.kind == EncounterObjectiveKind::other) {
        return true; // A class that the runner does not run: the tool refused the recipe, and the runner refuses it too.
    }
    if (!seconds(objective, "timeLimit", out.timeLimit)) {
        return false;
    }
    std::uint64_t number = 0;
    out.succeedOnTimeout = flag_of(objective, "succeedOnTimeout");
    out.requiredCount = whole(objective, "requiredCount", EncounterData::kMaxCount * EncounterData::kMaxSpawns, number)
                            ? static_cast<std::uint32_t>(number) : 0;
    out.tracked = lower_text(text_of(objective, "trackedSlice"));
    if (out.tracked.empty()) {
        out.tracked = lower_text(text_of(objective, "trackedAlias"));
    }
    out.trackedSlices = lower_list(objective, "trackedSlices");
    out.title = text_of(objective, "title");
    out.description = text_of(objective, "description");
    out.cutscene = text_of(objective, "cutscene");
    out.cutsceneCrc = out.cutscene.empty() ? 0 : quest_crc(out.cutscene);
    out.eventId = text_of(objective, "eventId");
    if (out.kind == EncounterObjectiveKind::entityState && objective.contains("triggers") && objective["triggers"].is_array()) {
        for (const json& item : objective["triggers"]) {
            EncounterTrigger trigger;
            trigger.entity = text_of(item, "entity");
            trigger.deadTargets = flag_of(item, "deadTargets");
            if (!place_of(item.value("position", json()), trigger.position) || !item.contains("shapes") || !item["shapes"].is_array()) {
                return false;
            }
            for (const json& value : item["shapes"]) {
                EncounterShape shape;
                std::string kind;
                if (read_shape(value, shape, kind) != ShapeRead::good) {
                    return false;
                }
                trigger.shapes.push_back(shape);
            }
            trigger.reach = reach_of(trigger.shapes);
            out.triggers.push_back(std::move(trigger));
        }
    }
    return true;
}

} // namespace

bool EncounterData::fail(std::string cause) {
    m_loaded = false;
    m_error = std::move(cause);
    m_recipes.clear();
    m_members.clear();
    m_memberIndex.clear();
    return false;
}

const EncounterMember* EncounterData::member(std::string_view slice) const {
    const auto found = m_memberIndex.find(slice);
    return found != m_memberIndex.end() ? &m_members[found->second] : nullptr;
}

bool EncounterData::load(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return fail("the file is not there");
    }
    std::stringstream text;
    text << file.rdbuf();
    return load_text(text.str());
}

bool EncounterData::load_text(const std::string& text) {
    fail("");
    const json document = json::parse(text, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return fail("the file is not a JSON object");
    }
    const std::uint64_t format = document.contains("format") && document["format"].is_number_unsigned() ? document["format"].get<std::uint64_t>() : 0;
    if (format != kFormat && format != kFormatInstances) {
        return fail("the file does not have format 1 or 2");
    }
    if (!document.contains("recipes") || !document["recipes"].is_array() || !document.contains("members") || !document["members"].is_object()) {
        return fail("the file does not have the members recipes and members");
    }
    if (document["recipes"].size() > kMaxRecipes) {
        return fail("the file has more than " + std::to_string(kMaxRecipes) + " recipes");
    }
    for (const auto& [slice, entry] : document["members"].items()) {
        EncounterMember member;
        member.slice = lower_text(slice);
        const std::string kind = text_of(entry, "kind");
        member.kind = kind == "creature" ? EncounterMemberKind::creature : kind == "object" ? EncounterMemberKind::object : EncounterMemberKind::unknown;
        if (!entry.is_object() || (kind != "creature" && kind != "object" && kind != "unknown")) {
            return fail("the member " + slice + " has no kind creature, object, or unknown");
        }
        member.vitals = text_of(entry, "vitals");
        member.category = text_of(entry, "category");
        member.body = lower_text(text_of(entry, "body"));
        if (entry.contains("bodySlices") && entry["bodySlices"].is_array()) {
            for (const json& path : entry["bodySlices"]) {
                if (path.is_string()) {
                    member.bodySlices.push_back(lower_text(path.get<std::string>()));
                }
            }
        }
        member.variant = text_of(entry, "variant");
        member.row = text_of(entry, "row");
        member.event = text_of(entry, "event");
        member.tag = text_of(entry, "tag");
        member.onlyOnQuest = flag_of(entry, "onlyOnQuest");
        member.proximity = flag_of(entry, "proximity");
        member.cause = text_of(entry, "cause");
        member.interact = !entry.contains("interact") || flag_of(entry, "interact");
        member.direct = flag_of(entry, "direct");
        member.stationary = flag_of(entry, "stationary");
        // Stage QG5c5: the parts of a group member (one for each creature spawner of its slice).
        if (entry.contains("group") && entry["group"].is_array()) {
            if (entry["group"].size() > kMaxParts) {
                return fail("the member " + slice + " has more than " + std::to_string(kMaxParts) + " parts");
            }
            for (const json& item : entry["group"]) {
                EncounterPart part;
                if (!item.is_object() || !read_part(item, part) || part.vitals.empty() || part.body.empty()) {
                    return fail("the member " + slice + " has a part with no vitals row, no body, or a place, a yaw, or a path that is not "
                                "correct");
                }
                member.parts.push_back(std::move(part));
            }
        }
        if (entry.contains("radius") && (!seconds(entry, "radius", member.radius) || member.radius > kMaxRadius)) {
            return fail("the member " + slice + " has a radius that is not a number of 0 to " + std::to_string(static_cast<int>(kMaxRadius)));
        }
        if (entry.contains("lifetime") && !seconds(entry, "lifetime", member.lifetime)) {
            return fail("the member " + slice + " has a lifetime that is not a number of 0 to an hour");
        }
        if (member.kind == EncounterMemberKind::object && member.proximity && !(member.radius > 0.0F)) {
            return fail("the proximity object " + slice + " has no radius");
        }
        if (member.kind == EncounterMemberKind::creature && (member.vitals.empty() || member.body.empty())) {
            return fail("the creature member " + slice + " has no vitals row or no body");
        }
        if (!m_memberIndex.emplace(member.slice, static_cast<std::uint32_t>(m_members.size())).second) {
            return fail("two members have the slice " + slice);
        }
        m_members.push_back(std::move(member));
    }
    std::set<std::string> keys;
    for (const json& entry : document["recipes"]) {
        EncounterRecipe recipe;
        recipe.key = lower_text(text_of(entry, "key"));
        const std::string what = "the recipe " + recipe.key;
        std::uint64_t sub = 0;
        std::uint64_t slot = 0;
        if (recipe.key.empty() || !keys.insert(recipe.key).second) {
            return fail("a recipe has no key, or two recipes have the key " + recipe.key);
        }
        recipe.slice = lower_text(text_of(entry, "slice"));
        recipe.rootName = text_of(entry, "rootName");
        recipe.layer = text_of(entry, "layer");
        recipe.pattern = text_of(entry, "pattern");
        recipe.season = lower_text(text_of(entry, "season"));
        recipe.mode = text_of(entry, "mode");
        recipe.map = text_of(entry, "map");
        recipe.daily = flag_of(entry, "daily");
        if (!hex_bytes(entry.value("asset", json()), recipe.asset) || !whole(entry, "sub", 0xFFFFFFFFULL, sub)
            || !hex_u64(entry.value("root", json()), recipe.root) || !place_of(entry.value("position", json()), recipe.position)
            || !entry.contains("yaw") || !finite_number(entry["yaw"])) {
            return fail(what + " has no asset, sub, root, position, or yaw");
        }
        recipe.sub = static_cast<std::uint32_t>(sub);
        recipe.yaw = entry["yaw"].get<float>();
        if (entry.contains("rotation") && entry["rotation"].is_array() && entry["rotation"].size() == 4) {
            for (std::size_t axis = 0; axis < 4; ++axis) {
                if (!finite_number(entry["rotation"][axis])) {
                    return fail(what + " has a rotation that is not finite");
                }
                recipe.rotation[axis] = entry["rotation"][axis].get<float>();
            }
        }
        const json& slots = entry.value("slots", json::object());
        if (!whole(slots, "manager", 4095, slot)) {
            return fail(what + " has no manager slot");
        }
        recipe.managerSlot = static_cast<std::uint32_t>(slot);
        recipe.placeSlot = whole(slots, "place", 4095, slot) ? static_cast<std::uint32_t>(slot) : 0;
        recipe.objectivesSlot = whole(slots, "objectives", 4095, slot) ? static_cast<std::uint32_t>(slot) : 0;
        const json& manager = entry.value("manager", json::object());
        std::uint64_t number = 0;
        recipe.autoActivate = flag_of(manager, "autoActivate");
        recipe.activateDelay = std::chrono::milliseconds(whole(manager, "autoActivateDelayMs", 3600000, number) ? number : 0);
        recipe.managerGrace = whole(manager, "gracePeriod", 3600, number) ? static_cast<std::uint32_t>(number) : 0;
        recipe.destroyOnCompletion = flag_of(manager, "destroyOnCompletion");
        recipe.autoPerceive = flag_of(manager, "autoPerceiveParticipants");
        recipe.overrideAggroDelay = flag_of(manager, "overrideAggroDelay");
        if (manager.contains("aggroDelay") && !seconds(manager, "aggroDelay", recipe.aggroDelay)) {
            return fail(what + " has an aggro delay that is not a number of 0 to an hour");
        }
        if (entry.contains("tracker") && entry["tracker"].is_object()) {
            const json& tracker = entry["tracker"];
            if (!hex_u64(tracker.value("component", json()), recipe.trackerComponent) || !whole(tracker, "grace", 3600, number)) {
                return fail(what + " has a participant tracker with no component or no grace");
            }
            recipe.participantGrace = static_cast<std::uint32_t>(number);
            recipe.participantEvent = text_of(tracker, "addedEvent");
        }
        if (entry.contains("detector") && entry["detector"].is_object()) {
            const json& detector = entry["detector"];
            if (!detector.contains("radius") || !finite_number(detector["radius"]) || detector["radius"].get<double>() < 0.0
                || detector["radius"].get<double>() > 1000.0) {
                return fail(what + " has a detector radius that is not a number from 0 to 1000");
            }
            recipe.detectorRadius = detector["radius"].get<float>();
            recipe.detectorFilter = text_of(detector, "filter");
            // Stage QG5c3: the shapes of the detector (a sphere, a cylinder, an axis box); a file of before: one sphere of the radius.
            if (detector.contains("shapes") && detector["shapes"].is_array()) {
                for (const json& item : detector["shapes"]) {
                    EncounterShape shape;
                    std::string kind;
                    const ShapeRead read = read_shape(item, shape, kind);
                    if (read == ShapeRead::skipped) {
                        continue; // The detector stays empty for a shape that the runner does not test.
                    }
                    if (read == ShapeRead::bad) {
                        return fail(what + " has a detector shape " + kind + " with no size, or a size that is not a number of 0 to 1000");
                    }
                    recipe.detectorShapes.push_back(shape);
                }
            } else if (recipe.detectorRadius > 0.0F) {
                EncounterShape sphere;
                sphere.radius = recipe.detectorRadius;
                recipe.detectorShapes.push_back(sphere);
            }
            recipe.detectorReach = reach_of(recipe.detectorShapes);
        }
        recipe.successEvent = text_of(entry, "successEvent");
        if (entry.contains("questEvents") && entry["questEvents"].is_array()) {
            for (const json& event : entry["questEvents"]) {
                if (event.is_string()) {
                    recipe.questEvents.push_back(lower_text(event.get<std::string>()));
                }
            }
        }
        const json& parent = entry.value("parent", json::object());
        recipe.parent.kind = text_of(parent, "kind");
        recipe.parent.maintainPopulation = flag_of(parent, "maintainPopulation");
        recipe.parent.spawnOnEnable = flag_of(parent, "spawnOnEnable");
        recipe.parent.liveCount = whole(parent, "liveCount", 1000, number) ? static_cast<std::uint32_t>(number) : 0;
        if (parent.contains("respawn") && parent["respawn"].is_array() && parent["respawn"].size() == 2) {
            for (std::size_t bound = 0; bound < 2; ++bound) {
                const json& value = parent["respawn"][bound];
                if (!finite_number(value) || value.get<double>() < 0.0 || value.get<double>() > 7.0 * 24.0 * 3600.0) {
                    return fail(what + " has a parent respawn that is not a number of 0 to a week");
                }
            }
            recipe.parent.respawnMin = parent["respawn"][0].get<float>();
            recipe.parent.respawnMax = parent["respawn"][1].get<float>();
        }
        if (!entry.contains("stages") || !entry["stages"].is_array() || entry["stages"].size() > kMaxStages) {
            return fail(what + " has no list of stages, or more than " + std::to_string(kMaxStages));
        }
        for (const json& item : entry["stages"]) {
            EncounterStage stage;
            stage.name = text_of(item, "name");
            if (!hex_u64(item.value("entity", json()), stage.entity) || !hex_u64(item.value("component", json()), stage.component)
                || !whole(item, "slot", 4095, slot)) {
                return fail(what + ": the stage " + stage.name + " has no entity, component, or slot");
            }
            stage.slot = static_cast<std::uint32_t>(slot);
            stage.userWave = flag_of(item, "userWave");
            const json& objective = item.value("objective", json::object());
            if (!read_objective(objective, stage.objective)) {
                return fail(what + ": the stage " + stage.name + " has a time limit that is not a number of 0 to an hour, or a trigger "
                            "with no place or no good shape");
            }
            // Stage QG5c4: the other objectives of the stage ("more").
            if (objective.contains("more") && objective["more"].is_array()) {
                if (objective["more"].size() > kMaxSpawns) {
                    return fail(what + ": the stage " + stage.name + " has more than " + std::to_string(kMaxSpawns) + " objectives");
                }
                for (const json& other : objective["more"]) {
                    EncounterObjective more;
                    if (!read_objective(other, more)) {
                        return fail(what + ": an objective of the stage " + stage.name + " has a time limit that is not a number of 0 to an "
                                    "hour");
                    }
                    stage.more.push_back(std::move(more));
                }
            }
            stage.childrenOptional = flag_of(item, "childrenOptional");
            if (item.contains("children") && item["children"].is_array()) {
                for (const json& child : item["children"]) {
                    if (!child.is_number_unsigned() || child.get<std::uint64_t>() >= entry["stages"].size()) {
                        return fail(what + ": the stage " + stage.name + " names a child stage that is not a stage");
                    }
                    stage.children.push_back(child.get<std::uint32_t>());
                }
            }
            for (const char* list : {"startActions", "endActions"}) {
                if (item.contains(list) && item[list].is_array()) {
                    for (const json& action : item[list]) {
                        const bool atStart = std::string_view(list) == "startActions";
                        if (text_of(action, "type") == "KillSpawnsAction") {
                            (atStart ? stage.killSpawnsAtStart : stage.killSpawnsAtEnd) = true;
                        } else if (text_of(action, "type") == "DestroySliceAction") {
                            // Stage QG5c5: the members of a slice or of an alias go.
                            EncounterDestroy destroy{lower_text(text_of(action, "slice")), lower_text(text_of(action, "alias")),
                                                     lower_list(action, "aliasSlices")};
                            (atStart ? stage.destroyAtStart : stage.destroyAtEnd).push_back(std::move(destroy));
                        }
                    }
                }
            }
            if (!item.contains("spawns") || !item["spawns"].is_array() || item["spawns"].size() > kMaxSpawns) {
                return fail(what + ": the stage " + stage.name + " has no list of spawns, or more than " + std::to_string(kMaxSpawns));
            }
            for (const json& value : item["spawns"]) {
                EncounterSpawn spawn;
                spawn.member = lower_text(text_of(value, "member"));
                spawn.alias = lower_text(text_of(value, "alias"));
                spawn.skip = flag_of(value, "skip");
                float delay = 0.0F;
                if (!seconds(value, "delay", delay) || !whole(value, "count", kMaxCount, number)) {
                    return fail(what + ": a spawn of the stage " + stage.name + " has no delay of 0 to an hour or no count of 0 to "
                                + std::to_string(kMaxCount));
                }
                spawn.delay = milliseconds_of(delay);
                spawn.count = static_cast<std::uint32_t>(number);
                const auto member = m_memberIndex.find(spawn.member);
                if (spawn.member.empty() || member == m_memberIndex.end()) {
                    // A spawn of a slice that the members do not have: the tool refused the recipe; the index is not used.
                    spawn.memberIndex = static_cast<std::uint32_t>(m_members.size());
                } else {
                    spawn.memberIndex = member->second;
                }
                if (value.contains("locations") && value["locations"].is_array()) {
                    for (const json& location : value["locations"]) {
                        EncounterPlace place;
                        if (!place_of(location.value("position", json()), place.position) || !location.contains("yaw")
                            || !finite_number(location["yaw"])) {
                            return fail(what + ": a spawn location of the stage " + stage.name + " has no position or yaw");
                        }
                        place.yaw = location["yaw"].get<float>();
                        spawn.locations.push_back(place);
                    }
                }
                stage.spawns.push_back(std::move(spawn));
            }
            recipe.stages.push_back(std::move(stage));
        }
        if (entry.contains("stageOrder") && entry["stageOrder"].is_array()) {
            for (const json& index : entry["stageOrder"]) {
                if (!index.is_number_unsigned() || index.get<std::uint64_t>() >= recipe.stages.size()) {
                    return fail(what + " has a stage order with an index that is not a stage");
                }
                recipe.stageOrder.push_back(index.get<std::uint32_t>());
            }
        }
        // Stage QG5c3: the parent of each child stage. A stage that two lists name, a stage of the manager in a child list, or a cycle
        // stops the load (the tool reads each stage one time, in a breadth walk from the manager).
        for (std::uint32_t index = 0; index < recipe.stages.size(); ++index) {
            for (const std::uint32_t child : recipe.stages[index].children) {
                EncounterStage& stage = recipe.stages[child];
                if (child == index || stage.parent >= 0
                    || std::find(recipe.stageOrder.begin(), recipe.stageOrder.end(), child) != recipe.stageOrder.end()) {
                    return fail(what + ": the stage " + stage.name + " is in two stage lists");
                }
                stage.parent = static_cast<std::int32_t>(index);
            }
        }
        for (EncounterStage& stage : recipe.stages) {
            std::size_t depth = 0;
            for (std::int32_t at = stage.parent; at >= 0; at = recipe.stages[static_cast<std::size_t>(at)].parent) {
                if (++depth > recipe.stages.size()) {
                    return fail(what + ": the child stages of the stage " + stage.name + " make a cycle");
                }
            }
        }
        // The collection of the client (0x26F1FD0): the user-wave stages in the stage order, from 0.
        for (const std::uint32_t index : recipe.stageOrder) {
            EncounterStage& stage = recipe.stages[index];
            if (stage.userWave && stage.userIndex < 0) {
                stage.userIndex = static_cast<std::int32_t>(recipe.userWaves++);
            }
        }
        // Stage QG5c4 (static read D of stage QG5c4): the task tree of the client. The collector 0x26F1FD0 walks the stages of the manager
        // and their child stages in their order and takes each objective of a user-wave stage in the order of m_objectives; 0x44650F0 gives
        // each objective that makes a task (a timed, a destroyed, a spawned objective; an entity-state objective with a description; no
        // WaitForParentObjective) a leaf and a wrapper of one child: leaf 2m + 1, wrapper 2m + 2 for the task m (from 0), the outer container
        // 2T + 1 for T tasks.
        std::vector<std::uint32_t> walk(recipe.stageOrder.rbegin(), recipe.stageOrder.rend());
        while (!walk.empty()) {
            EncounterStage& stage = recipe.stages[walk.back()];
            walk.pop_back();
            if (stage.userWave) {
                for (std::size_t at = 0; at <= stage.more.size(); ++at) {
                    EncounterObjective& objective = at == 0 ? stage.objective : stage.more[at - 1];
                    const bool task = objective.kind == EncounterObjectiveKind::timed || objective.kind == EncounterObjectiveKind::destroyed
                                      || objective.kind == EncounterObjectiveKind::spawned
                                      || (objective.kind == EncounterObjectiveKind::entityState && !objective.description.empty());
                    if (task) {
                        objective.leaf = static_cast<std::int32_t>(2 * recipe.taskLeaves + 1);
                        objective.target = objective.kind == EncounterObjectiveKind::destroyed || objective.kind == EncounterObjectiveKind::spawned
                                               ? objective.requiredCount : 1;
                        ++recipe.taskLeaves;
                    }
                }
            }
            walk.insert(walk.end(), stage.children.rbegin(), stage.children.rend());
        }
        if (recipe.taskLeaves > 100) {
            return fail(what + " has more than 100 tasks of the client (a task index is one byte)");
        }
        if (entry.contains("refused") && entry["refused"].is_array()) {
            for (const json& cause : entry["refused"]) {
                if (cause.is_string()) {
                    recipe.refused.push_back(cause.get<std::string>());
                }
            }
        }
        m_recipes.push_back(std::move(recipe));
    }
    // Stage QG9a (format 2): the objects of the mode root slices.
    if (document.contains("instanceObjects")) {
        if (!document["instanceObjects"].is_array()) {
            return fail("the member instanceObjects is not a list");
        }
        for (const json& item : document["instanceObjects"]) {
            InstanceObject object;
            object.mode = text_of(item, "mode");
            object.map = text_of(item, "map");
            object.key = lower_text(text_of(item, "key"));
            object.slice = lower_text(text_of(item, "slice"));
            object.spawner = text_of(item, "spawner");
            object.onInit = flag_of(item, "onInit");
            if (object.mode.empty() || object.key.empty() || object.slice.empty() || !place_of(item.value("position", json()), object.position)
                || !item.contains("yaw") || !finite_number(item["yaw"])) {
                return fail("an instance object has no mode, key, slice, position, or yaw");
            }
            object.yaw = item["yaw"].get<float>();
            m_instanceObjects.push_back(std::move(object));
        }
    }
    m_loaded = true;
    return true;
}

} // namespace soulwarden::backend::gamedata

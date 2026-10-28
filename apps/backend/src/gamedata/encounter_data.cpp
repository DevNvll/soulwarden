#include "gamedata/encounter_data.h"

#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "gamedata/loot_tables.h"

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

constexpr std::uint64_t kFormat = 1;
// The coordinates of the world (the rule of the other loaders of places).
constexpr double kMaxCoordinate = 100000.0;

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
    if (!document.contains("format") || !document["format"].is_number_unsigned() || document["format"].get<std::uint64_t>() != kFormat) {
        return fail("the file does not have format 1");
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
            stage.objective.type = text_of(objective, "type");
            stage.objective.kind = stage.objective.type == "InvasionWaveObjective" ? EncounterObjectiveKind::timed
                                   : stage.objective.type == "SliceDestroyedObjective" ? EncounterObjectiveKind::destroyed
                                                                                        : EncounterObjectiveKind::other;
            if (stage.objective.kind != EncounterObjectiveKind::other) {
                if (!seconds(objective, "timeLimit", stage.objective.timeLimit)) {
                    return fail(what + ": the stage " + stage.name + " has a time limit that is not a number of 0 to an hour");
                }
                stage.objective.succeedOnTimeout = flag_of(objective, "succeedOnTimeout");
                stage.objective.requiredCount = whole(objective, "requiredCount", kMaxCount * kMaxSpawns, number)
                                                    ? static_cast<std::uint32_t>(number) : 0;
                stage.objective.tracked = lower_text(text_of(objective, "trackedSlice"));
                if (stage.objective.tracked.empty()) {
                    stage.objective.tracked = lower_text(text_of(objective, "trackedAlias"));
                }
                stage.objective.title = text_of(objective, "title");
                stage.objective.description = text_of(objective, "description");
            }
            for (const char* list : {"startActions", "endActions"}) {
                if (item.contains(list) && item[list].is_array()) {
                    for (const json& action : item[list]) {
                        if (text_of(action, "type") == "KillSpawnsAction") {
                            (std::string_view(list) == "startActions" ? stage.killSpawnsAtStart : stage.killSpawnsAtEnd) = true;
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
        // The collection of the client (0x26F1FD0): the user-wave stages in the stage order, from 0.
        for (const std::uint32_t index : recipe.stageOrder) {
            EncounterStage& stage = recipe.stages[index];
            if (stage.userWave && stage.userIndex < 0) {
                stage.userIndex = static_cast<std::int32_t>(recipe.userWaves++);
            }
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
    m_loaded = true;
    return true;
}

} // namespace soulwarden::backend::gamedata

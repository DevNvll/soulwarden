#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <utility>
#include <vector>

#include "core/logging/log.h"
#include "server/http/character_commands.h"
#include "server/http/router.h"

// The creature commands of the common layer (character_commands.h): the development routes
// (tools/world) and the admin routes (the tool window) use the same functions. Each command
// calls the creature system of the world (world/creatures.h). A route set gives its rule for
// the creature of a command (CreatureReach): a development route changes each creature of the
// world, and an admin route changes only a creature near the character of the command.

namespace soulwarden::backend::server::http::commands {
namespace {

using nlohmann::json;

/** A new creature of a command is this far to the north of the character when the command gives no place (meters). */
constexpr float kDefaultSpawnDistance = 3.0F;
/** Largest distance of a new creature from the character of the command (meters). */
constexpr float kSpawnOffsetLimit = 500.0F;

std::string id_text(world::EntityId id) {
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(id));
    return text;
}

/** @return The id of a creature from its text of 16 hexadecimal digits, or no value. */
std::optional<world::EntityId> creature_id(const Arguments& arguments) {
    const auto text = arguments.text("creature");
    if (!text || text->size() != 16 || text->find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
        return std::nullopt;
    }
    return static_cast<world::EntityId>(std::strtoull(text->c_str(), nullptr, 16));
}

const char* error_text(world::CreatureError error) {
    switch (error) {
    case world::CreatureError::none:
        return "done";
    case world::CreatureError::noData:
        return "the backend has no creature data: make the file with tools/gamedata/creatures.sh";
    case world::CreatureError::unknownKind:
        return "the creature data has no kind with this id";
    case world::CreatureError::unknownCreature:
        return "the world has no creature with this id";
    case world::CreatureError::badPosition:
        return "the position is not a position of the world";
    case world::CreatureError::badLevel:
        return "the level table of the game has no row for this level";
    case world::CreatureError::badHealth:
        return "the health must be a number from 0 to the maximum health of the creature";
    case world::CreatureError::dead:
        return "the creature is dead: its health does not change";
    case world::CreatureError::unknownAction:
        return "the action table of the slice of the creature has no action with this name that has a fragment";
    case world::CreatureError::immune:
        return "the creature is on its way back: it takes no damage";
    }
    return "error";
}

int status_of(world::CreatureError error) {
    return error == world::CreatureError::unknownCreature || error == world::CreatureError::unknownKind
                   || error == world::CreatureError::unknownAction
               ? 404
               : 400;
}

json creature_json(const world::Entity& entity) {
    const world::Creature& creature = entity.creature;
    return {{"id", id_text(entity.id)},
            {"actor", world::actor_text(entity.actor)},
            {"kind", creature.kind->id},
            {"name", entity.name},
            {"level", creature.level},
            {"health", entity.vitals.health},
            {"healthMax", entity.vitals.healthMax},
            {"dead", creature.dead},
            {"spawnPoint", creature.spawnPoint.has_value()},
            {"slice", creature.body->path},
            {"position", {entity.position.x, entity.position.y, entity.position.z}},
            {"yaw", creature.yaw},
            // The behavior of the creature: what it does, its target, and if it fights and walks.
            {"mode", creature.mind.mode == world::CreatureMode::fight  ? "fight"
                     : creature.mind.mode == world::CreatureMode::home ? "home"
                                                                       : "rest"},
            {"target", creature.mind.target != 0 ? id_text(creature.mind.target) : std::string()},
            {"fights", creature.body->fights()},
            // `walks` has a meaning only when `groundKnown` is true: the world looks at the floor
            // of a creature when the navigation data of its place is there.
            {"walks", creature.mind.walks},
            {"groundKnown", creature.mind.groundKnown}};
}

service::HttpResponse creature_reply(const world::CreatureResult& result) {
    if (result.error != world::CreatureError::none) {
        return json_reply(status_of(result.error), json{{"message", error_text(result.error)}}.dump());
    }
    json reply{{"message", "done"}, {"applied", "world"}};
    if (result.entity != nullptr) {
        reply["creature"] = creature_json(*result.entity);
    }
    return json_reply(200, reply.dump());
}

/**
 * The rule of a route set for the creature of a command (CreatureReach, world thread).
 * @return No value when the command can change the creature, or the reply of the refusal.
 */
std::optional<service::HttpResponse> out_of_reach(world::World& target, const std::string& character, world::EntityId id,
                                                  CreatureReach reach) {
    if (reach == CreatureReach::world) {
        return std::nullopt; // The creature system gives the reply for an id of no creature.
    }
    const world::Entity* reference = target.find_character(character);
    if (reference == nullptr) {
        return message(404, "the character is not in the world: a creature command needs its place");
    }
    // A creature that is farther gets the reply of an id of no creature: the reply does not say
    // which creatures are at a different place of the world.
    const world::Entity* creature = target.creatures().find(id);
    if (creature == nullptr || world::horizontal_distance(reference->position, creature->position) > kAdminCreatureRange) {
        char text[96];
        std::snprintf(text, sizeof(text), "no creature with this id is %.0f m or less from the character",
                      static_cast<double>(kAdminCreatureRange));
        return message(404, text);
    }
    return std::nullopt;
}

} // namespace

json creature_kind_json(const gamedata::CreatureData& data, const gamedata::CreatureKind& kind) {
    return {{"id", kind.id},
            {"name", kind.name},
            {"vitalsName", kind.vitalsName},
            {"category", kind.category},
            {"family", kind.family},
            {"creatureType", kind.creatureType},
            {"level", kind.level},
            {"health", data.health(kind, kind.level)},
            {"spawnPoints", kind.points},
            {"slice", data.slices()[kind.bodies.front().slice].path}};
}

json creatures_view(world::World& target, const std::string& character, std::optional<float> range) {
    const world::Entity* reference = character.empty() ? nullptr : target.find_character(character);
    world::Creatures& system = target.creatures();
    std::vector<std::pair<float, const world::Entity*>> found;
    for (const world::EntityId id : system.ids()) {
        const world::Entity* entity = system.find(id);
        if (entity == nullptr) {
            continue;
        }
        const float distance = reference != nullptr ? world::horizontal_distance(reference->position, entity->position) : 0.0F;
        if (range && (reference == nullptr || distance > *range)) {
            continue;
        }
        found.emplace_back(distance, entity);
    }
    std::sort(found.begin(), found.end(), [](const auto& left, const auto& right) {
        return left.first != right.first ? left.first < right.first : left.second->id < right.second->id;
    });
    json creatures = json::array();
    const world::Behavior& behavior = system.behavior();
    for (const auto& [distance, entity] : found) {
        json row = creature_json(*entity);
        if (reference != nullptr) {
            row["distance"] = distance;
        }
        // The roam (stage 3.4e): the patrol route or the wander of the creature, and what it does now.
        const world::CreatureMind& mind = entity->creature.mind;
        const gamedata::PatrolRoute* route = behavior.route_of(*entity);
        const auto& values = entity->creature.body->behavior;
        row["roam"] = route != nullptr ? "patrol" : values && values->wander && values->wanderFarthest > 0.0F ? "wander" : "none";
        row["roaming"] = behavior.roaming(entity->id);
        // A creature with no behavior data of its slice has no speed: it does not roam.
        row["behaviorData"] = values.has_value();
        row["settled"] = mind.settled;
        if (route != nullptr) {
            row["waypoint"] = mind.waypoint;
            row["waypoints"] = route->points.size();
        }
        row["immune"] = entity->creature.immune();
        // The behavior tree (stage BT2): its name, the binding that selected it, and the refusal of the tree of its binding.
        const world::CreatureBrain& brain = mind.brain;
        row["tree"] = brain.tree ? brain.tree->name : std::string();
        row["treeBinding"] = brain.binding;
        if (!brain.refusal.empty()) {
            row["treeRefused"] = brain.refusal;
        }
        // A forced movement that runs (stage 3.3b-S4): the damage row of its hit.
        if (entity->creature.forced) {
            row["forced"] = entity->creature.forced->row;
        }
        row["holds"] = mind.holdUntil.has_value();
        row["retracing"] = mind.retracing;
        row["anchor"] = {mind.anchor.x, mind.anchor.y, mind.anchor.z};
        // The respawn time of the spawn point of a dead creature (stage 3.5a): seconds from now.
        // Code review of stage DG2, finding DG2R-R1: the point of the spawn set of the space of the creature.
        const world::SpawnSet* points = entity->creature.spawnPoint ? system.spawn_set(entity->space) : nullptr;
        if (entity->creature.dead && points != nullptr && *entity->creature.spawnPoint < points->points.size()) {
            const world::SpawnPointState& point = points->points[*entity->creature.spawnPoint];
            if (point.waiting && point.respawnAt) {
                row["respawnIn"] = std::max(0.0, std::chrono::duration<double>(*point.respawnAt - target.clock().now()).count());
            }
        }
        creatures.push_back(std::move(row));
    }
    const std::size_t waiting = system.waiting_count();
    const world::CreatureCounts& counts = system.counts();
    return json{{"data", system.has_data()},
                {"character", character},
                {"inWorld", reference != nullptr},
                {"creatures", creatures},
                {"total", system.count()},
                {"spawnPoints", system.spawn_points().size()},
                {"spawnPointsWaiting", waiting},
                {"counts",
                 {{"spawnedByPoint", counts.spawnedByPoint},
                  {"spawnedByCommand", counts.spawnedByCommand},
                  {"removedByDistance", counts.removedByDistance},
                  {"removedByCommand", counts.removedByCommand},
                  {"removedByRespawn", counts.removedByRespawn},
                  {"renewals", counts.renewals},
                  {"deaths", counts.deaths},
                  {"largest", counts.largest}}},
                {"revision", target.revision()}};
}

WorldCommand spawn_creature(const std::string& character, const Arguments& arguments, CreatureReach,
                            service::HttpResponse& refusal) {
    const auto kind = arguments.text("kind");
    if (!kind || kind->empty()) {
        refusal = message(400, "give kind=ID: the id of a kind of the creature data");
        return {};
    }
    std::optional<std::uint32_t> level;
    if (arguments.has("level")) {
        const auto value = arguments.integer("level", 1, 1000);
        if (!value) {
            refusal = message(400, "level must be a number from 1 to 1000");
            return {};
        }
        level = static_cast<std::uint32_t>(*value);
    }
    // The place of the creature from the character. An offset that is there with a wrong form
    // is a wrong request: it is not 0.
    state::WorldPosition offset;
    bool placed = false;
    const std::pair<const char*, float*> offsets[] = {{"dx", &offset.x}, {"dy", &offset.y}, {"dz", &offset.z}};
    for (const auto& [name, value] : offsets) {
        if (!arguments.has(name)) {
            continue;
        }
        const auto number = arguments.number(name, -kSpawnOffsetLimit, kSpawnOffsetLimit);
        if (!number) {
            refusal = message(400, "dx, dy, and dz must be numbers from -500 to 500 (meters: east, north, up)");
            return {};
        }
        *value = *number;
        placed = true;
    }
    if (!placed) {
        offset.y = kDefaultSpawnDistance;
    }
    // With no direction the creature looks at the character of the command: a creature has
    // its vision to its front, and a creature with its back to the character does not see it.
    std::optional<float> wantedYaw;
    if (arguments.has("yaw")) {
        const auto value = arguments.number("yaw", -360.0F, 360.0F);
        if (!value) {
            refusal = message(400, "yaw must be a number from -360 to 360 (degrees)");
            return {};
        }
        wantedYaw = *value;
    }
    return [character, kind = *kind, level, offset, wantedYaw](world::World& target) {
        const world::Entity* reference = target.find_character(character);
        if (reference == nullptr) {
            return message(404, "the character is not in the world: a new creature needs its place");
        }
        if (reference->space != world::kMainSpace) {
            // Stage DG1: an instance has no content and no level data (the heights and the navigation of its level are stage DG2).
            return message(409, "the character is in an instance: a creature of a command comes there with the level data of stage DG2");
        }
        state::WorldPosition position{reference->position.x + offset.x, reference->position.y + offset.y,
                                      reference->position.z + offset.z};
        const float yaw = wantedYaw                                                              ? *wantedYaw
                          : world::horizontal_distance(position, reference->position) > 0.01F ? world::yaw_to(position, reference->position)
                                                                                              : 0.0F;
        // The ground is not level. A character that stands on a floor of the navigation gives the
        // creature the floor at the place of the creature that is nearest to the height of the
        // character: a creature on no floor does not leave its place. With no navigation data the
        // floor is the terrain, and a character that is not on the terrain gives its own height.
        const std::optional<float> under = target.navigation().ground_height(reference->position);
        const std::optional<float> there = target.navigation().ground_height(position);
        if (under && there && std::fabs(reference->position.z - *under) <= gamedata::kTerrainTolerance) {
            position.z = *there + offset.z;
        }
        const world::CreatureResult result = target.creatures().spawn(kind, position, yaw, level);
        if (result.entity != nullptr) {
            log::write("world: command: creature %s of the kind %s near character %s",
                       id_text(result.entity->id).c_str(),
                       result.entity->creature.kind->id.c_str(),
                       character.substr(0, 8).c_str());
        }
        return creature_reply(result);
    };
}

WorldCommand remove_creature(const std::string& character, const Arguments& arguments, CreatureReach reach,
                             service::HttpResponse& refusal) {
    const auto id = creature_id(arguments);
    if (!id) {
        refusal = message(400, "give creature=ID: the id of a creature (16 hexadecimal digits)");
        return {};
    }
    return [character, id = *id, reach](world::World& target) {
        if (const auto refused = out_of_reach(target, character, id, reach)) {
            return *refused;
        }
        const world::CreatureResult result = target.creatures().remove(id);
        if (result.error == world::CreatureError::none) {
            log::write("world: command: creature %s removed", id_text(id).c_str());
        }
        return creature_reply(result);
    };
}

WorldCommand set_creature_health(const std::string& character, const Arguments& arguments, CreatureReach reach,
                                 service::HttpResponse& refusal) {
    const auto id = creature_id(arguments);
    const bool absolute = arguments.has("health");
    const auto value = absolute ? arguments.number("health", 0.0F, 100000000.0F) : arguments.number("percent", 0.0F, 100.0F);
    if (!id || !value || (absolute && arguments.has("percent"))) {
        refusal = message(400, "give creature=ID, and health=N (0 to the maximum health) or percent=N (0 to 100)");
        return {};
    }
    return [character, id = *id, reach, absolute, value = *value](world::World& target) {
        if (const auto refused = out_of_reach(target, character, id, reach)) {
            return *refused;
        }
        const world::Entity* entity = target.creatures().find(id);
        if (entity == nullptr) {
            return creature_reply(world::CreatureResult{world::CreatureError::unknownCreature, nullptr});
        }
        // A percent value is that part of the maximum, with the product and the quotient in
        // double precision. The product of two floats is exact there: 100 percent is the exact
        // maximum, and no percent value gives a health above it. In float precision the product
        // of a maximum and 100, divided by 100, can be one float step above the maximum (the
        // creature system refuses it) or one step below it.
        const float health = absolute ? value
                                      : static_cast<float>(static_cast<double>(entity->vitals.healthMax)
                                                           * static_cast<double>(value) / 100.0);
        const world::CreatureResult result = target.creatures().set_health(id, health);
        if (result.error == world::CreatureError::none) {
            log::write("world: command: health of creature %s is %.1f of %.1f",
                       id_text(id).c_str(),
                       static_cast<double>(result.entity->vitals.health),
                       static_cast<double>(result.entity->vitals.healthMax));
        }
        return creature_reply(result);
    };
}

WorldCommand kill_creature(const std::string& character, const Arguments& arguments, CreatureReach reach,
                           service::HttpResponse& refusal) {
    const auto id = creature_id(arguments);
    if (!id) {
        refusal = message(400, "give creature=ID: the id of a creature (16 hexadecimal digits)");
        return {};
    }
    return [character, id = *id, reach](world::World& target) {
        if (const auto refused = out_of_reach(target, character, id, reach)) {
            return *refused;
        }
        return creature_reply(target.creatures().kill(id));
    };
}

WorldCommand give_creature_effect(const std::string& character, const Arguments& arguments, CreatureReach reach,
                                  service::HttpResponse& refusal) {
    const auto id = creature_id(arguments);
    const auto status = arguments.text("status");
    std::optional<float> seconds;
    if (arguments.has("seconds")) {
        seconds = arguments.number("seconds", 0.0F, 3600.0F);
    }
    if (!id || !status || status->empty() || (arguments.has("seconds") && !seconds)) {
        refusal = message(400, "give creature=ID and status=NAME (a status row of the ability data, or knockdown or "
                               "stagger), and when necessary seconds=N (0 to 3600)");
        return {};
    }
    return [character, id = *id, status = *status, seconds, reach](world::World& target) {
        if (const auto refused = out_of_reach(target, character, id, reach)) {
            return *refused;
        }
        world::Entity* creature = target.creatures().find(id);
        if (creature == nullptr) {
            return message(404, "the world has no creature with this id");
        }
        bool done = false;
        if (status == "knockdown" || status == "stagger") {
            const bool knockdown = status == "knockdown";
            const auto time = seconds ? std::chrono::milliseconds(static_cast<long long>(*seconds * 1000.0F))
                                      : (knockdown ? world::ability_rules::kKnockdownTime : world::ability_rules::kStaggerTime);
            done = target.creatures().knock(*creature, knockdown ? world::ability_rules::kKnockdownPower
                                                                 : world::ability_rules::kStaggerPower, time);
        } else {
            done = target.abilities().apply_status(*creature, status, 0, seconds);
        }
        if (!done) {
            return message(400, "the creature took no effect: the ability data has no such status, or the creature is dead "
                                "or on its way back");
        }
        log::write("world: command: creature %s has the effect %s", id_text(id).c_str(), status.c_str());
        return creature_reply(world::CreatureResult{world::CreatureError::none, creature});
    };
}

WorldCommand play_creature_action(const std::string& character, const Arguments& arguments, CreatureReach reach,
                                  service::HttpResponse& refusal) {
    const auto id = creature_id(arguments);
    const auto action = arguments.text("action");
    std::optional<float> seconds;
    if (arguments.has("seconds")) {
        seconds = arguments.number("seconds", 0.0F, 3600.0F);
    }
    // Development aid: the tag state of the fragments, as 24 hexadecimal digits.
    std::optional<gamedata::FragmentTags> tags;
    if (arguments.has("tags")) {
        const auto text = arguments.text("tags");
        if (text && text->size() == 2 * gamedata::kFragmentTagBytes
            && text->find_first_not_of("0123456789abcdefABCDEF") == std::string::npos) {
            tags.emplace();
            for (std::size_t index = 0; index < tags->size(); ++index) {
                (*tags)[index] = static_cast<std::uint8_t>(std::strtoul(text->substr(index * 2, 2).c_str(), nullptr, 16));
            }
        }
    }
    if (!id || !action || action->empty() || (arguments.has("seconds") && !seconds) || (arguments.has("tags") && !tags)) {
        refusal = message(400, "give creature=ID and action=NAME (an action of the action table of its slice), and when "
                               "necessary seconds=N (the time of the action, 0 to 3600) and tags=HEX (24 digits)");
        return {};
    }
    return [character, id = *id, action = *action, seconds, tags, reach](world::World& target) {
        if (const auto refused = out_of_reach(target, character, id, reach)) {
            return *refused;
        }
        const world::CreatureResult result = target.creatures().play(id, action, seconds, tags);
        if (result.error == world::CreatureError::none) {
            const world::CreatureAction& started = *result.entity->creature.action;
            log::write("world: command: creature %s starts the action %s (index %u, fragment index %u)%s",
                       id_text(id).c_str(),
                       action.c_str(),
                       started.action,
                       started.fragment,
                       seconds ? " with an end" : "");
        }
        return creature_reply(result);
    };
}

WorldCommand creature_shoot(const std::string& character, const Arguments& arguments, CreatureReach reach,
                            service::HttpResponse& refusal) {
    const auto id = creature_id(arguments);
    if (!id) {
        refusal = message(400, "give creature=ID (a creature with a basic shot) and character=ID (its target)");
        return {};
    }
    return [character, id = *id, reach](world::World& target) {
        if (const auto refused = out_of_reach(target, character, id, reach)) {
            return *refused;
        }
        world::Entity* creature = target.creatures().find(id);
        const world::Entity* aimed = target.find_character(character);
        if (creature == nullptr || aimed == nullptr) {
            return message(404, "the world has no creature with this id, or the character is not in the world");
        }
        if (!target.creatures().behavior().command_shot(*creature, aimed->id)) {
            return message(400, "the creature does not shoot: it has no basic shot, it is dead, it holds, the character is dead or "
                                "ignored, or no tree of it has a node basic_shot of its band");
        }
        log::write("world: command: creature %s shoots at %s", id_text(id).c_str(), character.c_str());
        return creature_reply(world::CreatureResult{world::CreatureError::none, creature});
    };
}

WorldCommand creature_area(const std::string& character, const Arguments& arguments, CreatureReach reach, service::HttpResponse& refusal) {
    const auto id = creature_id(arguments);
    if (!id) {
        refusal = message(400, "give creature=ID (a creature with an area attack) and character=ID (its target)");
        return {};
    }
    return [character, id = *id, reach](world::World& target) {
        if (const auto refused = out_of_reach(target, character, id, reach)) {
            return *refused;
        }
        world::Entity* creature = target.creatures().find(id);
        const world::Entity* aimed = target.find_character(character);
        if (creature == nullptr || aimed == nullptr) {
            return message(404, "the world has no creature with this id, or the character is not in the world");
        }
        if (!target.creatures().behavior().command_area(*creature, aimed->id)) {
            return message(400, "the creature makes no area attack: it has no area attack with spell data, it is dead, it holds, the "
                                "character is dead or ignored, or no tree of it has a node area_attack");
        }
        log::write("world: command: creature %s starts its area attack at %s", id_text(id).c_str(), character.c_str());
        return creature_reply(world::CreatureResult{world::CreatureError::none, creature});
    };
}

WorldCommand creature_status_action(const std::string& character, const Arguments& arguments, CreatureReach reach,
                                    service::HttpResponse& refusal) {
    const auto id = creature_id(arguments);
    if (!id) {
        refusal = message(400, "give creature=ID (a creature with a status action) and, if you want, action=NAME");
        return {};
    }
    std::string action;
    if (arguments.has("action")) {
        const auto text = arguments.text("action");
        if (!text || text->empty() || text->size() > 128) {
            refusal = message(400, "action=NAME must be the name of an action");
            return {};
        }
        action = *text;
    }
    return [character, id = *id, reach, action](world::World& target) {
        if (const auto refused = out_of_reach(target, character, id, reach)) {
            return *refused;
        }
        world::Entity* creature = target.creatures().find(id);
        if (creature == nullptr) {
            return message(404, "the world has no creature with this id");
        }
        if (!target.creatures().behavior().command_status_action(*creature, action)) {
            return message(400, "the creature starts no status action: it has no such status action, it is dead, it holds, or no tree of "
                                "it has a node status_action");
        }
        log::write("world: command: creature %s starts its status action %s", id_text(id).c_str(), action.empty() ? "(the first)" : action.c_str());
        return creature_reply(world::CreatureResult{world::CreatureError::none, creature});
    };
}

namespace {

const char* cause_name(world::StepCause cause) {
    switch (cause) {
    case world::StepCause::none:
        return "none";
    case world::StepCause::tree:
        return "tree";
    case world::StepCause::held:
        return "held (a stun, a knockdown, a stagger, or a forced movement)";
    case world::StepCause::hit_stun:
        return "hit stun";
    case world::StepCause::action:
        return "an action plays";
    case world::StepCause::way_back:
        return "the way back";
    case world::StepCause::no_tree:
        return "no tree";
    }
    return "none";
}

const char* mode_name(world::CreatureMode mode) {
    switch (mode) {
    case world::CreatureMode::rest:
        return "rest";
    case world::CreatureMode::fight:
        return "fight";
    case world::CreatureMode::home:
        return "home";
    }
    return "rest";
}

/** @return Seconds from now to a time of the world clock (0 for a time that passed). */
double seconds_to(world::WorldClock::time_point time, world::WorldClock::time_point now) {
    return time > now ? std::chrono::duration<double>(time - now).count() : 0.0;
}

} // namespace

WorldCommand creature_tree(const std::string& character, const Arguments& arguments, CreatureReach reach, service::HttpResponse& refusal) {
    const auto id = creature_id(arguments);
    if (!id) {
        refusal = message(400, "give creature=ID (16 hexadecimal digits)");
        return {};
    }
    return [character, id = *id, reach](world::World& target) {
        if (const auto refused = out_of_reach(target, character, id, reach)) {
            return *refused;
        }
        const world::Entity* creature = target.creatures().find(id);
        if (creature == nullptr) {
            return message(404, "the world has no creature with this id");
        }
        const world::CreatureMind& mind = creature->creature.mind;
        const world::CreatureBrain& brain = mind.brain;
        const world::WorldClock::time_point now = target.clock().now();
        json reply{{"creature", id_text(id)}, {"kind", creature->creature.kind->id}, {"mode", mode_name(mind.mode)}};
        json step{{"cause", cause_name(brain.cause)},
                  {"secondsAgo", brain.at.time_since_epoch().count() != 0 ? std::chrono::duration<double>(now - brain.at).count() : -1.0},
                  {"decided", brain.decided != 0 ? std::string(world::tree::kind_name(static_cast<world::tree::NodeKind>(brain.decided - 1)))
                                                 : std::string()}};
        reply["step"] = step;
        // The binding (stage BT2): the binding that selected the tree, and the refusal of the tree of its binding.
        reply["binding"] = brain.binding;
        if (!brain.refusal.empty()) {
            reply["refused"] = brain.refusal;
        }
        if (brain.tree) {
            const world::tree::CompiledTree& tree = *brain.tree;
            reply["tree"] = tree.name;
            reply["source"] = tree.source;
            json nodes = json::array();
            for (std::size_t index = 0; index < tree.nodes.size(); ++index) {
                const world::tree::Node& node = tree.nodes[index];
                json parameters = json::object();
                for (const auto& [name, value] : world::tree::parameters(tree, index)) {
                    parameters[name] = (name == "key" && static_cast<std::size_t>(value) < tree.keys.size())
                                           ? json(tree.keys[static_cast<std::size_t>(value)].name)
                                       : (name == "action" && static_cast<std::size_t>(value) < tree.actions.size())
                                           ? json(tree.actions[static_cast<std::size_t>(value)].name)
                                           : json(value);
                }
                const auto status = static_cast<world::tree::Status>(index < brain.last.size() ? brain.last[index] : 0);
                nodes.push_back({{"index", index},
                                 {"depth", node.depth},
                                 {"node", world::tree::kind_name(node.kind)},
                                 {"name", node.label},
                                 {"status", world::tree::status_name(status)},
                                 {"parameters", parameters}});
            }
            reply["nodes"] = nodes;
            json keys = json::object();
            for (std::size_t slot = 0; slot < tree.keys.size() && slot < brain.board.size(); ++slot) {
                keys[tree.keys[slot].name] = brain.board[slot];
            }
            reply["blackboard"]["keys"] = keys;
        }
        // The facts of the creature that the leaves use (the names of the auto-evaluated keys of the client where the meaning is the same:
        // Has_Target, Target_Vitals_State; research BT0, section 2).
        const world::Entity* goal = mind.target != 0 ? target.find(mind.target) : nullptr;
        json facts{{"Has_Target", mind.target != 0},
                   {"Health_Percent", creature->vitals.healthMax > 0.0F ? 100.0 * creature->vitals.health / creature->vitals.healthMax : 0.0},
                   {"Anchor_Distance", world::horizontal_distance(creature->position, mind.anchor)},
                   {"Attacks", mind.attacks},
                   {"Ready_In", seconds_to(mind.ready, now)},
                   {"Shot_Ready_In", seconds_to(mind.shotReady, now)},
                   {"Area_Ready_In", seconds_to(mind.areaReady, now)},
                   {"Status_Ready_In", seconds_to(mind.statusReady, now)},
                   {"Retreating", mind.retreating},
                   {"Closing", mind.closing},
                   {"No_Way_Hold_In", mind.holdUntil ? seconds_to(*mind.holdUntil, now) : -1.0}};
        if (goal != nullptr) {
            facts["Target"] = goal->kind == world::EntityKind::player ? goal->name : std::string("a summon");
            facts["Target_Kind"] = goal->kind == world::EntityKind::player ? "player" : "summon";
            facts["Target_Distance"] = world::horizontal_distance(creature->position, goal->position);
            facts["Target_Vitals_State"] = goal->kind == world::EntityKind::player && goal->life.dead ? "dead" : "alive";
        }
        reply["blackboard"]["facts"] = facts;
        return json_reply(200, reply.dump());
    };
}

WorldCommand reload_creature_trees(const std::string&, const Arguments&, CreatureReach, service::HttpResponse& refusal) {
    if (!harness_gate()) {
        refusal = message(403, "the reload of the behavior trees is a harness command: the backend needs SOULWARDEN_HARNESS=1");
        return {};
    }
    return [](world::World& target) {
        world::tree::LoadReport report;
        const bool loaded = target.creatures().behavior().reload_trees(target.options().behaviorDirectory, report);
        log::write("world: command: the behavior trees again (%zu tree(s), %zu file(s) refused)", report.loaded.size(), report.refused.size());
        return json_reply(loaded ? 200 : 500, json{{"loaded", report.loaded},
                                                   {"refused", report.refused},
                                                   {"bindings", report.bindings},
                                                   {"folder", !target.options().behaviorDirectory.empty()}}
                                                  .dump());
    };
}

WorldCommand set_creature_tree(const std::string& character, const Arguments& arguments, CreatureReach reach,
                               service::HttpResponse& refusal) {
    if (!harness_gate()) {
        refusal = message(403, "the tree of a creature is a harness command: the backend needs SOULWARDEN_HARNESS=1");
        return {};
    }
    const auto id = creature_id(arguments);
    const std::string name = arguments.text("tree").value_or(std::string());
    if (!id || name.empty() || name.size() > 64) {
        refusal = message(400, "give creature=ID (16 hexadecimal digits) and tree=NAME (a tree of the set)");
        return {};
    }
    return [character, id = *id, reach, name](world::World& target) {
        if (const auto refused = out_of_reach(target, character, id, reach)) {
            return *refused;
        }
        world::Entity* creature = target.creatures().find(id);
        if (creature == nullptr) {
            return message(404, "the world has no creature with this id");
        }
        std::string cause;
        switch (target.creatures().behavior().set_tree(*creature, name, cause)) {
        case world::Behavior::TreeCommand::unknownTree:
            return message(404, cause);
        case world::Behavior::TreeCommand::refused:
            return json_reply(409, json{{"message", "refused: " + cause + "; the creature keeps its tree"},
                                        {"creature", id_text(id)},
                                        {"tree", creature->creature.mind.brain.tree ? creature->creature.mind.brain.tree->name : std::string()}}
                                       .dump());
        case world::Behavior::TreeCommand::done:
            break;
        }
        return json_reply(200, json{{"message", "done"},
                                    {"creature", id_text(id)},
                                    {"kind", creature->creature.kind->id},
                                    {"tree", name},
                                    {"binding", creature->creature.mind.brain.binding}}
                                   .dump());
    };
}

WorldCommand move_creature_anchor(const std::string& character, const Arguments& arguments, CreatureReach reach,
                                  service::HttpResponse& refusal) {
    const auto id = creature_id(arguments);
    const auto x = arguments.number("x", -world::kMaxHorizontalPosition, world::kMaxHorizontalPosition);
    const auto y = arguments.number("y", -world::kMaxHorizontalPosition, world::kMaxHorizontalPosition);
    const auto z = arguments.number("z", world::kLowestTravelHeight, world::kHighestTravelHeight);
    if (!id || !x || !y || !z) {
        refusal = message(400, "give creature=ID and the place of the anchor in meters (x, y, and z: a height from -99 to 999)");
        return {};
    }
    return [character, id = *id, reach, anchor = state::WorldPosition{*x, *y, *z}](world::World& target) {
        if (const auto refused = out_of_reach(target, character, id, reach)) {
            return *refused;
        }
        world::Entity* entity = target.creatures().find(id);
        if (entity == nullptr) {
            return creature_reply(world::CreatureResult{world::CreatureError::unknownCreature, nullptr});
        }
        if (!target.creatures().behavior().move_anchor(*entity, anchor)) {
            return message(409, "the creature has no target: only a creature in a fight has an anchor of a fight");
        }
        return creature_reply(world::CreatureResult{world::CreatureError::none, entity});
    };
}

WorldCommand set_respawn_time(const std::string& character, const Arguments& arguments, CreatureReach reach,
                              service::HttpResponse& refusal) {
    const auto id = creature_id(arguments);
    const auto seconds = arguments.number("seconds", 0.0F, 86400.0F);
    if (!id || !seconds) {
        refusal = message(400, "give creature=ID (a dead creature of a spawn point) and seconds (0 to 86400)");
        return {};
    }
    return [character, id = *id, reach, seconds = *seconds](world::World& target) {
        if (const auto refused = out_of_reach(target, character, id, reach)) {
            return *refused;
        }
        world::Entity* entity = target.creatures().find(id);
        if (entity == nullptr) {
            return creature_reply(world::CreatureResult{world::CreatureError::unknownCreature, nullptr});
        }
        if (!entity->creature.dead || !entity->creature.spawnPoint) {
            return message(409, "the creature is not a dead creature of a spawn point");
        }
        const auto time = target.clock().now() + std::chrono::milliseconds(static_cast<long long>(seconds * 1000.0F));
        // Code review of stage DG2, finding DG2R-R1: the spawn set of the space of the creature.
        if (!target.creatures().respawn_at(entity->space, *entity->creature.spawnPoint, time)) {
            return message(409, "the spawn set of the space of the creature has no such spawn point");
        }
        log::write("world: command: the spawn point of creature %016llx makes its creature again in %.0f s",
                   static_cast<unsigned long long>(id),
                   static_cast<double>(seconds));
        return creature_reply(world::CreatureResult{world::CreatureError::none, entity});
    };
}

WorldCommand reset_creatures(const std::string&, const Arguments&, CreatureReach, service::HttpResponse&) {
    return [](world::World& target) {
        const std::size_t count = target.creatures().reset_spawn_points();
        log::write("world: command: %zu spawn point(s) that waited are free again", count);
        return json_reply(200, json{{"message", "done"}, {"applied", "world"}, {"spawnPoints", count}}.dump());
    };
}

} // namespace soulwarden::backend::server::http::commands

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
    for (const auto& [distance, entity] : found) {
        json row = creature_json(*entity);
        if (reference != nullptr) {
            row["distance"] = distance;
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

WorldCommand reset_creatures(const std::string&, const Arguments&, CreatureReach, service::HttpResponse&) {
    return [](world::World& target) {
        const std::size_t count = target.creatures().reset_spawn_points();
        log::write("world: command: %zu spawn point(s) that waited are free again", count);
        return json_reply(200, json{{"message", "done"}, {"applied", "world"}, {"spawnPoints", count}}.dump());
    };
}

} // namespace soulwarden::backend::server::http::commands

#include <optional>
#include <string>

#include "core/logging/log.h"
#include "server/http/character_commands.h"
#include "server/http/router.h"

// The instance commands of the common layer (character_commands.h; stage DG1, docs/systems/instances.md): the entry of a character into
// an instance of a map of the game modes, and its return to its return place (world/instances.h). Only the development routes have them
// (the harness gate) until stage DG3 gives the entrance of the game.

namespace soulwarden::backend::server::http::commands {
namespace {

using nlohmann::json;

json place_json(const state::WorldPosition& place) {
    return json::array({place.x, place.y, place.z});
}

json instance_json(const world::World& world, const world::Instance& instance) {
    json members = json::array();
    for (const auto& [characterId, member] : instance.members) {
        members.push_back({{"character", characterId},
                           {"returnPlace", place_json(member.returnPlace)},
                           {"seconds", std::chrono::duration<double>(world.clock().now() - member.entered).count()}});
    }
    json result = {{"id", instance.id},
                   {"space", instance.space},
                   {"map", instance.map},
                   {"mode", instance.mode},
                   {"level", instance.level.level},
                   {"terrain", instance.level.terrain},
                   {"start", place_json(instance.start)},
                   {"heading", instance.heading},
                   {"capacity", instance.capacity},
                   {"seconds", std::chrono::duration<double>(world.clock().now() - instance.started).count()},
                   {"members", members}};
    // Stage DG2: the spawn points of the level set of the space (an own level), with their creatures.
    if (const world::SpawnSet* set = instance.space != world::kMainSpace ? world.creatures().spawn_set(instance.space) : nullptr) {
        std::size_t creatures = 0;
        for (const world::SpawnPointState& point : set->points) {
            creatures += point.entity != 0 ? 1 : 0;
        }
        result["levelSpawnPoints"] = set->points.size();
        result["levelCreatures"] = creatures;
        result["levelWaiting"] = set->waiting.size();
    }
    return result;
}

json part_json(const gamedata::LevelPart& part) {
    json result = {{"loaded", part.loaded}, {"bytes", part.bytes}};
    if (!part.loaded) {
        result["problem"] = part.problem;
    }
    return result;
}

/** Stage DG2: the own levels that the world read (world/levels.h). */
json levels_json(const world::World& world) {
    json list = json::array();
    for (const auto& [name, level] : world.levels().levels()) {
        json entry = {{"level", name},
                      {"set", level.data != nullptr},
                      {"loadMilliseconds", std::chrono::duration<double, std::milli>(level.loadTime).count()},
                      {"spaces", json(level.spaces)},
                      {"navigation", level.navigation != nullptr}};
        if (level.data != nullptr) {
            const gamedata::LevelData& data = *level.data;
            entry["accepted"] = data.accepted();
            if (!data.accepted()) {
                entry["problem"] = data.problem();
            } else {
                entry["terrain"] = data.terrain();
                entry["creatures"] = part_json(data.creature_part());
                entry["creatures"]["spawnPoints"] = data.creatures().spawn_points().size();
                entry["creatures"]["kinds"] = data.creatures().kinds().size();
                entry["heights"] = part_json(data.height_part());
                entry["navigationData"] = part_json(data.navigation_part());
                entry["navigationData"]["regions"] = data.navigation().regions();
                entry["interactions"] = part_json(data.interaction_part());
                entry["interactions"]["objects"] = data.interactions().objects().size();
            }
        }
        if (level.navigation != nullptr) {
            const world::NavigationCounts& counts = level.navigation->counts();
            entry["tilesHeld"] = counts.tilesHeld;
            entry["tilesLoaded"] = counts.tilesLoaded;
            entry["searches"] = counts.searches;
        }
        list.push_back(std::move(entry));
    }
    const world::LevelCounts& counts = world.levels().counts();
    return {{"source", world.levels().source() != nullptr},
            {"levels", list},
            {"counts", {{"reads", counts.reads}, {"refused", counts.refused}, {"noSet", counts.noSet}, {"spacesStarted", counts.spacesStarted},
                        {"spacesEnded", counts.spacesEnded}}}};
}

service::HttpResponse refusal_of(const world::InstanceResult& result) {
    switch (result.error) {
    case world::InstanceError::notInWorld:
    case world::InstanceError::unknownMap:
    case world::InstanceError::unknownInstance:
        return message(404, world::instance_error_text(result.error));
    case world::InstanceError::storage:
        return message(500, world::instance_error_text(result.error));
    case world::InstanceError::noData:
        return message(400, world::instance_error_text(result.error));
    default:
        return message(409, world::instance_error_text(result.error));
    }
}

} // namespace

WorldCommand enter_instance(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto map = arguments.text("map");
    if (!map || map->empty() || map->size() > 64) {
        refusal = message(400, "give the id of a map of the game modes (map, \"DungeonAmrine\"; tools/world instance maps)");
        return {};
    }
    std::uint64_t join = 0;
    if (arguments.has("instance")) {
        const auto id = arguments.integer("instance", 1, 0xFFFFFFFFFFFFULL);
        if (!id) {
            refusal = message(400, "instance must be the id of an instance (a whole number from 1)");
            return {};
        }
        join = *id;
    }
    return [character, map = *map, join](world::World& target) {
        const world::InstanceResult result = target.instances().enter(character, map, join);
        if (result.error != world::InstanceError::none) {
            return refusal_of(result);
        }
        return json_reply(200, json{{"message", "done"}, {"instance", instance_json(target, *result.instance)}, {"place", place_json(result.place)}}
                                   .dump());
    };
}

WorldCommand return_instance(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    (void)arguments;
    (void)refusal;
    return [character](world::World& target) {
        const world::InstanceResult result = target.instances().leave(character);
        if (result.error != world::InstanceError::none) {
            return refusal_of(result);
        }
        return json_reply(200, json{{"message", "done"},
                                    {"instance", instance_json(target, *result.instance)},
                                    {"place", place_json(result.place)},
                                    {"ended", result.ended}}
                                   .dump());
    };
}

json instances_json(world::World& world) {
    const world::Instances& instances = world.instances();
    json list = json::array();
    for (const auto& [id, instance] : instances.instances()) {
        list.push_back(instance_json(world, instance));
    }
    const world::InstanceCounts& counts = instances.counts();
    return {{"data", instances.data() != nullptr},
            {"instances", list},
            {"levels", levels_json(world)},
            {"counts",
             {{"made", counts.made},
              {"ended", counts.ended},
              {"entries", counts.entries},
              {"exits", counts.exits},
              {"departures", counts.departures},
              {"recovered", counts.recovered},
              {"refused", counts.refused},
              {"questObjectsMade", counts.questObjectsMade},
              {"questObjectsGathered", counts.questObjectsGathered},
              {"questObjectsAgain", counts.questObjectsAgain}}}};
}

json game_maps_json(const gamedata::GameModes* modes) {
    const bool loaded = modes != nullptr && modes->loaded();
    json reply{{"loaded", loaded}, {"maps", json::array()}};
    if (loaded) {
        for (const gamedata::GameMap& map : modes->maps()) {
            json starts = json::array();
            for (const gamedata::ModeStart& start : map.starts) {
                starts.push_back({start.position.x, start.position.y, start.position.z, start.heading});
            }
            reply["maps"].push_back({{"id", map.id},
                                     {"mode", map.mode},
                                     {"name", map.name},
                                     {"level", map.level},
                                     {"terrain", map.terrain},
                                     {"starts", starts},
                                     {"recommendedLevel", map.recommendedLevel},
                                     {"requiredLevel", map.requiredLevel},
                                     {"teamCapacity", map.teamCapacity},
                                     {"minGroupSize", map.minGroupSize},
                                     {"dungeon", map.dungeon},
                                     {"soloTrial", map.soloTrial},
                                     {"raidTrial", map.raidTrial},
                                     {"timeoutSeconds", map.timeoutSeconds}});
        }
    }
    return reply;
}

} // namespace soulwarden::backend::server::http::commands

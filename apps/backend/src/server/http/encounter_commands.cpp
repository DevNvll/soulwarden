#include <cstdio>
#include <string>

#include "server/http/character_commands.h"
#include "server/http/router.h"

// The commands of the encounters (stage QG5c2, world/encounters.h, docs/systems/encounters.md), in the common layer of the development routes
// (character_commands.h): the instances near a character (the stage, the count of its destroyed spawns, the participants, the members), the
// counts of the runner, and a development aid: the end of the instance of a placement (the placement is free again at once).

namespace soulwarden::backend::server::http::commands {
namespace {

using nlohmann::json;

std::string hex64(std::uint64_t value) {
    char text[17];
    std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(value));
    return text;
}

} // namespace

json encounters_view(world::World& world, const std::string& character, float radius) {
    world::Encounters& encounters = world.encounters();
    const world::Entity* entity = world.find_character(character);
    const world::EncounterCounts& counts = encounters.counts();
    json reply{{"inWorld", entity != nullptr},
               {"data", encounters.data() != nullptr},
               {"radius", radius},
               {"instances", json::array()},
               {"links", json::array()},
               {"counts",
                {{"placements", counts.placements}, {"taken", counts.taken}, {"made", counts.made}, {"succeeded", counts.succeeded},
                 {"failed", counts.failed}, {"dormant", counts.dormant}, {"membersMade", counts.membersMade}, {"membersLowered", counts.membersLowered},
                 {"membersRemoved", counts.membersRemoved}, {"memberDeaths", counts.memberDeaths}, {"objectsMade", counts.objectsMade},
                 {"objectsGathered", counts.objectsGathered}, {"objectsFound", counts.objectsFound},
                 {"proximityLooks", counts.proximityLooks}, {"timedEnds", counts.timedEnds}, {"spawnsSkipped", counts.spawnsSkipped},
                 {"triggersFound", counts.triggersFound}, {"triggerLooks", counts.triggerLooks},
                 {"objectivesVoided", counts.objectivesVoided}, {"findRetries", counts.findRetries},
                 {"partsMade", counts.partsMade}, {"partsHidden", counts.partsHidden}, {"partsFilled", counts.partsFilled},
                 {"slicesDestroyed", counts.slicesDestroyed},
                 {"oldGeneration", counts.oldGeneration},
                 {"participantsAdded", counts.participantsAdded}, {"participantsLeft", counts.participantsLeft}, {"credits", counts.credits},
                 {"creditRetries", counts.creditRetries}, {"creditsDropped", counts.creditsDropped}, {"scans", counts.scans},
                 {"placementsLooked", counts.placementsLooked}, {"entitiesLooked", counts.entitiesLooked}, {"due", encounters.due_count()}}}};
    if (entity == nullptr) {
        return reply;
    }
    for (const world::EntityId root : entity->encounterLinks) {
        reply["links"].push_back(hex64(root));
    }
    for (const world::EncounterView& view : encounters.view(entity->position, radius)) {
        json members = json::array();
        for (const world::EntityId member : view.members) {
            members.push_back(hex64(member));
        }
        reply["instances"].push_back({{"key", view.key},           {"slice", view.slice},
                                      {"root", hex64(view.root)},  {"generation", view.generation},
                                      {"phase", world::encounter_phase_text(view.phase)},
                                      {"stage", view.stage},       {"destroyed", view.destroyed},
                                      {"required", view.required}, {"participants", view.participants},
                                      {"members", members},        {"distance", view.distance},
                                      {"running", view.running}});
    }
    return reply;
}

json encounter_recipes(world::World& world) {
    json recipes = json::array();
    std::size_t taken = 0;
    for (const world::EncounterPlacement& placement : world.encounters().loaded_placements()) {
        const gamedata::EncounterRecipe& recipe = *placement.recipe;
        taken += placement.refused.empty() ? 1 : 0;
        recipes.push_back({{"key", recipe.key},
                           {"slice", recipe.slice},
                           {"root", recipe.rootName},
                           {"pattern", recipe.pattern},
                           {"position", {recipe.position.x, recipe.position.y, recipe.position.z}},
                           {"questEvents", recipe.questEvents},
                           {"successEvent", recipe.successEvent},
                           {"taken", placement.refused.empty()},
                           {"live", placement.live},
                           {"refused", placement.refused}});
    }
    return {{"data", world.encounters().data() != nullptr}, {"recipes", recipes}, {"taken", taken},
            {"refused", recipes.size() - taken}};
}

WorldCommand use_start_object(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    (void)arguments;
    (void)refusal;
    return [character](world::World& target) {
        world::Entity* entity = target.find_character(character);
        if (entity == nullptr) {
            return message(404, "the character is not in the world");
        }
        const std::optional<world::EntityId> object = target.encounters().use_start_object(*entity);
        if (!object) {
            return message(404, "no start object of a running encounter stage is 10 m or less from the character");
        }
        return json_reply(200, json{{"applied", "world"},
                                    {"object", hex64(*object)},
                                    {"message", "DEVELOPMENT COMMAND: the start object counts as gathered by the character (no gather)"}}
                                   .dump());
    };
}

WorldCommand reset_encounter(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    (void)character;
    const auto key = arguments.text("key");
    if (!key || key->empty()) {
        refusal = message(400, "give key (the key PLACEMENT_UUID/SPAWNER_ENTITY of a recipe: tools/world encounter)");
        return {};
    }
    return [key = *key](world::World& target) {
        if (!target.encounters().placement_of(key)) {
            return message(404, "no recipe has this key");
        }
        const bool ended = target.encounters().reset(key);
        return json_reply(200, json{{"applied", "world"},
                                    {"ended", ended},
                                    {"message", ended ? "DEVELOPMENT COMMAND: the instance ended; the placement is free again at once"
                                                      : "the placement has no instance; it is free again at once"}}
                                   .dump());
    };
}

} // namespace soulwarden::backend::server::http::commands

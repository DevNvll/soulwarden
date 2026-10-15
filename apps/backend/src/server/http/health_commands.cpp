#include <string>

#include "core/logging/log.h"
#include "server/http/character_commands.h"
#include "server/http/router.h"

// The commands for the health of a character and for the behavior of the creatures toward it
// (world/health.h, world/behavior.h), in the common layer of the development routes and of the
// admin routes (character_commands.h).

namespace soulwarden::backend::server::http::commands {
namespace {

using nlohmann::json;

/** @return The truth value of an argument ("1", "0", "true", "false", "on", "off"), or no value. */
std::optional<bool> truth(const Arguments& arguments, const std::string& name) {
    const auto text = arguments.text(name);
    if (!text) {
        return std::nullopt;
    }
    if (*text == "1" || *text == "true" || *text == "on") {
        return true;
    }
    if (*text == "0" || *text == "false" || *text == "off") {
        return false;
    }
    return std::nullopt;
}

} // namespace

json health_json(world::World& target, const std::string& character) {
    const world::Entity* entity = target.find_character(character);
    json reply{{"inWorld", entity != nullptr},
               {"creaturesIgnore", target.creatures().behavior().ignores(character)},
               {"creatureBehavior", target.options().creatureBehavior},
               {"forceLoot", target.rewards().forced(character)}};
    if (entity != nullptr) {
        reply["health"] = entity->vitals.health;
        reply["healthMax"] = entity->vitals.healthMax;
        reply["dead"] = entity->life.dead;
    }
    return reply;
}

WorldCommand heal(const std::string& character, const Arguments&, service::HttpResponse&) {
    return [character](world::World& target) {
        const world::HealthResult result = target.health().heal(character);
        if (result.error == world::HealthError::notInWorld) {
            // The health of a character is not in the database: each world entry starts with the full health.
            if (!target.store().find_character(character)) {
                return message(404, "no character with this id");
            }
            json reply = health_json(target, character);
            reply["applied"] = "stored";
            reply["message"] = "the character is not in the world: it has its full health at its next world entry";
            return json_reply(200, reply.dump());
        }
        json reply = health_json(target, character);
        reply["applied"] = "world";
        reply["revived"] = result.revived;
        reply["message"] = result.revived ? "the character lives again with its full health" : "the character has its full health";
        return json_reply(200, reply.dump());
    };
}

WorldCommand set_creatures_ignore(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto ignore = truth(arguments, "ignore");
    if (!ignore) {
        refusal = message(400, "give ignore: 1 (the creatures ignore the character) or 0");
        return {};
    }
    return [character, ignore = *ignore](world::World& target) {
        if (target.find_character(character) == nullptr && !target.store().find_character(character)) {
            return message(404, "no character with this id");
        }
        // A value of the character in the world, with the character id as its key: it stays
        // when the session of the character ends, until the backend stops.
        target.creatures().behavior().set_ignored(character, ignore);
        log::write("world: command: the creatures %s character %s", ignore ? "ignore" : "do not ignore", character.c_str());
        json reply = health_json(target, character);
        reply["applied"] = target.find_character(character) != nullptr ? "world" : "stored";
        reply["message"] = ignore ? "the creatures ignore the character" : "the creatures do not ignore the character";
        return json_reply(200, reply.dump());
    };
}

WorldCommand hurt_character(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto amount = arguments.number("amount", 0.0F, 1000000.0F);
    if (!amount) {
        refusal = message(400, "give amount: the damage, a number from 0 to 1000000");
        return {};
    }
    return [character, amount = *amount](world::World& target) {
        const world::HealthResult result = target.health().hurt(character, amount);
        switch (result.error) {
        case world::HealthError::notInWorld:
            return message(404, "the character is not in the world");
        case world::HealthError::dead:
            return message(409, "the character is dead");
        case world::HealthError::badValue:
            return message(400, "the damage is not a number of its range");
        case world::HealthError::alive:
        case world::HealthError::none:
            break;
        }
        json reply = health_json(target, character);
        reply["applied"] = "world";
        reply["message"] = result.died ? "the character is dead" : "the character took the damage";
        return json_reply(200, reply.dump());
    };
}

} // namespace soulwarden::backend::server::http::commands

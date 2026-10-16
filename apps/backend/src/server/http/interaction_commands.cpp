#include <cstdio>
#include <string>

#include "core/logging/log.h"
#include "server/http/character_commands.h"
#include "server/http/router.h"

// The commands of the interaction base (stage 2.5a, world/interactions.h), in the common layer of the development routes and of
// the admin routes (character_commands.h): the list of the objects near a character with their state, the enable of an object
// (all its Interact components), and the end of the committed interaction of a character.

namespace soulwarden::backend::server::http::commands {
namespace {

using nlohmann::json;

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

std::string hex64(std::uint64_t value) {
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
}

std::string hex_bytes(const std::array<std::uint8_t, 16>& bytes) {
    std::string text;
    for (const std::uint8_t byte : bytes) {
        char digits[3];
        std::snprintf(digits, sizeof(digits), "%02x", byte);
        text += digits;
    }
    return text;
}

/** @return The horizontal distance of the nearest bound of an object from a character. */
float distance_of(const gamedata::InteractionObject& object, const world::Entity& character) {
    float best = -1.0F;
    for (const gamedata::InteractionBound& bound : object.bounds) {
        const float distance = std::hypot(bound.position.x - character.position.x, bound.position.y - character.position.y);
        best = best < 0.0F ? distance : std::min(best, distance);
    }
    return best;
}

} // namespace

json interactions_view(world::World& target, const std::string& character, float radius) {
    const world::InteractionsView view = target.interactions().view(character, radius);
    const gamedata::InteractionData* data = target.interactions().data();
    json reply{{"inWorld", view.inWorld},
               {"data", data != nullptr},
               {"radius", radius},
               {"objects", json::array()},
               {"work", {{"cells", view.work.cells}, {"bounds", view.work.bounds}}},
               {"objectEntities", target.interactions().object_entities()},
               {"active", nullptr}};
    if (view.active && data != nullptr) {
        const gamedata::InteractionObject& object = data->objects()[view.active->object];
        reply["active"] = {{"object", object.key},
                           {"kind", gamedata::interaction_kind_text(object.kind)},
                           {"slice", object.slice},
                           {"option", data->options()[view.active->option].name},
                           {"action", view.active->action},
                           {"seconds", std::chrono::duration<double>(target.clock().now() - view.active->started).count()}};
    }
    for (const world::InteractionObjectView& entry : view.objects) {
        const gamedata::InteractionObject& object = *entry.object;
        json bounds = json::array();
        for (std::size_t index = 0; index < object.bounds.size(); ++index) {
            const gamedata::InteractionBound& bound = object.bounds[index];
            json options = json::array();
            for (const gamedata::BoundOption& option : bound.options) {
                options.push_back(data->options()[option.option].name);
            }
            bounds.push_back({{"slot", bound.slot},
                              {"target", hex64(bound.target)},
                              {"position", {bound.position.x, bound.position.y, bound.position.z}},
                              {"range", std::sqrt(bound.rangeSq)},
                              {"capacity", bound.capacity},
                              {"enabled", entry.bounds[index].enabled},
                              {"users", entry.bounds[index].users},
                              {"options", options}});
        }
        reply["objects"].push_back({{"key", object.key},
                                    {"kind", gamedata::interaction_kind_text(object.kind)},
                                    {"slice", object.slice},
                                    {"actor", hex_bytes(object.actor)},
                                    {"distance", entry.distance},
                                    {"entity", entry.entity != 0 ? hex64(entry.entity) : std::string()},
                                    {"bounds", bounds}});
    }
    const world::InteractionCounts& counts = target.interactions().counts();
    json verdicts = json::object();
    for (std::size_t index = 0; index < counts.verdicts.size(); ++index) {
        if (counts.verdicts[index] != 0) {
            verdicts[world::interaction_verdict_text(static_cast<world::InteractionVerdict>(index))] = counts.verdicts[index];
        }
    }
    reply["counts"] = {{"requests", verdicts}, {"begins", counts.begins}, {"ends", counts.ends},
                       {"active", target.interactions().active_count()}};
    return reply;
}

WorldCommand set_object_enabled(const std::string& character, const Arguments& arguments, CreatureReach reach,
                                service::HttpResponse& refusal) {
    const auto key = arguments.text("object");
    const auto enabled = truth(arguments, "enabled");
    if (!key || key->empty() || !enabled) {
        refusal = message(400, "give object (the key of an object of the interaction data) and enabled (1 or 0)");
        return {};
    }
    return [character, key = *key, enabled = *enabled, reach](world::World& target) {
        const gamedata::InteractionData* data = target.interactions().data();
        const gamedata::InteractionObject* object = data != nullptr ? data->find_key(key) : nullptr;
        if (object == nullptr) {
            return message(404, "the interaction data has no object with this key");
        }
        if (reach == CreatureReach::nearCharacter) {
            // The admin interface changes only an object near the character of the command (the list of its state).
            const world::Entity* entity = target.find_character(character);
            if (entity == nullptr || distance_of(*object, *entity) > kAdminInteractionRange) {
                return message(404, "the character is not in the world, or the object is not near it");
            }
        } else if (target.find_character(character) == nullptr && !target.store().find_character(character)) {
            return message(404, "no character with this id");
        }
        if (target.interactions().set_enabled(key, enabled) != world::ObjectError::none) {
            return message(404, "the interaction data has no object with this key");
        }
        json reply{{"applied", "world"}, {"object", object->key}, {"enabled", enabled},
                   {"message", enabled ? "the object is enabled: each session near it gets its state"
                                       : "the object is disabled: its prompt goes away, and a request is refused"}};
        return json_reply(200, reply.dump());
    };
}

WorldCommand cancel_interaction(const std::string& character, const Arguments&, service::HttpResponse&) {
    return [character](world::World& target) {
        if (target.find_character(character) == nullptr) {
            return target.store().find_character(character) ? message(404, "the character is not in the world")
                                                            : message(404, "no character with this id");
        }
        if (!target.interactions().cancel(character)) {
            return message(404, "the character has no committed interaction");
        }
        json reply{{"applied", "world"}, {"message", "the interaction ended: the client of the character got the message 3746"}};
        return json_reply(200, reply.dump());
    };
}

} // namespace soulwarden::backend::server::http::commands

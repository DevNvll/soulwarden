#include <algorithm>
#include <cstdio>
#include <string>

#include "core/logging/log.h"
#include "server/http/character_commands.h"
#include "server/http/router.h"

// The commands of the interaction base (stage 2.5a, world/interactions.h), in the common layer of the development routes and of
// the admin routes (character_commands.h): the list of the objects near a character with their state, the enable of an object
// (all its Interact components), and the end of the committed interaction of a character. Stage 2.5b: the server children in the
// list (the stations, the camps, the fixtures), the station facts and the service grant of a character, and two development aids:
// a fixture near a character and the removal of a server child.

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
               {"active", nullptr},
               {"station", nullptr},
               {"grant", nullptr}};
    if (view.active && data != nullptr && view.activeObject != nullptr) {
        const gamedata::InteractionObject& object = *view.activeObject;
        reply["active"] = {{"object", object.key},
                           {"kind", gamedata::interaction_kind_text(object.kind)},
                           {"slice", object.slice},
                           {"option", data->options()[view.active->option].name},
                           {"action", view.active->action},
                           {"seconds", std::chrono::duration<double>(target.clock().now() - view.active->started).count()},
                           {"entity", view.active->instance != 0 ? hex64(view.active->instance) : std::string()},
                           {"generation", view.active->generation},
                           {"timed", view.active->due.has_value()},
                           {"completed", view.active->completed}};
        if (view.active->due) {
            reply["active"]["secondsLeft"] = std::max(0.0, std::chrono::duration<double>(*view.active->due - target.clock().now()).count());
        }
    }
    if (view.station) {
        const world::StationContext& station = *view.station;
        reply["station"] = {{"object", station.objectKey},
                            {"generation", station.objectGeneration},
                            {"actor", hex_bytes(station.actor)},
                            {"group", hex64(station.group)},
                            {"interactTarget", hex64(station.interactTarget)},
                            {"optionEntity", hex64(station.optionEntity)},
                            {"assemblyKey", station.assemblyKey},
                            {"chargeCraftingFee", station.chargeCraftingFee},
                            {"keyResolved", station.keyResolved},
                            {"assemblyEntity", hex64(station.assemblyEntity)},
                            {"assemblyTarget", hex64(station.assemblyTarget)},
                            {"parent", station.parentKey},
                            {"parentActor", station.parentKey.empty() ? std::string() : hex_bytes(station.parentActor)},
                            {"region", station.region},
                            {"camp", station.isCamp}};
    }
    if (view.grant) {
        reply["grant"] = {{"kind", view.grant->kind}, {"ids", view.grant->ids}, {"source", hex64(view.grant->source.group)},
                          {"revision", view.grant->revision}};
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
        json item{{"key", object.key},
                  {"kind", gamedata::interaction_kind_text(object.kind)},
                  {"slice", object.slice},
                  {"actor", hex_bytes(object.actor)},
                  {"distance", entry.distance},
                  {"entity", entry.entity != 0 ? hex64(entry.entity) : std::string()},
                  {"bounds", bounds}};
        if (object.assembly) {
            item["assembly"] = {{"key", object.assembly->key}, {"fee", object.assembly->fee}, {"resolved", object.assembly->resolved}};
        }
        if (entry.spawned) {
            item["spawned"] = {{"generation", entry.generation}, {"parent", entry.parentKey}, {"fixture", entry.fixture},
                               {"completions", entry.completions}};
        }
        reply["objects"].push_back(item);
    }
    const world::InteractionCounts& counts = target.interactions().counts();
    json verdicts = json::object();
    for (std::size_t index = 0; index < counts.verdicts.size(); ++index) {
        if (counts.verdicts[index] != 0) {
            verdicts[world::interaction_verdict_text(static_cast<world::InteractionVerdict>(index))] = counts.verdicts[index];
        }
    }
    reply["counts"] = {{"requests", verdicts},
                       {"begins", counts.begins},
                       {"ends", counts.ends},
                       {"active", target.interactions().active_count()},
                       {"timed", target.interactions().timed_count()},
                       {"completions", counts.completions},
                       {"serverChildren", target.interactions().instance_count()},
                       {"made", counts.spawned},
                       {"removed", counts.removed},
                       {"spawnScans", counts.scans},
                       {"spawnWork", {{"cells", counts.scanWork.cells}, {"entries", counts.scanWork.bounds}}}};
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
        // A level object of the data (its placement UUID) or a live server child (its key; stage 2.5b).
        const gamedata::InteractionObject* object = target.interactions().object_of_key(key);
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

WorldCommand make_fixture(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto name = arguments.text("name");
    const auto dx = arguments.has("dx") ? arguments.number("dx", -50.0F, 50.0F) : std::optional<float>(0.0F);
    const auto dy = arguments.has("dy") ? arguments.number("dy", -50.0F, 50.0F) : std::optional<float>(3.0F);
    const auto dz = arguments.has("dz") ? arguments.number("dz", -50.0F, 50.0F) : std::optional<float>(0.0F);
    const auto yaw = arguments.has("yaw") ? arguments.number("yaw", -360.0F, 360.0F) : std::optional<float>(0.0F);
    const auto permission = arguments.has("permission") ? arguments.number("permission", 0.0F, 255.0F) : std::optional<float>(-1.0F);
    if (!name || name->empty() || !dx || !dy || !dz || !yaw || !permission) {
        refusal = message(400, "give name (a fixture of the interaction data), and when necessary dx, dy, dz (-50 to 50 m), yaw (degrees), "
                               "permission (0 to 255)");
        return {};
    }
    return [character, name = *name, offset = state::WorldPosition{*dx, *dy, *dz}, yaw = *yaw,
            permission = static_cast<int>(*permission)](world::World& target) {
        const world::FixtureResult result = target.interactions().make_fixture(character, name, offset, yaw, permission);
        switch (result.error) {
        case world::FixtureError::unknownFixture:
            return message(404, "the interaction data has no fixture with this name");
        case world::FixtureError::notInWorld:
            return message(404, "the character is not in the world");
        case world::FixtureError::badPosition:
            return message(400, "the place is not a position of the world");
        case world::FixtureError::none:
            break;
        }
        const world::Entity& entity = *result.entity;
        json reply{{"applied", "world"},
                   {"entity", hex64(entity.id)},
                   {"actor", hex_bytes(entity.actor.bytes)},
                   {"position", {entity.position.x, entity.position.y, entity.position.z}},
                   {"message", "DEVELOPMENT COMMAND: the fixture is a server child near the character"}};
        log::write("world: DEVELOPMENT COMMAND: fixture %s %016llx near character %s", name.c_str(), static_cast<unsigned long long>(entity.id),
                   character.c_str());
        return json_reply(200, reply.dump());
    };
}

WorldCommand remove_object(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    (void)character;
    const auto text = arguments.text("entity");
    std::uint64_t id = 0;
    bool valid = text && text->size() == 16;
    for (const char digit : valid ? *text : std::string()) {
        const int nibble = digit >= '0' && digit <= '9' ? digit - '0' : digit >= 'a' && digit <= 'f' ? digit - 'a' + 10 : -1;
        valid = valid && nibble >= 0;
        id = (id << 4) | static_cast<std::uint64_t>(std::max(nibble, 0));
    }
    if (!valid) {
        refusal = message(400, "give entity: the 16 hexadecimal digits (lower case) of a server child");
        return {};
    }
    return [id](world::World& target) {
        if (!target.interactions().remove_object(id)) {
            return message(404, "no server child with this id");
        }
        json reply{{"applied", "world"}, {"message", "the server child went away: its uses ended (3746), and each observer got the stop"}};
        return json_reply(200, reply.dump());
    };
}

} // namespace soulwarden::backend::server::http::commands

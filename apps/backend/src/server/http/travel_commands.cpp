#include <optional>
#include <string>
#include <utility>

#include "core/logging/log.h"
#include "server/http/character_commands.h"
#include "server/http/router.h"

// The travel command of the common layer (character_commands.h): the development routes
// (tools/world) and the admin routes (the tool window) use the same function. It moves a
// character to a place of the world (World::travel): a travel place of the game data, or a
// position in numbers.

namespace soulwarden::backend::server::http::commands {
namespace {

using nlohmann::json;

/** What a travel command names: a travel place, or a position. */
struct Destination {
    std::string place;
    state::WorldPosition position;
    world::TravelScreen screen{world::TravelScreen::automatic};
};

json place_json(const gamedata::TravelPlace& place) {
    return {{"id", place.id},
            {"kind", place.kind},
            {"name", place.name},
            {"territory", place.territory},
            {"territoryId", place.territoryId},
            {"position", {place.position.x, place.position.y, place.position.z}}};
}

} // namespace

json travel_places_json(const gamedata::TravelPlaces* places) {
    const bool loaded = places != nullptr && places->loaded();
    json reply{{"loaded", loaded}, {"places", json::array()}};
    if (loaded) {
        for (const gamedata::TravelPlace& place : places->places()) {
            reply["places"].push_back(place_json(place));
        }
    }
    return reply;
}

WorldCommand travel(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    Destination destination;
    const bool numbers = arguments.has("x") || arguments.has("y") || arguments.has("z");
    if (arguments.has("place")) {
        const auto place = arguments.text("place");
        if (!place || place->empty() || numbers) {
            refusal = message(400, "give the id of a travel place (place), or a position (x, y, z), not the two");
            return {};
        }
        destination.place = *place;
    } else {
        // A position in meters. The limits are the limits of a place for a character.
        const auto x = arguments.number("x", -world::kMaxHorizontalPosition, world::kMaxHorizontalPosition);
        const auto y = arguments.number("y", -world::kMaxHorizontalPosition, world::kMaxHorizontalPosition);
        const auto z = arguments.number("z", world::kLowestTravelHeight, world::kHighestTravelHeight);
        if (!x || !y || !z) {
            refusal = message(400, "give the id of a travel place (place), or a position in meters (x, y, and z: a height "
                                   "from -99 to 999)");
            return {};
        }
        destination.position = state::WorldPosition{*x, *y, *z};
    }
    if (arguments.has("screen")) {
        // The loading screen of the client. An argument that is there must be correct.
        const auto screen = arguments.text("screen");
        if (screen && *screen == "auto") {
            destination.screen = world::TravelScreen::automatic;
        } else if (screen && *screen == "on") {
            destination.screen = world::TravelScreen::on;
        } else if (screen && *screen == "off") {
            destination.screen = world::TravelScreen::off;
        } else {
            refusal = message(400, "screen must be auto, on, or off");
            return {};
        }
    }
    return [character, destination](world::World& target) mutable {
        json reply{{"message", "done"}};
        if (!destination.place.empty()) {
            const gamedata::TravelPlaces* places = target.options().travelPlaces;
            if (places == nullptr || !places->loaded()) {
                return message(400, "the backend has no travel places: make the file with tools/gamedata/places.sh, or give "
                                    "a position (x, y, z)");
            }
            const gamedata::TravelPlace* place = places->find(destination.place);
            if (place == nullptr) {
                return message(404, "the travel places have no place with this id");
            }
            destination.position = place->position;
            reply["place"] = place_json(*place);
        }
        const state::WorldPosition& position = destination.position;
        reply["position"] = {position.x, position.y, position.z};
        const world::TravelResult result = target.travel(character, position, destination.screen);
        switch (result.error) {
        case world::TravelError::none:
            reply["applied"] = "world";
            reply["distance"] = result.distance;
            return json_reply(200, reply.dump());
        case world::TravelError::badPosition:
            return message(400, "the position is not a place for a character");
        case world::TravelError::storage:
            // The world wrote no row and moved nothing: the character is at its place.
            return message(500, "the position of the character was not written");
        case world::TravelError::notInWorld:
            break;
        }
        // A character that is not in the world: its row gets the place, and its next world
        // entry is there.
        if (!world::travel_position(position)) {
            return message(400, "the position is not a place for a character");
        }
        if (!target.store().find_character(character)) {
            return message(404, "no character with this id");
        }
        if (!target.store().set_character_position(character, position)) {
            return message(500, "the position of the character was not written");
        }
        log::write("world: travel of character %s, which is not in the world: its row has the position %.2f %.2f %.2f",
                   character.c_str(),
                   static_cast<double>(position.x),
                   static_cast<double>(position.y),
                   static_cast<double>(position.z));
        reply["applied"] = "stored";
        return json_reply(200, reply.dump());
    };
}

} // namespace soulwarden::backend::server::http::commands

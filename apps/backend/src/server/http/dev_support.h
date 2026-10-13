#pragma once

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "server/http/character_commands.h"
#include "server/http/routes.h"

// Common parts of the development routes (dev_routes.cpp, dev_progress_routes.cpp): the
// readers of a query parameter, the reply with a message, and the run of a command in the
// world thread. The commands that the admin routes have too are in the common layer
// (character_commands.h): a development route gives them the query of its request.

namespace soulwarden::backend::server::http {

/** Adds the development routes for the level, the attributes, and the skills of a character (/v1/dev/progress). */
void register_dev_progress_routes(Router& router, const Services& services);

namespace dev {

/** @return The text of one query parameter, or no value. The values of these routes need no decoding. */
inline std::optional<std::string> query_text(const service::HttpRequest& request, const std::string& name) {
    return commands::QueryArguments(request).text(name);
}

/** @return A finite number of a query parameter, or no value ("nan" and "inf" are not numbers of a command). */
inline std::optional<float> query_number(const service::HttpRequest& request, const std::string& name) {
    const auto text = query_text(request, name);
    if (!text || text->empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const float value = std::strtof(text->c_str(), &end);
    return end == text->c_str() + text->size() && std::isfinite(value) ? std::optional<float>(value) : std::nullopt;
}

/** @return A whole number of a query parameter in the range `lowest` to `highest`, or no value. */
inline std::optional<std::uint64_t> query_integer(const service::HttpRequest& request,
                                           const std::string& name,
                                           std::uint64_t lowest,
                                           std::uint64_t highest) {
    return commands::QueryArguments(request).integer(name, lowest, highest);
}

inline service::HttpResponse message(int status, const std::string& text) {
    return commands::message(status, text);
}

/**
 * Runs a command in the world thread. The reply 503 when the world did not start it: the
 * command is then cancelled, and nothing changed. The reply 500 when the command started and
 * stopped with an error.
 */
inline service::HttpResponse in_world(const Services& services, std::function<service::HttpResponse(world::World&)> command) {
    return commands::in_world(services, std::move(command));
}

/**
 * Checks the request of a command for a character: a development route is a management
 * route (a request with a service host gets 403), and it names a character.
 * @return The character id, or no value with the reply in `refusal`.
 */
inline std::optional<std::string> character_of(const RouteContext& context, service::HttpResponse& refusal) {
    if (!context.request.host.empty()) {
        refusal = message(403, "forbidden");
        return std::nullopt;
    }
    const auto character = query_text(context.request, "character");
    if (!character || character->empty()) {
        refusal = message(400, "give the character id: character=ID");
        return std::nullopt;
    }
    return character;
}

/** @return The handler of a development route for a command of the common layer, with the query as its arguments. */
inline RouteHandler command_route(const Services& services, commands::Command command) {
    return [&services, command](const RouteContext& context) {
        service::HttpResponse refusal;
        const auto character = character_of(context, refusal);
        if (!character) {
            return refusal;
        }
        commands::WorldCommand step = command(*character, commands::QueryArguments(context.request), refusal);
        return step ? in_world(services, std::move(step)) : refusal;
    };
}

} // namespace dev

} // namespace soulwarden::backend::server::http

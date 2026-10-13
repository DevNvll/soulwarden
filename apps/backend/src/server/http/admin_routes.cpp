#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/logging/log.h"
#include "server/http/admin_access.h"
#include "server/http/character_commands.h"
#include "server/http/routes.h"

// The admin interface: the routes of the tool window of the game hook. Each start of the
// backend has them when the launcher gave it a token (admin_access.h): they are a function for
// the user, not diagnostics, and they need no harness gate.
//
//   GET  /admin/v1/state[?character=ID]
//        The characters of the account of the token, and the state of one of them: the level,
//        the experience, the attributes, the coin, the bag, the equipment, the weapon
//        masteries, and the trade skills. With no id: the character of the account that a
//        session plays, or that is in the world, or the first one.
//   POST /admin/v1/commands
//        One command for one character: {"command": NAME, "character": ID, "args": {...}}.
//        NAME and the members of "args":
//          give_item           item (the ItemID text), quantity, gearScore
//          remove_item         slot (a slot of the bag), quantity
//          set_coin            amount (hundredths of a coin)
//          set_level           level (a level of the screen)
//          add_experience      amount
//          set_attribute       attribute (constitution, focus, strength, dexterity,
//                              intelligence), value
//          set_weapon_mastery  weapon ("SwordAbilityTable" or "sword"), level (1 to 20: each
//                              level above 1 gives one mastery point), experience
//          set_trade_skill     skill ("Mining"), level, experience
//          spawn_creature      kind (an id of the creature data), level, dx, dy, dz, yaw: a
//                              creature near the character (3 m to the north with no place)
//          remove_creature     creature (the id of a creature: 16 hexadecimal digits)
//          set_creature_health creature, and health or percent
//          kill_creature       creature
//          reset_creatures     no argument: each spawn point that waits is free again
//        The reply is the reply of the command of the common layer (character_commands.h):
//        200 with "applied":"world" or "applied":"stored", or 400 or 404 with a message.
//   GET  /admin/v1/catalog/items?q=TEXT[&limit=N][&offset=N]
//        The items of the item catalog whose id, type, or class has each word of the text.
//   GET  /admin/v1/catalog/creatures?q=TEXT[&limit=N][&offset=N]
//        The kinds of the creature data whose id, name, category, or family has each word.
// The state of a character that is in the world also has the creatures near it (the member
// "creatures": each creature that is 150 m or less from the character, the nearest one first).
//
// Rules of each route:
// - The request has the token of this start in the header X-Soulwarden-Admin. With no token or
//   with a wrong token the reply is 401, with no data, and nothing changes.
// - A request with a service host (a request of the game, which the hook redirects) or with
//   an Origin header (a page of a browser) gets the reply 403.
// - The token is for one account. A command names a character id (roadmap, rule MP-9), and
//   the backend finds the account from the token, not from the request (rule MP-5): a
//   character of a different account gives the reply 404, as a character that is not there.
// - A command runs as a task of the world thread and calls the same function of the world as
//   the development route for it.

namespace soulwarden::backend::server::http {
namespace {

using nlohmann::json;
using commands::in_world;
using commands::message;

constexpr const char* kNoCharacter = "the account of this start has no character with this id";
constexpr std::size_t kSearchLimit = 200;
constexpr std::size_t kSearchDefault = 50;
/** Characters of a character id that a log line shows (the same as the mark of a session). */
constexpr std::size_t kLogIdLength = 8;

struct NamedCommand {
    const char* name;
    commands::Command command;
};

constexpr NamedCommand kCommands[] = {
    {"give_item", commands::give_item},
    {"remove_item", commands::remove_item},
    {"set_coin", commands::set_coin},
    {"set_level", commands::set_level},
    {"add_experience", commands::add_experience},
    {"set_attribute", commands::set_attribute},
    {"set_weapon_mastery", commands::set_weapon_mastery},
    {"set_trade_skill", commands::set_trade_skill},
    // The creature commands: the character of the command is the reference place.
    {"spawn_creature", commands::spawn_creature},
    {"remove_creature", commands::remove_creature},
    {"set_creature_health", commands::set_creature_health},
    {"kill_creature", commands::kill_creature},
    {"reset_creatures", commands::reset_creatures},
};

/** The state of the tool window has the creatures that are this near to the selected character (meters). */
constexpr float kCreatureListRange = 150.0F;

/** @return The text of a query value: "%XX" is one byte, and "+" is a space. */
std::string url_decode(const std::string& text) {
    const auto digit = [](char character) -> int {
        if (character >= '0' && character <= '9') return character - '0';
        if (character >= 'a' && character <= 'f') return character - 'a' + 10;
        if (character >= 'A' && character <= 'F') return character - 'A' + 10;
        return -1;
    };
    std::string decoded;
    decoded.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '+') {
            decoded += ' ';
        } else if (text[index] == '%' && index + 2 < text.size() && digit(text[index + 1]) >= 0 && digit(text[index + 2]) >= 0) {
            decoded += static_cast<char>(digit(text[index + 1]) * 16 + digit(text[index + 2]));
            index += 2;
        } else {
            decoded += text[index];
        }
    }
    return decoded;
}

/**
 * Checks who sends a request.
 * @return The Steam id of the account of its token, or no value with the reply in `refusal`.
 */
std::optional<std::string> caller(const AdminAccess& access, const RouteContext& context, service::HttpResponse& refusal) {
    const service::HttpRequest& request = context.request;
    if (!request.host.empty() || !request.header("Origin").empty()) {
        refusal = message(403, "forbidden");
        return std::nullopt;
    }
    auto steamId = access.steam_id(request);
    if (!steamId) {
        refusal = message(401, "no access");
    }
    return steamId;
}

/** @return True when the character is a character of the account of a Steam id. World thread. */
bool owns(world::World& target, const std::string& steamId, const std::string& character) {
    const auto account = target.store().find_account_of_steam_id(steamId);
    const auto row = account ? target.store().find_character(character) : std::nullopt;
    return row && row->personaId == account->personaId;
}

service::HttpResponse show_state(const Services& services, const AdminAccess& access, const RouteContext& context) {
    service::HttpResponse refusal;
    const auto steamId = caller(access, context, refusal);
    if (!steamId) {
        return refusal;
    }
    const std::string wanted = url_decode(commands::QueryArguments(context.request).text("character").value_or(""));
    return in_world(services, [steamId = *steamId, wanted](world::World& target) {
        const gamedata::ItemCatalog* catalog = target.options().itemCatalog;
        json reply{{"account", false},
                   {"characters", json::array()},
                   {"catalog", catalog != nullptr && catalog->loaded()},
                   {"revision", target.revision()}};
        const auto account = target.store().find_account_of_steam_id(steamId);
        if (!account) {
            // No login of this start made the account yet.
            return wanted.empty() ? json_reply(200, reply.dump()) : message(404, kNoCharacter);
        }
        reply["account"] = true;
        // With no id in the request: the character that a session plays, then one that is in
        // the world, then the first one of the account.
        std::string selected;
        int best = 0;
        for (const state::Character& character : target.store().list_characters(account->personaId)) {
            const world::Entity* entity = target.find_character(character.characterId);
            const auto progress = target.progress().view(character.characterId);
            reply["characters"].push_back({{"id", character.characterId},
                                           {"name", character.name},
                                           {"level", progress ? progress->progression.level : 1U},
                                           {"inWorld", entity != nullptr},
                                           {"session", entity != nullptr ? entity->owner : 0U}});
            const int score = entity == nullptr ? 1 : entity->owner != 0 ? 3 : 2;
            if (wanted.empty() ? score > best : character.characterId == wanted) {
                selected = character.characterId;
                best = score;
            }
        }
        if (!wanted.empty() && selected != wanted) {
            return message(404, kNoCharacter);
        }
        if (!selected.empty()) {
            const auto items = commands::items_view(target, selected);
            const auto progress = commands::progress_view(target, selected);
            if (items && progress) {
                reply["selected"] = selected;
                reply["items"] = *items;
                reply["progress"] = *progress;
                // The creatures near the character. The character of a different account never
                // gets here: the list above has only the characters of the account of the token.
                reply["creatures"] = commands::creatures_view(target, selected, kCreatureListRange);
            }
        }
        return json_reply(200, reply.dump());
    });
}

service::HttpResponse run_command(const Services& services, const AdminAccess& access, const RouteContext& context) {
    service::HttpResponse refusal;
    const auto steamId = caller(access, context, refusal);
    if (!steamId) {
        return refusal;
    }
    const json body = json::parse(context.request.body, nullptr, false);
    if (!body.is_object() || !body.contains("command") || !body["command"].is_string() || !body.contains("character")
        || !body["character"].is_string() || body["character"].get<std::string>().empty()
        || (body.contains("args") && !body["args"].is_object())) {
        return message(400, R"(the body must be {"command": NAME, "character": ID, "args": {...}})");
    }
    const std::string name = body["command"].get<std::string>();
    const std::string character = body["character"].get<std::string>();
    const auto found = std::find_if(std::begin(kCommands), std::end(kCommands), [&name](const NamedCommand& entry) {
        return name == entry.name;
    });
    if (found == std::end(kCommands)) {
        return message(400, "the admin interface has no command with this name");
    }
    static const json kNoArguments = json::object();
    commands::WorldCommand step =
        found->command(character, commands::JsonArguments(body.contains("args") ? body["args"] : kNoArguments), refusal);
    if (!step) {
        return refusal;
    }
    return in_world(services, [steamId = *steamId, character, name, step = std::move(step)](world::World& target) {
        // The account comes from the token: a request cannot name an account.
        if (!owns(target, steamId, character)) {
            log::write("admin: command %s refused: the character is not a character of the account of this start", name.c_str());
            return message(404, kNoCharacter);
        }
        service::HttpResponse reply = step(target);
        log::write("admin: command %s for character %s: reply %d",
                   name.c_str(),
                   character.substr(0, kLogIdLength).c_str(),
                   reply.status);
        return reply;
    });
}

service::HttpResponse search_items(const Services& services, const AdminAccess& access, const RouteContext& context) {
    service::HttpResponse refusal;
    if (!caller(access, context, refusal)) {
        return refusal;
    }
    const commands::QueryArguments arguments(context.request);
    std::size_t limit = kSearchDefault;
    std::size_t offset = 0;
    if (arguments.text("limit")) {
        const auto value = arguments.integer("limit", 1, kSearchLimit);
        if (!value) {
            return message(400, "limit must be a number from 1 to 200");
        }
        limit = static_cast<std::size_t>(*value);
    }
    if (arguments.text("offset")) {
        const auto value = arguments.integer("offset", 0, 10000000);
        if (!value) {
            return message(400, "offset must be a number from 0 to 10000000");
        }
        offset = static_cast<std::size_t>(*value);
    }
    // The catalog does not change after the start of the backend: a read needs no world task.
    const gamedata::ItemCatalog* catalog = services.world.options().itemCatalog;
    json reply{{"loaded", catalog != nullptr && catalog->loaded()}, {"total", 0}, {"offset", offset}, {"items", json::array()}};
    if (catalog != nullptr && catalog->loaded()) {
        std::size_t total = 0;
        for (const gamedata::ItemDefinition* item :
             catalog->search(url_decode(arguments.text("q").value_or("")), offset, limit, &total)) {
            reply["items"].push_back({{"id", item->id},
                                      {"type", item->type},
                                      {"class", item->itemClass},
                                      {"tier", item->tier},
                                      {"maxStack", item->maxStack},
                                      {"minGearScore", item->minGearScore},
                                      {"maxGearScore", item->maxGearScore}});
        }
        reply["total"] = total;
    }
    return json_reply(200, reply.dump());
}

service::HttpResponse search_creatures(const Services& services, const AdminAccess& access, const RouteContext& context) {
    service::HttpResponse refusal;
    if (!caller(access, context, refusal)) {
        return refusal;
    }
    const commands::QueryArguments arguments(context.request);
    std::size_t limit = kSearchDefault;
    std::size_t offset = 0;
    if (arguments.text("limit")) {
        const auto value = arguments.integer("limit", 1, kSearchLimit);
        if (!value) {
            return message(400, "limit must be a number from 1 to 200");
        }
        limit = static_cast<std::size_t>(*value);
    }
    if (arguments.text("offset")) {
        const auto value = arguments.integer("offset", 0, 10000000);
        if (!value) {
            return message(400, "offset must be a number from 0 to 10000000");
        }
        offset = static_cast<std::size_t>(*value);
    }
    // The creature data does not change after the start of the backend: a read needs no world task.
    const gamedata::CreatureData* data = services.world.options().creatureData;
    const bool loaded = data != nullptr && data->loaded();
    json reply{{"loaded", loaded}, {"total", 0}, {"offset", offset}, {"kinds", json::array()}};
    if (loaded) {
        std::size_t total = 0;
        for (const gamedata::CreatureKind* kind :
             data->search(url_decode(arguments.text("q").value_or("")), offset, limit, &total)) {
            reply["kinds"].push_back(commands::creature_kind_json(*data, *kind));
        }
        reply["total"] = total;
    }
    return json_reply(200, reply.dump());
}

} // namespace

void register_admin_routes(Router& router, const Services& services, const AdminAccess& access) {
    const auto bind = [&services, &access](service::HttpResponse (*handler)(const Services&, const AdminAccess&, const RouteContext&)) {
        return [&services, &access, handler](const RouteContext& context) { return handler(services, access, context); };
    };
    router.add("GET", "", "/admin/v1/state", kAdminStateRoute, bind(show_state));
    router.add("POST", "", "/admin/v1/commands", "admin.commands", bind(run_command));
    router.add("GET", "", "/admin/v1/catalog/items", kAdminCatalogRoute, bind(search_items));
    router.add("GET", "", "/admin/v1/catalog/creatures", kAdminCreatureCatalogRoute, bind(search_creatures));
}

} // namespace soulwarden::backend::server::http

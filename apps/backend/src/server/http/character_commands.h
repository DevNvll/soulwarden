#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "server/http/services.h"
#include "service/http_server.h"

// The commands that change a character, and the views that read one. They are the one layer
// below the two route sets of the backend:
// - the development routes (dev_routes.cpp, dev_progress_routes.cpp), which a development run
//   has for tools/world, and
// - the admin routes (admin_routes.cpp), which each start has for the tool window of the
//   game hook.
// A route reads the character id and the arguments of its request, checks who sends it, and
// calls a function of this file. Each function names a character by its id (roadmap, rule
// MP-9) and calls the systems of the world (world/inventory.h, world/progress.h,
// world/skills.h): a route has no logic of its own for a character.
//
// A command has two steps. The first one reads and checks the arguments in the thread of the
// route. The second one is the function that it returns: it runs in the world thread.

namespace soulwarden::backend::server::http::commands {

/**
 * The arguments of one command, by name: the query of a development route, or the member
 * "args" of an admin command.
 */
class Arguments {
public:
    /**
     * @return The text of an argument. No value when the request does not have the argument,
     *         and when the argument has a form that is not a value (a JSON array, object, or
     *         null).
     */
    [[nodiscard]] virtual std::optional<std::string> text(const std::string& name) const = 0;

    /**
     * @return True when the request has the argument, in any form. A command uses it for an
     *         argument that is not necessary: an argument that is there must be correct, and
     *         one with a wrong form is not the same as no argument.
     */
    [[nodiscard]] virtual bool has(const std::string& name) const = 0;

    /** @return A whole number in the range `lowest` to `highest`, or no value. */
    [[nodiscard]] std::optional<std::uint64_t> integer(const std::string& name,
                                                       std::uint64_t lowest,
                                                       std::uint64_t highest) const;

    /** @return A finite number in the range `lowest` to `highest` ("nan" and "inf" are not numbers), or no value. */
    [[nodiscard]] std::optional<float> number(const std::string& name, float lowest, float highest) const;

protected:
    ~Arguments() = default;
};

/** The arguments of a query text ("a=1&b=2"). The values of the development routes need no decoding. */
class QueryArguments final : public Arguments {
public:
    explicit QueryArguments(const service::HttpRequest& request) : m_request(request) {}
    [[nodiscard]] std::optional<std::string> text(const std::string& name) const override;
    [[nodiscard]] bool has(const std::string& name) const override {
        return text(name).has_value();
    }

private:
    const service::HttpRequest& m_request;
};

/** The arguments of a JSON object. A member can be a text, a whole number, or a truth value. */
class JsonArguments final : public Arguments {
public:
    explicit JsonArguments(const nlohmann::json& object) : m_object(object) {}
    [[nodiscard]] std::optional<std::string> text(const std::string& name) const override;
    [[nodiscard]] bool has(const std::string& name) const override {
        return m_object.is_object() && m_object.contains(name);
    }

private:
    const nlohmann::json& m_object;
};

/** The second step of a command: it runs in the world thread and gives the reply. */
using WorldCommand = std::function<service::HttpResponse(world::World&)>;

/**
 * The first step of a command.
 * @param character The id of the character.
 * @param refusal The reply when the arguments are not correct.
 * @return The second step, or an empty function with the reply in `refusal`.
 */
using Command = WorldCommand (*)(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);

// The item commands (world/inventory.h). Arguments: give `item`, `quantity`, `gearScore`,
// `action`; remove `slot`, `quantity`; coin `amount` (hundredths of a coin).
[[nodiscard]] WorldCommand give_item(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
[[nodiscard]] WorldCommand remove_item(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
[[nodiscard]] WorldCommand set_coin(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);

// The progress commands (world/progress.h). Arguments: level `level` (a level of the screen);
// experience `amount`; attribute `attribute` (constitution, focus, strength, dexterity,
// intelligence, or 0 to 4) and `value`.
[[nodiscard]] WorldCommand set_level(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
[[nodiscard]] WorldCommand add_experience(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
[[nodiscard]] WorldCommand set_attribute(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);

// The skill commands (world/skills.h). Arguments: mastery `weapon` (the id of the game table,
// "SwordAbilityTable", or its first part, "sword"), `level` (1 to 20: each level above 1 gives
// one mastery point), `experience`; trade skill `skill`, `level`, `experience`.
[[nodiscard]] WorldCommand set_weapon_mastery(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
[[nodiscard]] WorldCommand set_trade_skill(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
/** Arguments: `id` (a weapon or a trade skill) and `amount`. */
[[nodiscard]] WorldCommand add_skill_experience(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);

// The creature commands (world/creatures.h, server/http/creature_commands.cpp). The character
// of a creature command is the reference place: the character of the user who gives it.
// spawn: `kind` (an id of the creature data), and when necessary `level`, `dx`, `dy`, `dz`
// (the place of the creature from the character, in meters: east, north, up; with none of
// them: 3 m to the north), and `yaw` (degrees). The character must be in the world.
// remove, kill: `creature` (the id of the creature: 16 hexadecimal digits).
// health: `creature`, and `health` (a number from 0 to the maximum) or `percent` (0 to 100).
// reset: no argument. Each spawn point that waits is free again.
[[nodiscard]] WorldCommand spawn_creature(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
[[nodiscard]] WorldCommand remove_creature(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
[[nodiscard]] WorldCommand set_creature_health(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
[[nodiscard]] WorldCommand kill_creature(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
[[nodiscard]] WorldCommand reset_creatures(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);

/**
 * @return The creatures of the world (world thread), the nearest one first: the id, the kind,
 *         the name, the level, the health, the position, and the distance from a character.
 * @param character The reference place. Empty, or a character that is not in the world: each
 *        creature, with no distance.
 * @param range With a value: only the creatures that are this near to the character (meters).
 */
[[nodiscard]] nlohmann::json creatures_view(world::World& world, const std::string& character, std::optional<float> range);

/** @return The JSON form of one kind of the creature data, with its health at its level. */
[[nodiscard]] nlohmann::json creature_kind_json(const gamedata::CreatureData& data, const gamedata::CreatureKind& kind);

/** @return The bag, the equipment, and the coin of a character (world thread), or no value when the store does not have it. */
[[nodiscard]] std::optional<nlohmann::json> items_view(world::World& world, const std::string& character);

/** @return The level, the attributes, the weapon masteries, and the trade skills of a character (world thread), or no value. */
[[nodiscard]] std::optional<nlohmann::json> progress_view(world::World& world, const std::string& character);

/** @return The reply of an item command, of a progress command, and of a skill command. */
[[nodiscard]] service::HttpResponse item_reply(const world::ItemResult& result);
[[nodiscard]] service::HttpResponse progress_reply(const world::ProgressResult& result);
[[nodiscard]] service::HttpResponse skill_reply(const world::SkillResult& result);

/** @return The JSON form of one item of a slot. */
[[nodiscard]] nlohmann::json item_json(const world::ItemInstance& item, std::uint32_t slot);

/** @return A reply with one message. */
[[nodiscard]] service::HttpResponse message(int status, const std::string& text);

/**
 * Runs the second step of a command in the world thread. The reply 503 when the world did not
 * start it: the command is then cancelled, and nothing changed. The reply 500 when the command
 * started and stopped with an error.
 */
[[nodiscard]] service::HttpResponse in_world(const Services& services, WorldCommand command);

} // namespace soulwarden::backend::server::http::commands

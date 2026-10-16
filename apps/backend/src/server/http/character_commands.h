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
// `action`, `perks` (stage 1.3b: at most five PerkID texts with "," between them, one for each
// place; an empty part is a place with no perk; a gem is a perk of one place); remove `slot`,
// `quantity`; coin `amount` (hundredths of a coin).
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
// The cooldown commands (stage 3.3a, world/abilities.h). reset_cooldowns: no argument. Each
// cooldown of the abilities of the character ends; its client gets the empty maps.
[[nodiscard]] WorldCommand reset_cooldowns(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
/**
 * @return The abilities of a character (world thread): "cooldowns" (each ability id with its
 *         remaining seconds), and the counts of the abilities of the world ("presses" by cause,
 *         the hits, the effects).
 */
[[nodiscard]] nlohmann::json abilities_json(world::World& world, const std::string& character);
// The spell commands (stage 3.3b-S2a, world/spells.h). cast_ability: `place` (0 to 2: Q, R, F) and `yaw` (degrees from
// north to the east, the direction of the throw; default 0): the press of a key of the bar for a character, with the
// checks and the cast of a press of a client (a remote test character with no client: the server throws a projectile
// itself). end_spells: no argument: each spell entity of the character ends (each observer gets the stop).
[[nodiscard]] WorldCommand cast_ability(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
[[nodiscard]] WorldCommand end_spells(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
/** @return The spell entities of the world (world thread): each one with its spell, its caster, its age, its end, and the counts. */
[[nodiscard]] nlohmann::json spells_json(world::World& world);
// The projectile commands (stage 3.3b-S3, world/projectiles.h). shoot: `yaw` (degrees from north to the east; default 0),
// `pitch` (degrees, positive up, -89 to 89; default 0), and `heavy` (0 or 1; default 0): a shot of the server for a character
// with no client (a remote test character), with the weapon in its hand, the rules, the cost, the flight, and the hit of a
// shot of a client.
[[nodiscard]] WorldCommand shoot(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
/** @return The projectiles in the air (world thread), the counts of the shots by verdict, of the ends by cause, and of the work. */
[[nodiscard]] nlohmann::json projectiles_json(world::World& world);
/** @return The counts of the sequences of the basic attacks (stage 3.3b-M1) for the list of the world. */
[[nodiscard]] nlohmann::json basic_attacks_json(world::World& world);
// The tree and the bar commands (stage 3.3a, world/skills.h): the same rules as the requests of
// a client. assign_node: `weapon` (as for the mastery) and `node` (the id text of the node of the
// game table, "Ability_WarHammer_WreckingBall"): one mastery point on the node.
// set_ability_bar: `weapon`, `slot` (0 to 2), and `ability` (the id text of an active node that
// the character has, or "none": an empty place).
[[nodiscard]] WorldCommand assign_node(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
[[nodiscard]] WorldCommand set_ability_bar(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);

// The travel command (World::travel, server/http/travel_commands.cpp). Arguments: `place` (the
// id of a travel place of the game data), or `x`, `y`, and `z` (a position in meters; the
// height is a height on the ground, from -99 to 999), and when necessary `screen` ("auto",
// "on", or "off": the loading screen of the client; "auto" is a loading screen for a place
// that is far). A character that is in the world goes to the place at once, with no new
// login ("applied":"world"). For a character that is not in the world, its row gets the place,
// and its next world entry is there ("applied":"stored").
[[nodiscard]] WorldCommand travel(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);

// The health commands (world/health.h, server/http/health_commands.cpp).
// heal: no argument. The character gets its full health, and a dead character lives again at
// its place. For a character that is not in the world nothing changes ("applied":"stored"):
// each world entry starts with the full health.
// set_creatures_ignore: `ignore` (1 or 0). The creatures ignore the character: they do not get
// it as a target, and a creature that has it as its target loses it. A value of the character
// in the world, with the character id as its key, until the backend stops.
// hurt_character: `amount` (the damage). A development command: only the development routes
// have it.
[[nodiscard]] WorldCommand heal(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
[[nodiscard]] WorldCommand set_creatures_ignore(const std::string& character, const Arguments& arguments,
                                                service::HttpResponse& refusal);
[[nodiscard]] WorldCommand hurt_character(const std::string& character, const Arguments& arguments,
                                          service::HttpResponse& refusal);

// The itemization commands (stage 1.3b, server/http/gear_commands.cpp).
// use_item: `slot`, and when necessary `container` ("bag", or "paperdoll" for a quick slot 14 to
// 19): the use of a consumable through the rule of a client request (world/consumables.h).
// set_rune_charge: `percent` (0 to 100): the heart rune charge (world/runes.h). activate_rune: no
// argument: the key of the heart rune. roll_item_perks: `item`, and when necessary `gs`, `level`,
// `count`: the perk roll of a loot item (world/perk_roll.h), with no item; the reply has the
// count of each perk. Only the development routes have roll_item_perks.
// set_item_perks: `slot`, `perks` (the form of give_item), and when necessary `container`: the perks of an item that the
// character has, with the checks and the swap of a give (Inventory::set_perks).
[[nodiscard]] WorldCommand set_item_perks(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
[[nodiscard]] WorldCommand use_item(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
[[nodiscard]] WorldCommand set_rune_charge(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
[[nodiscard]] WorldCommand activate_rune(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
[[nodiscard]] WorldCommand roll_item_perks(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
// The status commands (stage 3.3b-S1, server/http/status_commands.cpp; world/statuses.h).
// apply_status: `status` (the id of a status row), and when necessary `seconds` (0: the duration of the row), `potency`,
// `count` (1 to 25 applications: the stacks): the status on the character, with the rules of the status system.
// remove_status: `status` (the id of a status row, or "all"): the status ends (not a status of the gear).
[[nodiscard]] WorldCommand apply_status(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
[[nodiscard]] WorldCommand remove_status(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
/**
 * @return The statuses of a character (world thread): each status (index, seconds, stacks, potency, source, tray, public,
 *         wire), the speed and armor parts, the attribute bonus, and the counts of the status system.
 */
[[nodiscard]] nlohmann::json status_json(world::World& world, const std::string& character);
/**
 * @return The gear of a character (world thread): the perks with an effect (slot, item, perk, the
 *         perk before the swap, potency), the attribute bonus, the parts of the health, the
 *         damage, the armor, and the absorption, the armor, the equip load and its class, the
 *         effects, the item cooldowns, the rune charge, the health and the mana, and the counts.
 */
[[nodiscard]] nlohmann::json gear_json(world::World& world, const std::string& character);
/**
 * @return The passive rules of a character (world thread; stage 3.3b-S5, world/passives.h): each learned node of its weapon trees
 *         and each ability row of its perks with its kinds, whether the backend applies it, and what it does or why it has no
 *         effect; the critical values of the weapon in the hand; the counts of the evaluator.
 */
[[nodiscard]] nlohmann::json passives_json(world::World& world, const std::string& character);
/** @return Each node of a weapon tree (the id text of its ability table: "GreatAxeAbilityTable") with what the backend does with it. */
[[nodiscard]] nlohmann::json passives_table_json(world::World& world, const std::string& table);

/**
 * @return The heal values of a character (stage 3.3b-S6, heal_commands.cpp): the heal base with the weapon in its hand (its gear score,
 *         its ScalingFocus, the Focus of the character), the outgoing factor to a target (the character itself when `target` is empty),
 *         the received factors of the character for each kind, the friends of the test fixture, and the counts of the heals of the world.
 */
[[nodiscard]] nlohmann::json heals_json(world::World& world, const std::string& character, const std::string& target);

/**
 * Development aid (stage 3.3b-S6): makes two characters in the world friends (the test fixture of World::set_test_friends) for a live
 * test of a heal of a friend. @return The reply: "friends", or the cause of a refusal.
 */
[[nodiscard]] nlohmann::json make_friends(world::World& world, const std::string& character, const std::string& other);

// The loot commands (world/rewards.h, server/http/loot_commands.cpp).
// set_force_loot: `force` (1 or 0). A development switch of a character (the debug message
// DebugSetForceDropAILoot of the client): the outer loot chance and the coin chance of its kills
// pass. A value of the world with the character id as its key, until the backend stops.
// roll_loot: `kind` (a kind of the creature data), and when necessary `level` (the creature level;
// the level of the kind), `count` (1 to 10000 rolls), `forced` (1: the outer chance passes). A
// development command: it rolls the loot table of the kind for the level of the character and
// gives nothing; the reply has the count of each item. Only the development routes have it.
[[nodiscard]] WorldCommand set_force_loot(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
[[nodiscard]] WorldCommand roll_loot(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
/**
 * @return The health of a character (world thread): "inWorld", "creaturesIgnore",
 *         "creatureBehavior" (the switch of the backend), "forceLoot" (the loot switch), and for a
 *         character that is in the world "health", "healthMax", and "dead".
 */
[[nodiscard]] nlohmann::json health_json(world::World& world, const std::string& character);

/** @return The travel places of the backend: "loaded", and the list "places" (id, kind, name, territory, position). */
[[nodiscard]] nlohmann::json travel_places_json(const gamedata::TravelPlaces* places);

// The creature commands (world/creatures.h, server/http/creature_commands.cpp). The character
// of a creature command is the reference place: the character of the user who gives it.

/**
 * The creatures of a character for the admin interface: each creature that is this near to the
 * character (meters, the horizontal distance). The state of a character has the list of these
 * creatures, and a creature command of the admin interface changes only a creature of that list.
 */
inline constexpr float kAdminCreatureRange = 150.0F;

/**
 * Which creatures a creature command can change (remove, health, kill). Each route set gives
 * its rule to the command:
 * - `world`: the development routes (tools/world). Each creature of the world, and the command
 *   needs no character.
 * - `nearCharacter`: the admin routes (the tool window). The character of the command must be
 *   in the world, and the creature must be `kAdminCreatureRange` or less from it. The reply
 *   is 404 for a character that is not in the world, and 404 for a creature that is farther
 *   (the same reply as for an id of no creature), and nothing changes.
 * The rule makes no difference for spawn (the character must be in the world for each rule)
 * and for reset (it names no creature).
 */
enum class CreatureReach { world, nearCharacter };

/** The first step of a creature command: a command (see `Command`) with the rule of its route set. */
using CreatureCommand = WorldCommand (*)(const std::string& character, const Arguments& arguments, CreatureReach reach,
                                         service::HttpResponse& refusal);

// spawn: `kind` (an id of the creature data), and when necessary `level`, `dx`, `dy`, `dz`
// (the place of the creature from the character, in meters: east, north, up; with none of
// them: 3 m to the north), and `yaw` (degrees). The character must be in the world.
// remove, kill: `creature` (the id of the creature: 16 hexadecimal digits).
// health: `creature`, and `health` (a number from 0 to the maximum) or `percent` (0 to 100:
// 100 is the exact maximum).
// reset: no argument. Each spawn point that waits is free again.
[[nodiscard]] WorldCommand spawn_creature(const std::string& character, const Arguments& arguments, CreatureReach reach,
                                          service::HttpResponse& refusal);
[[nodiscard]] WorldCommand remove_creature(const std::string& character, const Arguments& arguments, CreatureReach reach,
                                           service::HttpResponse& refusal);
[[nodiscard]] WorldCommand set_creature_health(const std::string& character, const Arguments& arguments, CreatureReach reach,
                                               service::HttpResponse& refusal);
[[nodiscard]] WorldCommand kill_creature(const std::string& character, const Arguments& arguments, CreatureReach reach,
                                         service::HttpResponse& refusal);
[[nodiscard]] WorldCommand reset_creatures(const std::string& character, const Arguments& arguments, CreatureReach reach,
                                           service::HttpResponse& refusal);
/**
 * action: `creature`, `action` (the name of an action of the action table of the slice of the
 * creature: "Spawn", "Attack_Bite"), and when necessary `seconds` (the time of an action that
 * has an end). A development command: the creature plays the action in each client that
 * observes it. Only the development routes have it.
 */
[[nodiscard]] WorldCommand play_creature_action(const std::string& character, const Arguments& arguments, CreatureReach reach,
                                                service::HttpResponse& refusal);
/**
 * anchor: `creature`, and `x`, `y`, `z` (meters). A development command: the anchor of a
 * creature in a fight (the end of its way back, and the first place of its trail) is that
 * place. A test of the way back with no path to the anchor. Only the development routes have it.
 */
[[nodiscard]] WorldCommand move_creature_anchor(const std::string& character, const Arguments& arguments, CreatureReach reach,
                                                service::HttpResponse& refusal);

/**
 * effect: `creature`, `status` (the id text of a status row of the ability data,
 * "WarHammer_Shockwave_Stun", or "knockdown" or "stagger"), and when necessary `seconds` (the time
 * of the effect; no value: the time of the data, 2 s for a knockdown and 1 s for a stagger). A
 * development command (stage 3.3a): the creature takes the effect through the creature system, as
 * from an ability. Only the development routes have it.
 */
[[nodiscard]] WorldCommand give_creature_effect(const std::string& character, const Arguments& arguments, CreatureReach reach,
                                                service::HttpResponse& refusal);

/**
 * respawn: `creature` (a dead creature of a spawn point) and `seconds`. A development command: the
 * spawn point makes its creature again in this time (from now), when a player is near it, and its
 * body leaves the world first. Only the development routes have it.
 */
[[nodiscard]] WorldCommand set_respawn_time(const std::string& character, const Arguments& arguments, CreatureReach reach,
                                            service::HttpResponse& refusal);

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

// The interaction commands (stage 2.5a, world/interactions.h, server/http/interaction_commands.cpp).

/** The objects of the admin state of a character: each object of the interaction data this near to it (meters, horizontal). */
inline constexpr float kAdminInteractionRange = 30.0F;

/**
 * @return The interactions of a character (world thread): its committed interaction ("active": the object key, the kind, the
 *         option, the seconds), each object of the interaction data that is `radius` or less from it with the state of each
 *         bound (enabled, users, options), the work of the lookup, and the counts of the requests by verdict.
 */
[[nodiscard]] nlohmann::json interactions_view(world::World& world, const std::string& character, float radius);
/**
 * set_object_enabled: `object` (the key of an object of the interaction data: its placement UUID) and `enabled` (1 or 0). Each
 * Interact component of the object is on or off: each session near it gets the state, and a disabled object refuses each
 * request. The admin rule (`nearCharacter`): the object must be kAdminInteractionRange or less from the character.
 */
[[nodiscard]] WorldCommand set_object_enabled(const std::string& character, const Arguments& arguments, CreatureReach reach,
                                              service::HttpResponse& refusal);
/** cancel_interaction: no argument. The committed interaction of the character ends; its client gets the message 3746. */
[[nodiscard]] WorldCommand cancel_interaction(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
/**
 * make_fixture (stage 2.5b, a development aid of the live checks L5 and L6; the development routes only): `name` (a fixture of the
 * interaction data: powderKeg, campT5, camp), and when necessary `dx`, `dy`, `dz` (meters from the character; default 3 m to the
 * north), `yaw` (degrees, to the left), `permission` (the permission rule of its Ownership state, 0 to 255; default: the rule of the
 * data). The reply has the entity id of the fixture.
 */
[[nodiscard]] WorldCommand make_fixture(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
/** remove_object (stage 2.5b, the development routes only): `entity` (16 hexadecimal digits): a server child goes away. */
[[nodiscard]] WorldCommand remove_object(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);

/** @return The bag, the equipment, and the coin of a character (world thread), or no value when the store does not have it. */
[[nodiscard]] std::optional<nlohmann::json> items_view(world::World& world, const std::string& character);

/** @return The level, the attributes, the weapon masteries, and the trade skills of a character (world thread), or no value. */
[[nodiscard]] std::optional<nlohmann::json> progress_view(world::World& world, const std::string& character);

/**
 * @return The ownership of the DLC of a character (stage 1.1b, world/ownership.h; world thread): the owned expansion with its
 *         limits, the entitlements of its account, its achievements, and the rewards that it got; or no value.
 */
[[nodiscard]] std::optional<nlohmann::json> ownership_view(world::World& world, const std::string& character);

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

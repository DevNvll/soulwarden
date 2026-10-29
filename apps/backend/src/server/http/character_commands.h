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

/** The arguments of a query text ("a=1&b=2"). A value has its escapes %XX decoded (a "+" stays a "+"); a malformed escape: no value. */
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
/**
 * Stage QG1, give_standing: `territory` (the territory id, 6 is Windsward) and `amount` (1 to 10,000,000): a gain of the territory standing
 * with the rules of a game reward (Skills::give_standing: the GainStanding card, the ranks, one token for each rank, the experience of the
 * ranks). The reply has the gain, the rank, the points in the rank, the free tokens, and the experience of the ranks.
 */
[[nodiscard]] WorldCommand give_standing(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
/**
 * Stage QG7, set_faction: `faction` (none, syndicate, marauder, covenant, or 0 to 3), and optionally `changed` (now, clear, or the wall time
 * of the last change in Unix nanoseconds) and `changes` (0 to 65535): the faction of the character with no check of the data path, the
 * wait, or the cost (Factions::set; docs/systems/factions.md). The client of a character in the world gets the Faction state, and each
 * observer the faction. A harness command: it refuses (403) with no SOULWARDEN_HARNESS=1 in the backend process.
 */
[[nodiscard]] WorldCommand set_faction(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
/**
 * Stage QG7, set_faction_progress: `faction` (syndicate, marauder, covenant), `kind` (reputation or tokens), `rank` (0 to the largest
 * rank), and `points` (at most the cap of the rank): the rank and the points of a faction progression (Skills::set_faction_progress). A
 * harness command.
 */
[[nodiscard]] WorldCommand set_faction_progress(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
/**
 * Stage QG1, choose_territory_card: `territory` and `card` (the id text of the card, TB_GainStanding): one card choice with the rules of
 * the client message 4040 (Skills::spend_territory_point). The reply has the level of the card and the free tokens.
 */
[[nodiscard]] WorldCommand choose_territory_card(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
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
/**
 * The stamina and the dodges (stage 3.4f-B), for the development list (`tools/world list`, `tools/world stamina`): the data, the
 * counts of the dodge inputs by verdict, the payments, the gains, the empty stamina, the avoided creature hits, and for each
 * character its stamina, its maximum, the exhausted state, and its dodge.
 */
[[nodiscard]] nlohmann::json stamina_json(world::World& world);
/** @return The counts of the sequences of the basic attacks (stage 3.3b-M1) for the list of the world. */
[[nodiscard]] nlohmann::json basic_attacks_json(world::World& world);
// The tree and the bar commands (stage 3.3a, world/skills.h): the same rules as the requests of
// a client. assign_node: `weapon` (as for the mastery) and `node` (the id text of the node of the
// game table, "Ability_WarHammer_WreckingBall"): one mastery point on the node.
// set_ability_bar: `weapon`, `slot` (0 to 2), and `ability` (the id text of an active node that
// the character has, or "none": an empty place).
[[nodiscard]] WorldCommand assign_node(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
[[nodiscard]] WorldCommand set_ability_bar(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);

// The quests (stage 1.8a, world/quests.h, server/http/quest_commands.cpp).
// apply_backstory: `template` (a backstory template of the quest data, "Level60_PTR"): decision D2 option C (decision of the orchestrator,
// 2026-10-18): the achievements and the start objectives of the template, not its level, items, or faction. The reply 200 has the
// achievements, the objectives, and the ids that the data does not have; an unknown template gives 404, a database failure 500.
// grant_objective (development only): `objective` (an ObjectiveID): a new instance of the objective for a character in the world.
// set_achievement (development only): `id` (an achievement id) and `on` (1: unlock, 0: lock): the change event of the quest system.
[[nodiscard]] WorldCommand apply_backstory(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
[[nodiscard]] WorldCommand grant_objective(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
[[nodiscard]] WorldCommand set_achievement(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
// Stage 1.8e: quest_cinematic (development only): `id` (a cinematic id, TempestRising) and `phase` (start or end): the notice of the client
// messages 6606 and 6542 to the quest system (Quests::cinematic; an end needs the start of the same cinematic). A development run disables the
// audio system of the client (-sys_audio_disable 1), and then the Bink player of the client returns before it opens the video (0x6506CB0):
// the client sends neither message. The command does not close the video screen of the client.
[[nodiscard]] WorldCommand quest_cinematic(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
// Stage 1.8d: quest_rewards (a reader): `objective` and maybe `territory` (the origin territory; default the ExclusiveTerritory of the row): the
// one reward join of the hand-in (Quests::reward_join: what a hand-in pays, or the reward that refuses it) and the availability of the offer for
// the character (Quests::available). No change.
[[nodiscard]] WorldCommand quest_rewards(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
// Stage QG3: quest_places (a reader, no argument): the places of the data at the accepted position of the character: its territory (the
// bounds), each point of interest and area shape that holds it (the id, the tags, the kind, the shape file), and each trigger volume that
// holds it (the tag, the slice, the placement). A grid of the data visits only the shapes near the position. No change.
[[nodiscard]] WorldCommand quest_places(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
// Stage 1.8c: abandon_quest and reset_quest: `instance` (16 hexadecimal digits: an active instance of the character in the world): the rule of
// the client messages 3836 and 5600 (world/quests.h: the undo of the achievement actions, the return of the items, the pin; a reset makes a
// new instance and keeps the award rows). An objective with CanBeAbandoned 0 refuses the abandon (409).
[[nodiscard]] WorldCommand abandon_quest(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
[[nodiscard]] WorldCommand reset_quest(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
/** @return The quests of a character (world thread): each quest with its tasks and its pin, the one-time grants, and the counts. */
[[nodiscard]] std::optional<nlohmann::json> quests_view(world::World& world, const std::string& character);
/** @return The backstory templates of the quest data (world thread): each one with its start objectives (and if the data has each one). */
[[nodiscard]] nlohmann::json backstories_json(world::World& world);
/**
 * Stage 1.8e: the support manifest of the quests (world thread; world/quest_support.h): one row for each objective with its grants, gates,
 * tasks, world content, rewards, and the reasons of a block, and the counts of each history (a new character, decision D2). A reader.
 */
[[nodiscard]] nlohmann::json quest_support_json(world::World& world);

// The build presets (stage B0, world/build_presets.h, server/http/preset_commands.cpp).
// apply_preset: `preset` (the name of a preset, "bow-boltcaster") and, when necessary, `variant` (the name of a variant, "pve" or
// "pvp"; no value: the first variant): the whole build of the preset for the character, in one task of the world and one transaction
// of the database. The reply 200 has "changes" (one text for each change); a preset that a system refuses (a bag with no room, a level
// above the cap) gives the reply 409, an unknown preset or variant 404, and a disabled preset 409, with the cause: nothing changes.
[[nodiscard]] WorldCommand apply_preset(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
/**
 * @return The build presets of the backend (world thread): for each preset its name, title, "enabled" with the cause of a disabled
 *         preset, the variants (name, label, and the trees: table, level, nodes, bar), the level, the attributes, and the items (slot,
 *         item, quantity, gear score, perks).
 */
[[nodiscard]] nlohmann::json presets_json(world::World& world);

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
/** @return The ability rows of the data with a part of the stamina or of the dodge (stage 3.4f-B), each with what the backend does. */
[[nodiscard]] nlohmann::json passives_stamina_json(world::World& world);

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

// The world time commands (stage 2.2, world/world_time.h, server/http/world_time_commands.cpp).
// set_world_time: `time` ("HH:MM" or "HH:MM:SS", 00:00 to 23:59:59): the time of the day of the whole world, not of the character
// (the character names who gives the command, rule MP-9). Each player gets the new time; the reply has "scope":"world" and
// "worldTime". A time that is not a time of the day gives the reply 400, and a refused database write the reply 500: the time does
// not change.
[[nodiscard]] WorldCommand set_world_time(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
/**
 * @return The world time (world thread): the time ("HH:MM:SS"), the hour, the phase and its number, the next phase and the seconds to
 *         it, the position in the cycle, the cycle (its lengths, dawn, dusk), and the counts (a stored position at the start, the phase
 *         changes, the commands, the saves).
 */
[[nodiscard]] nlohmann::json world_time_json(world::World& world);

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
 * shoot: `creature` (stage 3.4f-A). A development command: the creature starts its basic shot (the action FireBow of its slice) at the
 * character of the command now, with no band and no repeat time; the release, the aim, the flight, and the hit are the rules of a shot
 * of the behavior. Only the development routes have it.
 */
[[nodiscard]] WorldCommand creature_shoot(const std::string& character, const Arguments& arguments, CreatureReach reach,
                                          service::HttpResponse& refusal);
/**
 * area: `creature` (stage 3.4f-C). A development command: the creature starts its next area attack (an action with the casts of a ground
 * area of its slice) at the character of the command now, with no distance and no repeat time; the casts, the places, the areas, and their
 * hits are the rules of the behavior and of the spell system. Only the development routes have it.
 */
[[nodiscard]] WorldCommand creature_area(const std::string& character, const Arguments& arguments, CreatureReach reach,
                                         service::HttpResponse& refusal);
/**
 * status-action: `creature`, and `action` (a name; no value: the first one) (stage 3.4f-D). A development command: the creature starts a
 * status action of its slice now (its heal or its buff), with no condition and no repeat time; its marks give their statuses to the
 * creature (the rules of the behavior and of the creature system). Only the development routes have it.
 */
[[nodiscard]] WorldCommand creature_status_action(const std::string& character, const Arguments& arguments, CreatureReach reach,
                                                  service::HttpResponse& refusal);
/**
 * tree: `creature` (stage BT1). A development command: the behavior tree of the creature and its binding (stage BT2: the binding that
 * selected it, and the cause when the slice did not fit the tree of its binding), its last step (what decided it: the tree,
 * or a rule of the engine; the result of each node: the path), the parameters of each node, and the blackboard (the declared keys of the
 * tree, and the facts of the creature that the leaves use: the names of the auto-evaluated keys of the client where the meaning is the
 * same, research BT0). Only the development routes have it.
 */
[[nodiscard]] WorldCommand creature_tree(const std::string& character, const Arguments& arguments, CreatureReach reach,
                                         service::HttpResponse& refusal);
/**
 * tree reload (stage BT1, a harness command): the behavior trees again, from the program and from the folder of the world options
 * (SOULWARDEN_BEHAVIOR_DIR of a development run); each creature of the world gets its new tree. A file that is not correct is refused
 * (its cause in the reply and one log line), and the tree of its name stays. Needs SOULWARDEN_HARNESS=1 (403 without it). Only the
 * development routes have it.
 */
[[nodiscard]] WorldCommand reload_creature_trees(const std::string& character, const Arguments& arguments, CreatureReach reach,
                                                 service::HttpResponse& refusal);
/**
 * tree set: `creature`, `tree` (the name of a tree of the set) (stage BT2, a harness command). The creature gets the tree now, until a
 * reload of the trees or its removal (`Behavior::set_tree`); "default" gives it the default tree. A tree that names an action that the
 * slice of the creature does not have is refused (409, with the cause), and the creature keeps its tree; a name that is not a tree: 404.
 * Needs SOULWARDEN_HARNESS=1 (403 without it). Only the development routes have it.
 */
[[nodiscard]] WorldCommand set_creature_tree(const std::string& character, const Arguments& arguments, CreatureReach reach,
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

// The person commands (stage 2.5c, world/persons.h, server/http/person_commands.cpp).

/** The persons of the admin state of a character: each person this near to it (meters, horizontal). */
inline constexpr float kAdminPersonRange = 30.0F;

/**
 * @return The person conversation of a character (world thread): the NPC, the selected state, the topics, the services, the finished
 *         topics, and the open flag; each person of the interaction data that is `radius` or less from it with its NPC row, its presence
 *         rule and its value for the character, and the selection that the character gets now; and the counts of the person system.
 */
[[nodiscard]] nlohmann::json persons_view(world::World& world, const std::string& character, float radius);

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

// The chest commands (stage 3.5b.1, world/chests.h, server/http/chest_commands.cpp).

/** The chests of the admin state of a character: each chest this near to it (meters, horizontal). */
inline constexpr float kAdminChestRange = 30.0F;

/**
 * @return The chests of a character (world thread): its open, its loot claims (the chest, the state, the seconds to the reset, the kept
 *         roll), each chest that is `radius` or less from it (its kind, its rows, its state or the reason that it is unavailable, its
 *         claim), and the counts of the system.
 */
[[nodiscard]] nlohmann::json chests_view(world::World& world, const std::string& character, float radius);
/** clear_chest_claim: `chest` (the placement key of a chest). The claim of the character on it goes away (a character not in the world: its row). */
[[nodiscard]] WorldCommand clear_chest_claim(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
/**
 * set_chest_reset (a development aid of live check L8; the development routes only): `chest` and `seconds` (1 to 604800). The looted
 * claim of a character in the world resets after that time, not at its time of the data.
 */
[[nodiscard]] WorldCommand set_chest_reset(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);

// The gathering commands (stage 3.6a, world/gathering.h, server/http/gathering_commands.cpp).

/** The gathering of the admin state of a character: the nodes with a state of the server this near to it (meters, horizontal). */
inline constexpr float kAdminGatheringRange = 30.0F;

/**
 * @return The gathering of a character (world thread): its work (the node, the row, the amount, the time, the speed parts), its speed and
 *         luck for each trade skill of gathering (with the tools in its paperdoll), the nodes with a state of the server `radius` or less
 *         from it (depleted, the seconds to the respawn), and the counts of the system.
 */
[[nodiscard]] nlohmann::json gathering_view(world::World& world, const std::string& character, float radius);
/** Stage QG5c2: the encounters of the admin state of a character: the instances this near to it (meters; the scan range of the runner). */
inline constexpr float kAdminEncounterRange = 200.0F;
/** Stage QG5c2 (world/encounters.h): the encounter instances near a character (radius in meters), its links, and the counts of the runner. */
[[nodiscard]] nlohmann::json encounters_view(world::World& world, const std::string& character, float radius);
/** Stage QG5c2: each encounter recipe of the data with its pattern, its quest events, and its refusal causes (empty: the runner takes it). */
[[nodiscard]] nlohmann::json encounter_recipes(world::World& world);
/** Stage QG5c2, a development aid (the development routes only): `key` of a recipe: its instance ends, and its placement is free at once. */
[[nodiscard]] WorldCommand reset_encounter(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
/** Stage QG5c2, a development aid (the development routes only): the start object near the character counts as gathered by it (no gather). */
[[nodiscard]] WorldCommand use_start_object(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
/**
 * set_node_respawn (a development aid; the development routes only): `node` (a placement UUID, a key REGION#RECORD, or a G) and `seconds`
 * (1 to 604800). A depleted node respawns after that time, not at its time of the data.
 */
[[nodiscard]] WorldCommand set_node_respawn(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);

// The crafting commands (stage 3.7a, world/crafting.h, server/http/crafting_commands.cpp).

/**
 * @return The crafting of a character (world thread): the crafting data (the recipes, the recipes of this stage), the station of its use
 *         (the Assembly key, the row, the camp mark, the fee flag), its craft batch (the recipe, the completed jobs, the fee paid), and
 *         the counts of the system (the verdicts, the jobs, the ends, the visits of the load case).
 */
[[nodiscard]] nlohmann::json crafting_view(world::World& world, const std::string& character);
/**
 * craft_preview (a diagnostic; the development routes and the tool window): `recipe` (the RecipeID text) and `quantity` (1 to 10000, default
 * 1). The plan of the recipe for the character: the checks of a request with the station of its use (no station: the plan, and the verdict
 * noStation), the costs of one job, the output, the fee, the gear score, the slots that the plan changes. No change. Stage 3.7c: `perks` (the
 * resource item ids of the perk items, with "," between them) and `upgrade` (0 to 5): the craft gear score with its parts, the perk plan,
 * the Azoth; with `seed` also the perks of each job (before and after the swap of the game).
 */
[[nodiscard]] WorldCommand craft_preview(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
/** stop_craft: the craft batch of the character stops (the completed jobs stay; its client gets the final progress). */
[[nodiscard]] WorldCommand stop_craft(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
/**
 * set_craft_cooldown (stage 3.7b, a harness command): `recipe` (a RecipeID with a cooldown), `used` (0 to its CooldownQuantity), `seconds`
 * (the time to the end of the window; 0: no window). The row, then the entity and its Crafting state.
 */
[[nodiscard]] WorldCommand set_craft_cooldown(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
/** set_craft_seed (stage 3.7b, a harness command): `seed`, the next state of the generator of the world (a seeded craft batch). */
[[nodiscard]] WorldCommand set_craft_seed(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);
/**
 * Code review of stage 3.7b, finding CR7B-R2: @return True when the process has the harness gate (SOULWARDEN_HARNESS=1 in its environment).
 * The harness commands (set_craft_cooldown, set_craft_seed) refuse to run without it (403), also through the admin routes: the admin
 * interface of a start of the user has no harness gate.
 */
[[nodiscard]] bool harness_gate();
/**
 * set_currency (stage 3.7c): `amount` (0 to the cap of the currency) and `currency` (default Azoth_Currency, a currency of the crafting data):
 * the balance of the character (its categorical progression of rank 0). As set_coin: the rows, then the entity and its categorical state.
 */
[[nodiscard]] WorldCommand set_currency(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal);

// The loot bag commands (stage 3.5b.2, world/loot_bags.h, server/http/loot_bag_commands.cpp).

/**
 * @return The loot bags of a character (world thread): its open bag and each bag in which it has a part (the kind, the source, the place,
 *         the distance, the seconds to its end, the items of its own part: never the part of a different character), and the counts.
 */
[[nodiscard]] nlohmann::json loot_bags_view(world::World& world, const std::string& character);

/** @return The bag, the equipment, the coin, and (stage 3.7c) the Azoth balance and its cap of a character (world thread), or no value. */
[[nodiscard]] std::optional<nlohmann::json> items_view(world::World& world, const std::string& character);

/**
 * @return The level, the attributes, the weapon masteries, and the trade skills of a character (world thread), or no value. Stage QG1:
 *         also "territory" (the territory at the position of a character in the world: World::territory_of) and "standings" (each
 *         territory standing: the rank, the points, the points of the rank, the free tokens, and each card with its level and bonus); stage
 *         QG7: "faction" (the faction, the time and the count of the last change, the wait, the cost, and the reputation and the tokens of
 *         each faction).
 */
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

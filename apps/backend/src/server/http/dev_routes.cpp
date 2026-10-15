#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/logging/log.h"
#include "server/http/dev_support.h"
#include "server/http/routes.h"

// Development commands for the world. They are management routes of the backend, not routes
// of the game: a request with a service host gets 403. Each command names a character by its
// id (roadmap, rule MP-9). A command runs as a task of the world thread.
//
//   GET  /v1/dev/world                                   the entities of the world
//   POST /v1/dev/world/spawn?character=ID&x=X&y=Y&z=Z    put a character of the database into
//                                                        the world as an entity with no session
//   POST /v1/dev/world/spawn?character=ID&near=ID&dx=N&dy=N&dz=N
//                                                        the same, at a distance from the
//                                                        entity of a character that is in the world
//   POST /v1/dev/world/remove?character=ID               remove an entity that has no session
//   POST /v1/dev/world/travel?character=ID&place=PLACE[&screen=auto|on|off]
//   POST /v1/dev/world/travel?character=ID&x=X&y=Y&z=Z[&screen=auto|on|off]
//                                                        move a character to a travel place or
//                                                        to a position (a character that is not
//                                                        in the world: its row gets the place)
//   GET  /v1/dev/world/places                            the travel places of the game data
//   GET  /v1/dev/world/path?x0=X&y0=Y&z0=Z&x1=X&y1=Y&z1=Z the path of a creature between two places, by
//                                                        the navigation of the world (the corners, the
//                                                        length, the steps and the time of the search)
//   POST /v1/dev/world/touch?character=ID                mark each component state of an entity
//                                                        as changed: each observer gets its
//                                                        full record again
//   POST /v1/dev/world/fragment?character=ID&slot=N&type=N&state=HEX
//                                                        give an entity one state fragment with
//                                                        these bytes (a live check of a state
//                                                        type that has no encoder)
//   POST /v1/dev/world/heal?character=ID                 give a character its full health; a dead
//                                                        character lives again at its place
//   POST /v1/dev/world/ignore?character=ID&ignore=0|1    the creatures ignore the character (or not)
//   POST /v1/dev/world/hurt?character=ID&amount=N        a character takes damage (the health 0
//                                                        is its death)
//   POST /v1/dev/world/force-loot?character=ID&force=0|1 the loot switch: each kill of the
//                                                        character drops loot and coin (or not)
//   GET  /v1/dev/loot/roll?kind=KIND[&level=N][&count=N][&forced=1][&character=ID]
//                                                        roll the loot table of a kind many times
//                                                        and count the items (nothing is given)
//   POST /v1/dev/world/probe?near=ID&asset=HEX32&sub=N[&dx=N&dy=N&dz=N][&radius=N][&fragments=SLOT:TYPE:HEX,...]
//                                                        a probe entity (live check L4 of stage 3.3b-S2a): an
//                                                        entity of a slice of the game (asset id and sub id)
//                                                        whose states are the fragments of the command; the reply
//                                                        has its entity id and its actor id
//   POST /v1/dev/world/probe/remove?entity=ID            remove a probe entity: each observer gets the stop
//   POST /v1/dev/world/fragment?entity=ID&slot=N&type=N&state=HEX
//                                                        the fragment command for a probe entity
//   POST /v1/dev/world/friends?character=ID&friend=ID     the two characters are friends (the test fixture of a heal of
//                                                        a friend, stage 3.3b-S6)
//   GET  /v1/dev/heals?character=ID[&target=ID]          the heal base, the outgoing and received factors, the friends,
//                                                        and the counts of the heals (stage 3.3b-S6)
//   POST /v1/dev/world/message?character=ID&type=N&body=HEX
//                                                        send the client of a character one
//                                                        message with these bytes (a live check
//                                                        of a message that has no code)
//
// The items, the equipment, and the coin of a character (world/inventory.h). A command for a
// character that is not in the world changes its rows, and the reply has "applied":"stored".
//   GET  /v1/dev/items?character=ID                      the bag, the equipment, and the coin
//   POST /v1/dev/items/give?character=ID&item=ITEM&quantity=N[&gearScore=N][&action=N][&perks=P1,P2,,P4]
//                                                        give a quantity of an item (ITEM: the
//                                                        ItemID text of the item catalog)
//   POST /v1/dev/items/remove?character=ID&slot=N[&quantity=N]
//                                                        remove the item of a slot, or a quantity
//   POST /v1/dev/items/move?character=ID&from=N&to=N     move an item: an empty target gets it, a
//                                                        stack of the same item takes what it can
//                                                        hold, a different item changes places
//   POST /v1/dev/items/split?character=ID&slot=N&quantity=N[&to=N]
//                                                        make a new stack from a quantity of a stack
//   POST /v1/dev/items/lock?character=ID&slot=N&locked=0|1
//   POST /v1/dev/items/coin?character=ID&amount=N        set the coin (hundredths of a coin)
//   POST /v1/dev/items/equip?character=ID&from=N[&slot=N]
//                                                        move the item of the bag slot `from` into a
//                                                        slot of the paperdoll (no slot: the slot of
//                                                        the item class)
//   POST /v1/dev/items/unequip?character=ID&slot=N[&to=N]
//                                                        move the item of a slot of the paperdoll
//                                                        into the bag (no `to`: the first empty slot)
// The creatures of the world (world/creatures.h). The character of a command is the reference
// place: the list has the distance of each creature from it, and a new creature is near it.
//   GET  /v1/dev/creatures[?character=ID][&range=N]      the creatures of the world
//   GET  /v1/dev/creatures/kinds[?q=TEXT]                the kinds of the creature data
//   POST /v1/dev/creatures/spawn?character=ID&kind=KIND[&level=N][&dx=N&dy=N&dz=N][&yaw=N]
//   POST /v1/dev/creatures/remove?creature=ID
//   POST /v1/dev/creatures/health?creature=ID&health=N   (or percent=N)
//   POST /v1/dev/creatures/kill?creature=ID
//   POST /v1/dev/creatures/reset                         each spawn point that waits is free again
//   POST /v1/dev/creatures/action?creature=ID&action=NAME[&seconds=N][&tags=HEX]
//   POST /v1/dev/creatures/anchor?creature=ID&x=N&y=N&z=N  the anchor of a creature in a fight
//   POST /v1/dev/creatures/respawn?creature=ID&seconds=N the spawn point of a dead creature makes
//                                                        its creature again in N s
//   POST /v1/dev/world/fragment?creature=ID&slot=N&type=N&state=HEX
//                                                        the fragment command for a creature
//   POST /v1/dev/creatures/effect?creature=ID&status=NAME[&seconds=N]
//                                                        a creature takes an effect of the ability
//                                                        data, or a knockdown or a stagger (stage 3.3a)
// The abilities of a character (world/abilities.h, stage 3.3a):
//   GET  /v1/dev/abilities?character=ID                  the cooldowns of the character and the
//                                                        counts of the abilities
//   POST /v1/dev/abilities/reset?character=ID            each cooldown of the character ends
//   POST /v1/dev/abilities/cast?character=ID&place=N[&yaw=DEGREES]
//                                                        the press of a key of the bar for a character (stage
//                                                        3.3b-S2a: a remote test character with no client)
//   POST /v1/dev/spells/end?character=ID                 each spell entity of the character ends
//   POST /v1/dev/spells/hurt?entity=ID&amount=N          a summon or a trap takes damage (stage 3.3b-S2b; the health 0
//                                                        starts its death chain)
// The commands remove, health, and kill change each creature of the world, and they need no
// character (the rule CreatureReach::world of character_commands.h). The admin interface has a
// different rule for them.
// The level, the attributes, and the skills of a character: server/http/dev_progress_routes.cpp.
// The commands give, remove, and coin, and the list, are commands of the common layer
// (character_commands.h), which the admin routes use too.
// tools/world sends these requests to the backend of a development run.

namespace soulwarden::backend::server::http {
namespace {

using nlohmann::json;
using namespace dev;
using commands::item_reply;

/** Checks the request of an item command. @return The character id, or no value with the reply in `refusal`. */
std::optional<std::string> item_target(const RouteContext& context, service::HttpResponse& refusal) {
    return character_of(context, refusal);
}

/** Largest slot number, type index, and state size of a development fragment. */
constexpr std::uint64_t kMaxFragmentSlot = 4095;
constexpr std::uint64_t kMaxFragmentType = 65535;
constexpr std::size_t kMaxFragmentState = 16384;

const char* kind_text(world::EntityKind kind) {
    switch (kind) {
    case world::EntityKind::creature:
        return "creature";
    case world::EntityKind::probe:
        return "probe";
    case world::EntityKind::spell:
        return "spell";
    case world::EntityKind::player:
        break;
    }
    return "player";
}

/** @return The bytes of a text of hexadecimal digits (an even count), or no value. */
std::optional<std::vector<std::uint8_t>> hex_bytes(const std::string& text) {
    if (text.size() % 2 != 0) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes;
    for (std::size_t index = 0; index < text.size(); index += 2) {
        char* end = nullptr;
        const std::string digits = text.substr(index, 2);
        bytes.push_back(static_cast<std::uint8_t>(std::strtoul(digits.c_str(), &end, 16)));
        if (end != digits.c_str() + 2) {
            return std::nullopt;
        }
    }
    return bytes;
}

/** @return The id of an entity of a query parameter (16 hexadecimal digits), or no value. */
std::optional<world::EntityId> entity_id_of(const std::optional<std::string>& text) {
    if (!text || text->size() != 16 || text->find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
        return std::nullopt;
    }
    return std::strtoull(text->c_str(), nullptr, 16);
}

json entity_json(const world::Entity& entity) {
    char group[17];
    std::snprintf(group, sizeof(group), "%016llx", static_cast<unsigned long long>(entity.id));
    return {
        {"entity", group},
        {"kind", kind_text(entity.kind)},
        {"actor", world::actor_text(entity.actor)},
        {"character", entity.characterId},
        {"name", entity.name},
        {"session", entity.owner},
        {"position", {entity.position.x, entity.position.y, entity.position.z}},
        {"health", entity.vitals.health},
        {"healthMax", entity.vitals.healthMax},
        {"dead", entity.kind == world::EntityKind::creature ? entity.creature.dead : entity.life.dead},
    };
}

service::HttpResponse list_entities(const Services& services, const RouteContext& context) {
    if (!context.request.host.empty()) {
        return message(403, "forbidden");
    }
    return in_world(services, [](world::World& target) {
        json entities = json::array();
        for (const auto& [id, entity] : target.entities()) {
            entities.push_back(entity_json(entity));
        }
        // The load of the backend: the real time of its ticks (a measurement, in microseconds).
        const auto meter = [](const world::LoadMeter& load) {
            const auto micro = [](std::chrono::nanoseconds time) {
                return std::chrono::duration<double, std::micro>(time).count();
            };
            return json{{"count", load.count},
                        {"largest", micro(load.largest)},
                        {"mean", load.count != 0 ? micro(load.total) / static_cast<double>(load.count) : 0.0}};
        };
        const world::WorldLoad& load = target.load();
        const world::Creatures& creatures = target.creatures();
        const world::Behavior& behavior = creatures.behavior();
        const world::BehaviorCounts& behaviorCounts = behavior.counts();
        const world::Navigation& navigation = target.navigation();
        const world::NavigationCounts& navigationCounts = navigation.counts();
        return json_reply(200,
                          json{{"entities", entities},
                               {"revision", target.revision()},
                               {"load",
                                {{"worldTick", meter(load.worldTick)},
                                 {"serverTick", meter(load.serverTick)},
                                 {"spawnScan", meter(creatures.scan_load())},
                                 {"spawnPoints", creatures.spawn_points().size()},
                                 {"spawnPointsLooked", creatures.counts().pointsLooked},
                                 {"spawnScans", creatures.counts().scans},
                                 {"creatures", creatures.count()},
                                 {"behavior", meter(behavior.load())},
                                 {"behaviorOn", target.options().creatureBehavior},
                                 {"creaturesActive", behavior.active_count()},
                                 {"creaturesActiveLargest", behaviorCounts.largestActive},
                                 {"creatureSteps", behaviorCounts.creatureSteps},
                                 {"creaturesSensed", behaviorCounts.sensed},
                                 {"targets", behaviorCounts.targetsBySight + behaviorCounts.targetsByHit},
                                 {"attacks", behaviorCounts.attacks},
                                 {"contactsHit", behaviorCounts.contactsHit},
                                 {"contactsMissed", behaviorCounts.contactsMissed},
                                 {"moves", behaviorCounts.moves},
                                 {"targetsNoWay", behaviorCounts.targetsNoWay},
                                 // The roam (stage 3.4e): the creatures that patrol or wander near a character, and the reset.
                                 {"roaming", behavior.roaming_count()},
                                 {"roamingLargest", behaviorCounts.largestRoaming},
                                 {"roamSteps", behaviorCounts.roamSteps},
                                 {"waypoints", behaviorCounts.waypoints},
                                 {"wanderPlaces", behaviorCounts.wanderPlaces},
                                 {"roamNoWay", behaviorCounts.roamNoWay},
                                 {"holds", behaviorCounts.holds},
                                 {"retraces", behaviorCounts.retraces},
                                 {"jumps", behaviorCounts.jumps},
                                 {"immuneHits", behaviorCounts.immuneHits},
                                 {"returns", behaviorCounts.returns},
                                 // The rewards of the kills (stage 3.5a).
                                 {"rewards",
                                  {{"data", target.rewards().has_data()},
                                   {"kills", target.rewards().counts().kills},
                                   {"killsWithNoCredit", target.rewards().counts().killsWithNoCredit},
                                   {"credited", target.rewards().counts().credited},
                                   {"experience", target.rewards().counts().experience},
                                   {"masteryExperience", target.rewards().counts().masteryExperience},
                                   {"coin", target.rewards().counts().coin},
                                   {"lootGates", target.rewards().counts().lootGates},
                                   {"rolls", target.rewards().counts().rolls},
                                   {"items", target.rewards().counts().items},
                                   {"itemsLost", target.rewards().counts().itemsLost},
                                   {"time", meter(target.rewards().load())}}},
                                 // The abilities (stage 3.3a): the presses by cause, the hits, and the effects.
                                 {"abilities", commands::abilities_json(target, "")},
                                 // The spell entities (stage 3.3b-S2a).
                                 {"spells", commands::spells_json(target)},
                                 // The projectiles of the players (stage 3.3b-S3).
                                 {"projectiles", commands::projectiles_json(target)},
                                 {"spawnPointsWaiting", creatures.waiting_count()},
                                 {"removedByRespawn", creatures.counts().removedByRespawn},
                                 // The navigation: the tiles of the data, the path searches, and the sight tests.
                                 {"navigation",
                                  {{"data", navigation.has_data()},
                                   {"tiles", navigationCounts.tilesHeld},
                                   {"tilesLargest", navigationCounts.largestTilesHeld},
                                   {"tileBytes", navigationCounts.tileBytes},
                                   {"tilesLoaded", navigationCounts.tilesLoaded},
                                   {"tilesDropped", navigationCounts.tilesDropped},
                                   {"tilesMissing", navigationCounts.tilesMissing},
                                   {"tileLoad", meter(navigation.tile_load())},
                                   {"search", meter(navigation.search_load())},
                                   {"pathsDirect", navigationCounts.pathsDirect},
                                   {"searches", navigationCounts.searches},
                                   {"searchSteps", navigationCounts.searchSteps},
                                   {"searchesFailed", navigationCounts.searchesFailed},
                                   {"noWay", navigationCounts.noWay},
                                   {"partialPaths", navigationCounts.partialPaths},
                                   {"largestTickSteps", navigationCounts.largestTickSteps},
                                   {"queued", navigation.queued()},
                                   {"largestQueue", navigationCounts.largestQueue},
                                   {"waits", navigationCounts.waits},
                                   {"sightTests", navigationCounts.sightTests},
                                   {"sightStopped", navigationCounts.sightStopped}}}}}}
                              .dump());
    });
}

service::HttpResponse find_path(const Services& services, const RouteContext& context) {
    const service::HttpRequest& request = context.request;
    if (!request.host.empty()) {
        return message(403, "forbidden");
    }
    state::WorldPosition from;
    state::WorldPosition to;
    const std::pair<const char*, float*> numbers[] = {{"x0", &from.x}, {"y0", &from.y}, {"z0", &from.z},
                                                      {"x1", &to.x},   {"y1", &to.y},   {"z1", &to.z}};
    for (const auto& [name, value] : numbers) {
        const auto number = query_number(request, name);
        if (!number) {
            return message(400, "give the two places: x0, y0, z0, x1, y1, z1 (finite numbers)");
        }
        *value = *number;
    }
    return in_world(services, [from, to](world::World& target) {
        // The search of this command runs now, in the world thread, with no limit of a tick: a development aid.
        const world::PathReport report = target.navigation().find_path(from, to);
        json corners = json::array();
        for (const state::WorldPosition& corner : report.corners) {
            corners.push_back({corner.x, corner.y, corner.z});
        }
        return json_reply(200,
                          json{{"path", report.found},
                               {"cause", report.cause},
                               {"direct", report.direct},
                               {"length", report.length},
                               {"cells", report.cells},
                               {"steps", report.steps},
                               {"microseconds", std::chrono::duration<double, std::micro>(report.time).count()},
                               {"corners", corners}}
                              .dump());
    });
}

service::HttpResponse spawn_entity(const Services& services, const RouteContext& context) {
    const service::HttpRequest& request = context.request;
    if (!request.host.empty()) {
        return message(403, "forbidden");
    }
    const auto character = query_text(request, "character");
    if (!character || character->empty()) {
        return message(400, "give the character id: character=ID");
    }
    const auto anchorId = query_text(request, "near");
    state::WorldPosition position;
    if (anchorId) {
        // An offset that the request does not have is 0. An offset that is not a finite number
        // is a wrong command: it is not 0.
        const std::pair<const char*, float*> offsets[] = {{"dx", &position.x}, {"dy", &position.y}, {"dz", &position.z}};
        for (const auto& [name, value] : offsets) {
            if (!query_text(request, name)) {
                continue;
            }
            const auto number = query_number(request, name);
            if (!number) {
                return message(400, "dx, dy, and dz must be finite numbers");
            }
            *value = *number;
        }
    } else {
        const auto x = query_number(request, "x");
        const auto y = query_number(request, "y");
        const auto z = query_number(request, "z");
        if (!x || !y || !z) {
            return message(400, "give the position: x=X&y=Y&z=Z, or near=ID with dx, dy, dz");
        }
        position = state::WorldPosition{*x, *y, *z};
    }
    return in_world(services, [character = *character, anchorId, position](world::World& target) mutable {
        if (anchorId) {
            // The position of the other character, with the height of its spawn.
            const world::Entity* anchor = target.find_character(*anchorId);
            if (anchor == nullptr) {
                return message(404, "the character of 'near' is not in the world");
            }
            const state::WorldPosition base = world::spawn_position(*anchor);
            position = state::WorldPosition{base.x + position.x, base.y + position.y, base.z + position.z};
        }
        switch (target.spawn_character(character, position)) {
        case world::SpawnError::unknownCharacter:
            return message(404, "the store has no character with this id");
        case world::SpawnError::inWorld:
            return message(409, "the character is in the world");
        case world::SpawnError::badPosition:
            return message(400, "the position is not a position of the world");
        case world::SpawnError::none:
            break;
        }
        log::write("dev: character %s is in the world as an entity with no session", character.c_str());
        return json_reply(200, entity_json(*target.find_character(character)).dump());
    });
}

service::HttpResponse remove_entity(const Services& services, const RouteContext& context) {
    if (!context.request.host.empty()) {
        return message(403, "forbidden");
    }
    const auto character = query_text(context.request, "character");
    if (!character || character->empty()) {
        return message(400, "give the character id: character=ID");
    }
    return in_world(services, [character = *character](world::World& target) {
        const world::Entity* entity = target.find_character(character);
        if (entity == nullptr) {
            return message(404, "the character is not in the world");
        }
        if (entity->owner != 0) {
            return message(409, "the character has a session: this command removes only an entity with no session");
        }
        target.remove_character(character, world::LostCause::deleted);
        log::write("dev: the entity of character %s was removed", character.c_str());
        return message(200, "removed");
    });
}

service::HttpResponse touch_entity(const Services& services, const RouteContext& context) {
    if (!context.request.host.empty()) {
        return message(403, "forbidden");
    }
    const auto character = query_text(context.request, "character");
    if (!character || character->empty()) {
        return message(400, "give the character id: character=ID");
    }
    return in_world(services, [character = *character](world::World& target) {
        const world::Entity* entity = target.find_character(character);
        if (entity == nullptr) {
            return message(404, "the character is not in the world");
        }
        target.touch_all(entity->id);
        log::write("dev: each component state of character %s was marked as changed", character.c_str());
        return message(200, "touched");
    });
}

service::HttpResponse raw_fragment(const Services& services, const RouteContext& context) {
    const service::HttpRequest& request = context.request;
    if (!request.host.empty()) {
        return message(403, "forbidden");
    }
    const auto probe = query_text(request, "entity");
    const auto creature = probe ? probe : query_text(request, "creature");
    const auto character = creature ? creature : query_text(request, "character");
    const auto slot = query_integer(request, "slot", 0, kMaxFragmentSlot);
    const auto type = query_integer(request, "type", 0, kMaxFragmentType);
    const auto text = query_text(request, "state");
    if (!character || character->empty() || !slot || !type || !text || text->empty() || text->size() % 2 != 0) {
        return message(400, "give character=ID (or creature=ID), slot=N, type=N, and state=HEX");
    }
    if (creature && (creature->size() != 16 || creature->find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)) {
        return message(400, "creature must be the id of a creature (16 hexadecimal digits)");
    }
    if (text->size() / 2 > kMaxFragmentState) {
        return message(400, "the state is too large for a development fragment");
    }
    std::vector<std::uint8_t> state;
    for (std::size_t index = 0; index < text->size(); index += 2) {
        char* end = nullptr;
        const std::string digits = text->substr(index, 2);
        state.push_back(static_cast<std::uint8_t>(std::strtoul(digits.c_str(), &end, 16)));
        if (end != digits.c_str() + 2) {
            return message(400, "the state must be hexadecimal text");
        }
    }
    return in_world(services, [character = *character, isCreature = creature.has_value(), isProbe = probe.has_value(),
                               slot = static_cast<std::uint32_t>(*slot),
                               type = static_cast<std::uint32_t>(*type), state = std::move(state)](world::World& target) {
        const world::Entity* entity =
            isCreature ? target.creatures().find(std::strtoull(character.c_str(), nullptr, 16)) : target.find_character(character);
        if (isProbe) {
            entity = target.find(std::strtoull(character.c_str(), nullptr, 16));
            entity = entity != nullptr && entity->kind == world::EntityKind::probe ? entity : nullptr;
        }
        if (entity == nullptr) {
            return message(404, isCreature ? "the world has no creature with this id" : "the character is not in the world");
        }
        log::write("dev: fragment for character %s: slot %u, type %u, %zu bytes", character.c_str(), slot, type, state.size());
        target.set_raw_fragment(entity->id, slot, type, state);
        return message(200, "fragment set");
    });
}

service::HttpResponse make_probe(const Services& services, const RouteContext& context) {
    const service::HttpRequest& request = context.request;
    if (!request.host.empty()) {
        return message(403, "forbidden");
    }
    const auto nearCharacter = query_text(request, "near");
    const auto asset = query_text(request, "asset");
    const auto sub = query_integer(request, "sub", 0, 0xFFFFFFFFULL);
    const auto radius = query_number(request, "radius");
    const auto list = query_text(request, "fragments");
    std::optional<std::vector<std::uint8_t>> assetBytes = asset ? hex_bytes(*asset) : std::nullopt;
    if (!nearCharacter || nearCharacter->empty() || !assetBytes || assetBytes->size() != 16 || !sub) {
        return message(400, "give near=ID (a character in the world), asset=HEX (32 digits), and sub=N");
    }
    if (radius && (*radius < 1.0F || *radius > 1000.0F)) {
        return message(400, "radius must be 1 to 1000 m");
    }
    float offset[3] = {0.0F, 0.0F, 0.0F};
    const char* names[3] = {"dx", "dy", "dz"};
    for (int axis = 0; axis < 3; ++axis) {
        if (query_text(request, names[axis])) {
            const auto value = query_number(request, names[axis]);
            if (!value) {
                return message(400, "dx, dy, and dz must be finite numbers");
            }
            offset[axis] = *value;
        }
    }
    // The fragments: SLOT:TYPE:HEX, separated by commas.
    std::vector<world::RawFragment> fragments;
    if (list && !list->empty()) {
        std::size_t start = 0;
        while (start <= list->size()) {
            const std::size_t end = std::min(list->find(',', start), list->size());
            const std::string part = list->substr(start, end - start);
            const std::size_t first = part.find(':');
            const std::size_t second = first == std::string::npos ? std::string::npos : part.find(':', first + 1);
            if (second == std::string::npos) {
                return message(400, "each fragment is SLOT:TYPE:HEX");
            }
            char* stop = nullptr;
            const unsigned long slot = std::strtoul(part.substr(0, first).c_str(), &stop, 10);
            const unsigned long type = std::strtoul(part.substr(first + 1, second - first - 1).c_str(), &stop, 10);
            const auto bytes = hex_bytes(part.substr(second + 1));
            if (!bytes || bytes->empty() || bytes->size() > kMaxFragmentState || slot > kMaxFragmentSlot || type > kMaxFragmentType) {
                return message(400, "a fragment has a slot, a type, or a state that is not correct");
            }
            fragments.push_back(world::RawFragment{static_cast<std::uint32_t>(slot), static_cast<std::uint32_t>(type), *bytes, 0});
            start = end + 1;
        }
    }
    world::ProbeBody body;
    std::copy(assetBytes->begin(), assetBytes->end(), body.asset.begin());
    body.subId = static_cast<std::uint32_t>(*sub);
    if (radius) {
        body.interestRadius = *radius;
    }
    return in_world(services, [nearCharacter = *nearCharacter, body, offset0 = offset[0], offset1 = offset[1], offset2 = offset[2],
                               fragments = std::move(fragments)](world::World& target) mutable {
        const world::Entity* reference = target.find_character(nearCharacter);
        if (reference == nullptr) {
            return message(404, "the character of 'near' is not in the world");
        }
        const state::WorldPosition position{reference->position.x + offset0, reference->position.y + offset1,
                                            reference->position.z + offset2};
        const world::Entity* probe = target.make_probe(body, position, std::move(fragments));
        if (probe == nullptr) {
            return message(400, "the position is not a position of the world");
        }
        char group[17];
        std::snprintf(group, sizeof(group), "%016llx", static_cast<unsigned long long>(probe->id));
        return json_reply(200, json{{"probe", group},
                                    {"actor", world::actor_text(probe->actor)},
                                    {"position", {probe->position.x, probe->position.y, probe->position.z}},
                                    {"fragments", probe->rawFragments.size()}}
                                   .dump());
    });
}

service::HttpResponse remove_probe(const Services& services, const RouteContext& context) {
    const service::HttpRequest& request = context.request;
    if (!request.host.empty()) {
        return message(403, "forbidden");
    }
    const auto id = entity_id_of(query_text(request, "entity"));
    if (!id) {
        return message(400, "give entity=ID (16 hexadecimal digits)");
    }
    return in_world(services, [id = *id](world::World& target) {
        return target.remove_probe(id) ? message(200, "removed") : message(404, "the world has no probe with this id");
    });
}

service::HttpResponse hurt_spell(const Services& services, const RouteContext& context) {
    const service::HttpRequest& request = context.request;
    if (!request.host.empty()) {
        return message(403, "forbidden");
    }
    const auto id = entity_id_of(query_text(request, "entity"));
    const auto amount = query_number(request, "amount");
    if (!id || !amount || *amount < 0.0F || *amount > 1.0e6F) {
        return message(400, "give entity=ID (16 hexadecimal digits) and amount=N");
    }
    return in_world(services, [id = *id, amount = *amount](world::World& target) {
        log::write("dev: the spell %016llx takes %.1f damage (a command)", static_cast<unsigned long long>(id), static_cast<double>(amount));
        return target.spells().hurt(id, amount) ? message(200, "done") : message(404, "the world has no summon or trap with this id");
    });
}

service::HttpResponse raw_message(const Services& services, const RouteContext& context) {
    const service::HttpRequest& request = context.request;
    if (!request.host.empty()) {
        return message(403, "forbidden");
    }
    const auto character = query_text(request, "character");
    const auto type = query_integer(request, "type", 0, kMaxFragmentType);
    const auto text = query_text(request, "body");
    if (!character || character->empty() || !type || !text || text->size() % 2 != 0) {
        return message(400, "give character=ID, type=N, and body=HEX");
    }
    if (text->size() / 2 > kMaxFragmentState) {
        return message(400, "the body is too large for a development message");
    }
    std::vector<std::uint8_t> body;
    for (std::size_t index = 0; index < text->size(); index += 2) {
        char* end = nullptr;
        const std::string digits = text->substr(index, 2);
        body.push_back(static_cast<std::uint8_t>(std::strtoul(digits.c_str(), &end, 16)));
        if (end != digits.c_str() + 2) {
            return message(400, "the body must be hexadecimal text");
        }
    }
    return in_world(services, [character = *character, type = static_cast<std::uint32_t>(*type),
                               body = std::move(body)](world::World& target) {
        log::write("dev: message for the client of character %s: type %u, %zu bytes", character.c_str(), type, body.size());
        return target.development_message(character, type, body)
                   ? message(200, "message sent")
                   : message(404, "the character is not in the world, or no session plays it");
    });
}

service::HttpResponse list_items(const Services& services, const RouteContext& context) {
    service::HttpResponse refusal;
    const auto character = item_target(context, refusal);
    if (!character) {
        return refusal;
    }
    return in_world(services, [character = *character](world::World& target) {
        const auto view = commands::items_view(target, character);
        return view ? json_reply(200, view->dump()) : message(404, "the store has no character with this id");
    });
}

service::HttpResponse equip_item(const Services& services, const RouteContext& context) {
    service::HttpResponse refusal;
    const auto character = item_target(context, refusal);
    if (!character) {
        return refusal;
    }
    const service::HttpRequest& request = context.request;
    const auto from = query_integer(request, "from", 0, world::kMaxContainerSlots - 1);
    std::optional<std::uint32_t> slot;
    if (query_text(request, "slot")) {
        const auto value = query_integer(request, "slot", 0, world::paperdoll::kSlotCount - 1);
        if (!value) {
            return message(400, "slot must be a slot number of the paperdoll (0 to 49)");
        }
        slot = static_cast<std::uint32_t>(*value);
    }
    if (!from) {
        return message(400, "give from=N: the slot of the bag (0 to 499)");
    }
    return in_world(services, [character = *character, from = static_cast<std::uint32_t>(*from), slot](world::World& target) {
        return item_reply(target.inventory().equip(character, from, slot));
    });
}

service::HttpResponse unequip_item(const Services& services, const RouteContext& context) {
    service::HttpResponse refusal;
    const auto character = item_target(context, refusal);
    if (!character) {
        return refusal;
    }
    const service::HttpRequest& request = context.request;
    const auto slot = query_integer(request, "slot", 0, world::paperdoll::kSlotCount - 1);
    std::optional<std::uint32_t> to;
    if (query_text(request, "to")) {
        const auto value = query_integer(request, "to", 0, world::kMaxContainerSlots - 1);
        if (!value) {
            return message(400, "to must be a number from 0 to 499");
        }
        to = static_cast<std::uint32_t>(*value);
    }
    if (!slot) {
        return message(400, "give slot=N: the slot of the paperdoll (0 to 49)");
    }
    return in_world(services, [character = *character, slot = static_cast<std::uint32_t>(*slot), to](world::World& target) {
        return item_reply(target.inventory().unequip(character, slot, to));
    });
}

service::HttpResponse move_item(const Services& services, const RouteContext& context) {
    service::HttpResponse refusal;
    const auto character = item_target(context, refusal);
    if (!character) {
        return refusal;
    }
    const auto from = query_integer(context.request, "from", 0, world::kMaxContainerSlots - 1);
    const auto to = query_integer(context.request, "to", 0, world::kMaxContainerSlots - 1);
    if (!from || !to) {
        return message(400, "give from=N and to=N (0 to 499)");
    }
    return in_world(services, [character = *character, from = static_cast<std::uint32_t>(*from),
                               to = static_cast<std::uint32_t>(*to)](world::World& target) {
        return item_reply(target.inventory().move(character, from, to));
    });
}

service::HttpResponse split_item(const Services& services, const RouteContext& context) {
    service::HttpResponse refusal;
    const auto character = item_target(context, refusal);
    if (!character) {
        return refusal;
    }
    const service::HttpRequest& request = context.request;
    const auto slot = query_integer(request, "slot", 0, world::kMaxContainerSlots - 1);
    const auto quantity = query_integer(request, "quantity", 1, world::kMaxItemQuantity);
    std::optional<std::uint32_t> to;
    if (query_text(request, "to")) {
        const auto value = query_integer(request, "to", 0, world::kMaxContainerSlots - 1);
        if (!value) {
            return message(400, "to must be a number from 0 to 499");
        }
        to = static_cast<std::uint32_t>(*value);
    }
    if (!slot || !quantity) {
        return message(400, "give slot=N (0 to 499) and quantity=N (1 to 10000)");
    }
    return in_world(services, [character = *character, slot = static_cast<std::uint32_t>(*slot),
                               quantity = static_cast<std::uint32_t>(*quantity), to](world::World& target) {
        return item_reply(target.inventory().split(character, slot, quantity, to));
    });
}

service::HttpResponse lock_item(const Services& services, const RouteContext& context) {
    service::HttpResponse refusal;
    const auto character = item_target(context, refusal);
    if (!character) {
        return refusal;
    }
    const auto slot = query_integer(context.request, "slot", 0, world::kMaxContainerSlots - 1);
    const auto locked = query_integer(context.request, "locked", 0, 1);
    if (!slot || !locked) {
        return message(400, "give slot=N (0 to 499) and locked=0 or locked=1");
    }
    return in_world(services, [character = *character, slot = static_cast<std::uint32_t>(*slot),
                               locked = *locked == 1](world::World& target) {
        return item_reply(target.inventory().set_locked(character, slot, nullptr, locked));
    });
}

service::HttpResponse list_creatures(const Services& services, const RouteContext& context) {
    const service::HttpRequest& request = context.request;
    if (!request.host.empty()) {
        return message(403, "forbidden");
    }
    const std::string character = query_text(request, "character").value_or("");
    std::optional<float> range;
    if (query_text(request, "range")) {
        range = query_number(request, "range");
        if (!range || *range < 0.0F) {
            return message(400, "range must be a number that is not negative (meters)");
        }
    }
    return in_world(services, [character, range](world::World& target) {
        return json_reply(200, commands::creatures_view(target, character, range).dump());
    });
}

service::HttpResponse list_creature_kinds(const Services& services, const RouteContext& context) {
    if (!context.request.host.empty()) {
        return message(403, "forbidden");
    }
    // The creature data does not change after the start of the backend: a read needs no world task.
    const gamedata::CreatureData* data = services.world.options().creatureData;
    json kinds = json::array();
    if (data != nullptr && data->loaded()) {
        for (const gamedata::CreatureKind* kind : data->search(query_text(context.request, "q").value_or(""), 0, 100000)) {
            kinds.push_back(commands::creature_kind_json(*data, *kind));
        }
    }
    return json_reply(200, json{{"loaded", data != nullptr && data->loaded()}, {"kinds", kinds}}.dump());
}

/**
 * @return The handler of a development route for a creature command. The character is the
 *         reference place of the command: a command that needs none works with no character.
 *         A development route gives the rule `world`: the command can change each creature.
 */
RouteHandler creature_route(const Services& services, commands::CreatureCommand command) {
    return [&services, command](const RouteContext& context) {
        if (!context.request.host.empty()) {
            return message(403, "forbidden");
        }
        service::HttpResponse refusal;
        const std::string character = query_text(context.request, "character").value_or("");
        commands::WorldCommand step =
            command(character, commands::QueryArguments(context.request), commands::CreatureReach::world, refusal);
        return step ? in_world(services, std::move(step)) : refusal;
    };
}

} // namespace

void register_dev_routes(Router& router, const Services& services) {
    const auto bind = [&services](service::HttpResponse (*handler)(const Services&, const RouteContext&)) {
        return [&services, handler](const RouteContext& context) { return handler(services, context); };
    };
    router.add("GET", "", "/v1/dev/world", "dev.world", bind(list_entities));
    router.add("POST", "", "/v1/dev/world/spawn", "dev.world_spawn", bind(spawn_entity));
    router.add("POST", "", "/v1/dev/world/remove", "dev.world_remove", bind(remove_entity));
    router.add("POST", "", "/v1/dev/world/travel", "dev.world_travel", command_route(services, commands::travel));
    router.add("GET", "", "/v1/dev/world/places", "dev.world_places", [&services](const RouteContext& context) {
        if (!context.request.host.empty()) {
            return message(403, "forbidden");
        }
        // The travel places do not change after the start of the backend: a read needs no world task.
        return json_reply(200, commands::travel_places_json(services.world.options().travelPlaces).dump());
    });
    router.add("GET", "", "/v1/dev/world/path", "dev.world_path", bind(find_path));
    router.add("POST", "", "/v1/dev/world/touch", "dev.world_touch", bind(touch_entity));
    router.add("POST", "", "/v1/dev/world/fragment", "dev.world_fragment", bind(raw_fragment));
    router.add("POST", "", "/v1/dev/world/message", "dev.world_message", bind(raw_message));
    router.add("POST", "", "/v1/dev/world/probe", "dev.world_probe", bind(make_probe));
    router.add("POST", "", "/v1/dev/world/probe/remove", "dev.world_probe_remove", bind(remove_probe));
    router.add("POST", "", "/v1/dev/world/heal", "dev.world_heal", command_route(services, commands::heal));
    router.add("POST", "", "/v1/dev/world/ignore", "dev.world_ignore", command_route(services, commands::set_creatures_ignore));
    router.add("POST", "", "/v1/dev/world/hurt", "dev.world_hurt", command_route(services, commands::hurt_character));
    router.add("POST", "", "/v1/dev/world/force-loot", "dev.world_force_loot", command_route(services, commands::set_force_loot));
    router.add("GET", "", "/v1/dev/loot/roll", "dev.loot_roll", [&services](const RouteContext& context) {
        if (!context.request.host.empty()) {
            return message(403, "forbidden");
        }
        service::HttpResponse refusal;
        // The character is not necessary: with one, the roll has its level.
        const std::string character = query_text(context.request, "character").value_or("");
        commands::WorldCommand step = commands::roll_loot(character, commands::QueryArguments(context.request), refusal);
        return step ? in_world(services, std::move(step)) : refusal;
    });
    router.add("GET", "", "/v1/dev/items", "dev.items", bind(list_items));
    router.add("POST", "", "/v1/dev/items/give", "dev.items_give", command_route(services, commands::give_item));
    router.add("POST", "", "/v1/dev/items/remove", "dev.items_remove", command_route(services, commands::remove_item));
    router.add("POST", "", "/v1/dev/items/move", "dev.items_move", bind(move_item));
    router.add("POST", "", "/v1/dev/items/split", "dev.items_split", bind(split_item));
    router.add("POST", "", "/v1/dev/items/lock", "dev.items_lock", bind(lock_item));
    router.add("POST", "", "/v1/dev/items/coin", "dev.items_coin", command_route(services, commands::set_coin));
    router.add("POST", "", "/v1/dev/items/equip", "dev.items_equip", bind(equip_item));
    router.add("POST", "", "/v1/dev/items/unequip", "dev.items_unequip", bind(unequip_item));
    router.add("GET", "", "/v1/dev/creatures", "dev.creatures", bind(list_creatures));
    router.add("GET", "", "/v1/dev/creatures/kinds", "dev.creatures_kinds", bind(list_creature_kinds));
    router.add("POST", "", "/v1/dev/creatures/spawn", "dev.creatures_spawn", creature_route(services, commands::spawn_creature));
    router.add("POST", "", "/v1/dev/creatures/remove", "dev.creatures_remove", creature_route(services, commands::remove_creature));
    router.add("POST", "", "/v1/dev/creatures/health", "dev.creatures_health",
               creature_route(services, commands::set_creature_health));
    router.add("POST", "", "/v1/dev/creatures/kill", "dev.creatures_kill", creature_route(services, commands::kill_creature));
    router.add("POST", "", "/v1/dev/creatures/reset", "dev.creatures_reset", creature_route(services, commands::reset_creatures));
    router.add("POST", "", "/v1/dev/creatures/action", "dev.creatures_action",
               creature_route(services, commands::play_creature_action));
    router.add("POST", "", "/v1/dev/creatures/effect", "dev.creatures_effect",
               creature_route(services, commands::give_creature_effect));
    router.add("GET", "", "/v1/dev/abilities", "dev.abilities", [&services](const RouteContext& context) {
        service::HttpResponse refusal;
        const auto character = character_of(context, refusal);
        if (!character) {
            return refusal;
        }
        return in_world(services, [character = *character](world::World& target) {
            return json_reply(200, commands::abilities_json(target, character).dump());
        });
    });
    router.add("POST", "", "/v1/dev/abilities/reset", "dev.abilities_reset", command_route(services, commands::reset_cooldowns));
    router.add("POST", "", "/v1/dev/abilities/cast", "dev.abilities_cast", command_route(services, commands::cast_ability));
    router.add("POST", "", "/v1/dev/spells/end", "dev.spells_end", command_route(services, commands::end_spells));
    router.add("POST", "", "/v1/dev/spells/hurt", "dev.spells_hurt", bind(hurt_spell));
    router.add("POST", "", "/v1/dev/projectiles/shoot", "dev.projectiles_shoot", command_route(services, commands::shoot));
    // The itemization (stage 1.3b): the gear, a use of a consumable, the heart rune, and a perk roll.
    router.add("GET", "", "/v1/dev/gear", "dev.gear", [&services](const RouteContext& context) {
        service::HttpResponse refusal;
        const auto character = character_of(context, refusal);
        if (!character) {
            return refusal;
        }
        return in_world(services, [character = *character](world::World& target) {
            return json_reply(200, commands::gear_json(target, character).dump());
        });
    });
    // The passive rules of the data (stage 3.3b-S5): the rules of a character, and the rules of a weapon tree.
    router.add("GET", "", "/v1/dev/passives", "dev.passives", [&services](const RouteContext& context) {
        service::HttpResponse refusal;
        const auto character = character_of(context, refusal);
        if (!character) {
            return refusal;
        }
        return in_world(services, [character = *character](world::World& target) {
            return json_reply(200, commands::passives_json(target, character).dump());
        });
    });
    router.add("GET", "", "/v1/dev/passives/table", "dev.passives_table", [&services](const RouteContext& context) {
        const std::string table = query_text(context.request, "table").value_or("");
        if (table.empty() || table.size() > 64) {
            return message(400, "give table (the id of an ability table: GreatAxeAbilityTable)");
        }
        return in_world(services, [table](world::World& target) { return json_reply(200, commands::passives_table_json(target, table).dump()); });
    });
    router.add("POST", "", "/v1/dev/items/use", "dev.items_use", command_route(services, commands::use_item));
    router.add("POST", "", "/v1/dev/items/perks", "dev.items_perks", command_route(services, commands::set_item_perks));
    router.add("POST", "", "/v1/dev/rune/charge", "dev.rune_charge", command_route(services, commands::set_rune_charge));
    router.add("POST", "", "/v1/dev/rune/activate", "dev.rune_activate", command_route(services, commands::activate_rune));
    // The status effects of a character (stage 3.3b-S1).
    router.add("GET", "", "/v1/dev/status", "dev.status", [&services](const RouteContext& context) {
        service::HttpResponse refusal;
        const auto character = character_of(context, refusal);
        if (!character) {
            return refusal;
        }
        return in_world(services, [character = *character](world::World& target) {
            return json_reply(200, commands::status_json(target, character).dump());
        });
    });
    router.add("POST", "", "/v1/dev/status", "dev.status_apply", command_route(services, commands::apply_status));
    // The heals (stage 3.3b-S6): the heal values of a character, and the friend pair of a live test (a test fixture).
    router.add("GET", "", "/v1/dev/heals", "dev.heals", [&services](const RouteContext& context) {
        service::HttpResponse refusal;
        const auto character = character_of(context, refusal);
        if (!character) {
            return refusal;
        }
        const std::string other = query_text(context.request, "target").value_or("");
        return in_world(services, [character = *character, other](world::World& target) {
            return json_reply(200, commands::heals_json(target, character, other).dump());
        });
    });
    router.add("POST", "", "/v1/dev/world/friends", "dev.world_friends", [&services](const RouteContext& context) {
        service::HttpResponse refusal;
        const auto character = character_of(context, refusal);
        if (!character) {
            return refusal;
        }
        const std::string other = query_text(context.request, "friend").value_or("");
        if (other.empty() || other.size() > 64) {
            return message(400, "give friend (the id of a second character)");
        }
        return in_world(services, [character = *character, other](world::World& target) {
            const json reply = commands::make_friends(target, character, other);
            return json_reply(reply.value("friends", false) ? 200 : 404, reply.dump());
        });
    });
    router.add("POST", "", "/v1/dev/status/remove", "dev.status_remove", command_route(services, commands::remove_status));
    router.add("GET", "", "/v1/dev/perks/roll", "dev.perks_roll", [&services](const RouteContext& context) {
        if (!context.request.host.empty()) {
            return message(403, "forbidden");
        }
        service::HttpResponse refusal;
        const std::string character = query_text(context.request, "character").value_or("");
        commands::WorldCommand step = commands::roll_item_perks(character, commands::QueryArguments(context.request), refusal);
        return step ? in_world(services, std::move(step)) : refusal;
    });
    router.add("POST", "", "/v1/dev/creatures/anchor", "dev.creatures_anchor",
               creature_route(services, commands::move_creature_anchor));
    router.add("POST", "", "/v1/dev/creatures/respawn", "dev.creatures_respawn",
               creature_route(services, commands::set_respawn_time));
    register_dev_progress_routes(router, services);
}

} // namespace soulwarden::backend::server::http

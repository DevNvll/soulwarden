#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "gamedata/creature_data.h"
#include "state/creation_data.h"
#include "state/store.h"
#include "world/ids.h"
#include "world/item.h"
#include "world/world_clock.h"

// One entity of the world. The state of a character is here, with the character id as its
// key, and not in a session (roadmap, rule MP-1). An entity has plain data and no wire bytes:
// the replication layer of the game server makes the bytes for each session that observes
// the entity (server/rep/states).
//
// An entity is the character of a player or a creature (EntityKind). A creature has no
// character, no account, and no session: its key is its entity id. A function for a character
// (World::find_character, each system with a character id) never finds a creature.

namespace soulwarden::backend::world {

enum class EntityKind {
    /** The character of a player. */
    player,
    /** A creature of the world: an animal, a monster. The creature system makes it (world/creatures.h). */
    creature,
};

/** The component states of an entity that the replication layer sends. Each system adds its keys. */
enum class ComponentKey {
    metadata,
    player,
    movement,
    vitals,
    appearance,
    /** The level and the experience of a character. An observer gets the level. */
    progression,
    /** The attributes of a character. Only the owner gets them. */
    attribute,
    /** The coin of a character. Only the owner gets it. */
    currency,
    /** The bag of a character. Only the owner gets it. */
    container,
    /** The equipment of a character. The owner gets each item, and an observer the items that show on the body. */
    paperdoll,
    /** The rank and the experience of each weapon mastery and trade skill. Only the owner gets them. */
    categorical,
    /** The free points and the nodes of each weapon tree. Only the owner gets them. */
    points,
    /** The ability bar of each weapon. Only the owner gets it. */
    abilities,
    /** The name plate of a creature: the marker component of its slice. */
    marker,
    /** The mana of a character (its own component; the values are in Entity::vitals). Only the owner gets it. */
    mana,
};

/**
 * Health, stamina, and mana of a character. The progress system sets the maximum of the health
 * from the level and from the constitution of the character (world/progress.h). These are the
 * values of a backend with no progression tables.
 */
struct Vitals {
    float health{1000.0F};
    float healthMax{1000.0F};
    float stamina{100.0F};
    float staminaMax{100.0F};
    float mana{100.0F};
    float manaMax{100.0F};
};

/**
 * The level of a character. The progress system changes it (world/progress.h). The client
 * needs the level of its player before it shows an item: the script of an item cell compares
 * it with the level that the item needs, and it stops with an error when the client has no
 * level (protocol notes 4.10).
 */
struct Progression {
    /** The level as the screen shows it: 1 is the first level. */
    std::uint32_t level{1};
    /** Total experience points. A level has a range of them (the experience table of the game). */
    std::uint32_t experience{0};
};

/** Numbers of the attributes on the wire (`CharacterAttributeType` of the client). */
namespace attribute {
inline constexpr unsigned kConstitution = 0;
inline constexpr unsigned kFocus = 1;
inline constexpr unsigned kStrength = 2;
inline constexpr unsigned kDexterity = 3;
inline constexpr unsigned kIntelligence = 4;
inline constexpr unsigned kCount = 5;
} // namespace attribute

/** The attributes of a character. The progress system changes them (world/progress.h). */
struct Attributes {
    /** The points that the character spent on each attribute. The value of an attribute is `minimum` plus its points. */
    std::array<std::uint32_t, attribute::kCount> spent{};
    /** The value of an attribute with no point (the first row of the attribute tables of the game). */
    std::uint32_t minimum{5};
    /** The points that the character can spend: the points of its level minus the spent points. The progress system computes it. */
    std::uint32_t unspent{0};

    [[nodiscard]] std::uint32_t value(unsigned type) const {
        return minimum + spent[type];
    }
};

/** Rank and experience of one weapon mastery or trade skill. */
struct ProgressionRank {
    std::uint16_t rank{0};
    /** Experience in the rank, not the total. */
    std::uint64_t points{0};
};

/** One node of a weapon tree of a character. */
struct TreeNode {
    /** 1: the character has the node. 0: the character lost it (a tree reset, a refused request). */
    std::uint16_t level{0};
    /**
     * World revision of the last change. The client removes a node only when it gets the node
     * with the level 0: a node that the character lost stays in the map while the character
     * is in the world, and a session gets it one time.
     */
    Revision changed{0};
};

/**
 * The weapon masteries, the weapon trees, the ability bars, and the trade skills of a
 * character. The skill system changes them (world/skills.h). Each key is an id on the wire:
 * the CRC-32 of the lower-case id text of the game table (protocol notes 4.13). The id of a
 * weapon is the id of its mastery, of its point pool, and of its ability table.
 */
struct SkillSet {
    /** The weapon masteries and the trade skills that are not at the start (rank 0, no experience). */
    std::map<std::uint32_t, ProgressionRank> progressions;
    /** The point pool of each weapon, with the nodes of its tree. The node key is the id of the node. */
    std::map<std::uint32_t, std::map<std::uint32_t, TreeNode>> pools;
    /** The three active abilities of the bar of each weapon. 0: the place is empty. */
    std::map<std::uint32_t, std::array<std::uint32_t, 3>> bars;
    /**
     * World revision of the last bar request of a client that the backend refused. 0: none. It
     * is not a value of the character: the store does not have it. A session that got the
     * bars before this revision gets them again in a form that its client applies.
     */
    Revision barsRefused{0};

    [[nodiscard]] std::uint16_t rank(std::uint32_t id) const {
        const auto found = progressions.find(id);
        return found == progressions.end() ? 0 : found->second.rank;
    }
    /** @return The mastery points that the character spent in the tree of a weapon. */
    [[nodiscard]] std::uint32_t spent_points(std::uint32_t pool) const {
        std::uint32_t total = 0;
        const auto found = pools.find(pool);
        if (found != pools.end()) {
            for (const auto& [node, state] : found->second) {
                total += state.level;
            }
        }
        return total;
    }
    /**
     * @return The mastery points that the character can spend in the tree of a weapon: one
     *         point for each rank of the mastery, minus the spent points.
     */
    [[nodiscard]] std::uint32_t free_points(std::uint32_t pool) const {
        const std::uint32_t total = rank(pool);
        const std::uint32_t spent = spent_points(pool);
        return spent >= total ? 0 : total - spent;
    }
};

/** What the server permits the input of the owner to do. */
struct MovementPermissions {
    bool sprint{true};
    bool run{true};
    bool navigate{true};
    bool dodge{true};
};

/**
 * The action that a character plays: the script layers of the action list of its movement
 * state. The backend has no action system yet: each character has the idle action of a player
 * that stands. The numbers are ids of the action list of the player in the supported client
 * build: the values of layer 0 of the own character at rest in the live client (protocol
 * notes 4.9). The client of an observer shows no body for a player with no action sequence.
 */
struct ActionState {
    /** Number of script layers of the action list of a player. */
    std::uint8_t layers{4};
    /** State, start count of the state, and sequence of layer 0. */
    std::int32_t state{30};
    std::uint16_t started{1};
    std::int32_t sequence{1080};
};

/**
 * The action that the server started for a creature: one row of the action table of its slice
 * (game data). A creature with no such action plays the idle action that its client selects
 * (live proof: protocol notes 4.14). A client plays the action from its start time: a session
 * that observes the creature later gets the time that passed, and its client shows the pose of
 * that time.
 */
struct CreatureAction {
    /** Index of the action in the action table of the slice, and of its animation fragment. */
    std::uint32_t action{0};
    std::uint32_t fragment{0};
    /** The start count of the actions of this creature: a new action has a new count. */
    std::uint16_t generation{0};
    /**
     * The tag state of the animation fragments for this action: the tags of the slice of the
     * creature (game data), which select the variant of a fragment (protocol notes 4.14).
     */
    gamedata::FragmentTags tags{};
    /** The start of the action, on the world clock. */
    WorldClock::time_point started{};
    /**
     * The world revision of the start of the action. A session gets the action one time: in
     * its first record of the creature, or in the first record after this revision. A later
     * movement state of the creature (a step of its way) has no action for that session: the
     * client plays the action from the one state to its end.
     */
    Revision changed{0};
    /**
     * The end of an action that has an end (the reaction to a hit), on the world clock: the
     * creature then has no action of the server, and a session that observes it later gets
     * none. No value: the action stays (the death).
     */
    std::optional<WorldClock::time_point> ends;
};

/** What a creature does (world/behavior.h). */
enum class CreatureMode : std::uint8_t {
    /**
     * The creature has no target. It stands, or it walks its patrol route or its wander while a
     * player is near it (the roam of world/behavior.h). A creature that stands needs no time of
     * the world.
     */
    rest,
    /** The creature has a target: it goes to the target, and it attacks in its reach. */
    fight,
    /**
     * The creature lost its target and goes back to its anchor (the reset): it takes no damage
     * and no target on this way, and it has its full health at the end of it.
     */
    home,
};

/**
 * The behavior values of a creature: its target, its home, and the attack that it plays. They
 * are values of the creature in the world, not of a session (roadmap, rule MP-1). The behavior
 * of the creature system sets them (world/behavior.h).
 */
struct CreatureMind {
    CreatureMode mode{CreatureMode::rest};
    /**
     * The target: the entity of a character of the world. 0: none. The id of an entity is not
     * used again: an entity that left the world is found no more.
     */
    EntityId target{0};
    /** The home of the creature: the place and the direction of its spawn point, or where a command made it. */
    state::WorldPosition home;
    float homeYaw{0.0F};
    /**
     * The anchor of the leash: the place where the creature took its target (a rule of this
     * project, protocol notes 4.17). The leash of a fight is a distance from it, and the way back
     * ends at it. A creature that stands at its home has its home as its anchor.
     */
    state::WorldPosition anchor;
    /**
     * The places of the way of the creature since it took its target, one for each meter (the
     * first one is the anchor). A creature whose way back has no path walks these places back,
     * and it does not jump (world/behavior.h).
     */
    std::vector<state::WorldPosition> trail;
    /** True while the way back follows the trail. */
    bool retracing{false};
    /**
     * A creature that has no way to its target goes to the place nearest to it and waits there
     * with its target until this time; then it goes back. No value: it has a way, or it goes.
     */
    std::optional<WorldClock::time_point> holdUntil;
    /**
     * The roam of a creature at rest (world/behavior.h): the waypoint of its patrol route that it
     * goes to or waits at, and the direction of a route that goes back and forth (+1 or -1).
     */
    std::uint32_t waypoint{0};
    std::int8_t routeStep{1};
    /** The place that the creature walks to with no target (a waypoint, a place of its wander). No value: none now. */
    std::optional<state::WorldPosition> roamGoal;
    /** The creature waits at its place with no target until this time (at a waypoint, after a walk of its wander). */
    WorldClock::time_point roamWaitUntil{};
    /** True: the roam of the creature is complete (a waypoint with a wait for ever, a route of one waypoint at its end). */
    bool settled{false};
    /**
     * True: the creature stands on a free floor of the navigation of the world (with no
     * navigation data: on the terrain) and walks. False: it does not leave its place: it turns
     * to its target and attacks in its reach. The value is the answer of the navigation at the
     * first look that had the data of the place (`groundKnown`): the world reads the navigation
     * data of a place only when a character is near it.
     */
    bool walks{false};
    bool groundKnown{false};
    /**
     * The character that the creature found no way to. Until `noWayUntil` the vision of the
     * creature does not take it as a target again (a hit does).
     */
    EntityId noWayTarget{0};
    WorldClock::time_point noWayUntil{};
    /** The attack that the creature plays: an index into the attacks of its slice. No value: none. */
    std::optional<std::uint32_t> attack;
    /** The start count of the action of that attack, and its end on the world clock. */
    std::uint16_t attackGeneration{0};
    WorldClock::time_point attackEnds{};
    /** The creature starts no attack before this time (the pause after an attack). */
    WorldClock::time_point ready{};
    /** Number of attacks that the creature started. It selects the next attack of its slice. */
    std::uint32_t attacks{0};
    /** The direction of the creature in the last movement state that the behavior marked. */
    float sentYaw{0.0F};
    /** Steps in sequence in which a different creature stood at the place of the next step of this one. */
    std::uint32_t blockedSteps{0};
};

/**
 * The values of a creature. The creature system sets them (world/creatures.h). The health of a
 * creature is in Entity::vitals, and its position in Entity::position.
 */
struct Creature {
    /** The kind and the slice of the creature (game data). Null for an entity that is not a creature. */
    const gamedata::CreatureKind* kind{nullptr};
    const gamedata::CreatureSlice* body{nullptr};
    std::uint32_t level{1};
    /** Direction of the creature: the angle about the vertical axis, in degrees. 0: the creature looks north. */
    float yaw{0.0F};
    /** The time at which the world made the creature, on the world clock: the start of its spawn action. */
    WorldClock::time_point madeAt{};
    /** True from the moment at which the health of the creature is 0. A dead creature does not get health again. */
    bool dead{false};
    /** The time of the death, on the world clock. Stage 3.5a starts the respawn time and the time of the body here. */
    WorldClock::time_point diedAt{};
    /**
     * The action that the server started: the death action of a dead creature, an attack, the
     * reaction of a creature to a hit, or the action of a command, for the time of that action.
     * No value: the client selects the action itself (the idle action), or the creature is on
     * its way (`moving`).
     */
    std::optional<CreatureAction> action;
    /**
     * The spawn action of a creature whose slice needs it (protocol notes 4.14): the client
     * shows the body and the name plate of such a creature only after this action. It is a
     * value of the creature for its whole time in the world: the first movement state of each
     * session for the creature has it, with the time 0 on the wire (the client plays it from its
     * start), also while a different action of the server plays; that action follows 1.5 s later
     * (rep_session.cpp, kSpawnLead). `started` keeps the time of the creation of the creature: it
     * is not the time on the wire. A dead creature has its death action in the first state of a
     * session; a dead creature of a slice with no death action has this action there. No value:
     * the slice needs none.
     */
    std::optional<CreatureAction> spawnAction;
    /**
     * The locomotion action of a creature on its way (the action "Move_Combat" of its slice,
     * CreatureSlice::moveAction), from its first step to an attack or to its arrival at its
     * home. Each movement state of the way has it, with one start count: the client selects
     * the walk or the run from the speed of the creature, and without this action the creature
     * slides in its pose. No value: the creature is not on a way, or its slice has no such action.
     */
    std::optional<CreatureAction> moving;
    /** The behavior values of the creature. */
    CreatureMind mind;
    /**
     * The world revision of the last change of the immune mark (the way back): the vitals state
     * of a session that got the creature before it has the fields of the mark again.
     */
    Revision immuneChanged{0};
    /** @return True while the creature takes no damage and no target by a hit: on its way back (the reset). */
    [[nodiscard]] bool immune() const {
        return mind.mode == CreatureMode::home;
    }
    /** Number of actions that the server started for this creature. The count of an action on the wire is this number. */
    std::uint32_t actions{0};
    /** The spawn point of the creature: an index into the spawn points of the creature data. No value: a command made it. */
    std::optional<std::uint32_t> spawnPoint;
};

/**
 * The life state of a character. The health system sets it (world/health.h). The health of a
 * character is in Entity::vitals.
 */
struct Life {
    /** True from the moment at which the health of the character is 0, to its respawn. */
    bool dead{false};
    /** The time of the death, on the world clock. */
    WorldClock::time_point diedAt{};
    /** Number of deaths of the character while its entity is in the world. For the log and for the tests. */
    std::uint32_t deaths{0};
    /**
     * The time of the last damage that the character took or did, on the world clock. No
     * value: none in this world entry. The health of a character comes back when this time is
     * `Health::kRestTime` or more before now (world/health.h).
     */
    std::optional<WorldClock::time_point> foughtAt;
    /**
     * The shelter of a character that came to a place (a world entry, a travel, a respawn):
     * until `shelterUntil` the vision of no creature takes the character as a target. While
     * `shelterWaits` is true, the client of the character did not report a position from the
     * place (it shows a loading screen): the time `behavior_rules::kShelterTime` starts at its
     * first position there, and `shelterUntil` is then the latest end. The behavior of the
     * creatures sets these values (world/behavior.h).
     */
    WorldClock::time_point shelterUntil{};
    bool shelterWaits{false};
};

/** The combat values of a character. The combat system sets them (world/combat.h). */
struct CombatState {
    /**
     * The weapon slot of the paperdoll that the client of the character selected with a weapon
     * key. 0: none, and the weapon in the hand is the first weapon slot with an item. The
     * sheathe bits and the active bits of the paperdoll state do not follow it yet (roadmap 3.1).
     */
    std::uint32_t selectedWeapon{0};
};

/**
 * Development aid (tools/world fragment): one state fragment of an entity whose bytes a
 * command gives. The backend does not know its content. It is for a live check of a state
 * type that has no encoder yet.
 */
struct RawFragment {
    /** Index of the component in the component list of the slice. */
    std::uint32_t slot{0};
    /** Type index of the state type. */
    std::uint32_t type{0};
    /** The wire form of the state: the group mask, and the field mask and the values of each group. */
    std::vector<std::uint8_t> state;
    /** World revision of the command. */
    Revision changed{0};
};

struct Entity {
    /** Entity group id: the first 8 bytes of the actor id. Key of the entity table. */
    EntityId id{0};
    ActorId actor;
    EntityKind kind{EntityKind::player};
    /** Players: the key of all rows of the character, and the account that owns it. Empty for a creature. */
    std::string characterId;
    std::string personaId;
    /** The name of the character, or the English name of the kind of a creature. */
    std::string name;
    /** Creatures: the kind, the level, the direction, and the life state. */
    Creature creature;
    /** The session of the client that plays this character. 0: no session. */
    SessionId owner{0};

    /**
     * Position in the world. The world object changes it: World::report_position for the
     * position that the client of the owner reports, and World::set_position for an estimate.
     */
    state::WorldPosition position;
    /**
     * False when the position is the exact position of the character: a position report of
     * its client that passed the check, or the position of a development command. True for a
     * saved row and for the estimate from the camera of the owner: the character then spawns
     * some distance above it, so that it is not below the ground (kSpawnLift).
     */
    bool positionEstimated{false};
    Revision positionRevision{0};
    /**
     * The teleport number of the movement state of the entity: the low 4 bits of its teleport
     * field (protocol notes 4.6). The client of the owner takes the position of a movement
     * state of the server only when the state has a number that is not the number of its own
     * state. The start number of a client is 0: thus the number is 1 to 15, never 0, and the
     * first movement state of each session puts the character at the position of the entity.
     * World::travel gives the entity the number after the number that the client of its owner
     * has (SessionSink::client_teleport).
     */
    std::uint8_t teleport{1};
    /** True from the first refused position: the log has one line for an entity (World::set_position). */
    bool badPositionLogged{false};

    /** World revision of the last change of each component state. */
    std::map<ComponentKey, Revision> changed;
    state::AppearanceIndexes appearance{state::kDefaultAppearance};
    Vitals vitals;
    MovementPermissions movement;
    ActionState action;
    Progression progression;
    Attributes attributes;
    SkillSet skills;
    /** The items, the equipment, and the coin of the character. The inventory system changes them (world/inventory.h). */
    Bag bag;
    Equipment equipment;
    Wallet wallet;
    /** The combat values of the character. The combat system changes them (world/combat.h). */
    CombatState combat;
    /** The life state of the character. The health system changes it (world/health.h). */
    Life life;
    /** Development aid: fragments of a command, one for each slot at most. Empty in a product run. */
    std::vector<RawFragment> rawFragments;

    // Persistence of the position. Only the world object uses these members.
    /** True when the position is not the position of the row. */
    bool positionDirty{false};
    /** True while the character moves: the save rule then needs a larger distance. */
    bool moving{false};
    std::optional<state::WorldPosition> savedPosition;
    WorldClock::time_point lastSave{};
};

/**
 * A saved or estimated position is this much below the spawn position of the character. The
 * client lets the character fall to the ground from this height. This applies to an exact
 * saved position too (live runs of stage 2.3a, protocol notes 4.6): with 0.1 m the client does
 * not let the character fall at some places, and it then stays above the ground; with no
 * lift the row would need a mark that its position is exact, because an estimate needs the lift.
 */
inline constexpr float kSpawnLift = 0.5F;

/** @return The position that a client gets for the spawn of the entity. */
[[nodiscard]] inline state::WorldPosition spawn_position(const Entity& entity) {
    state::WorldPosition position = entity.position;
    if (entity.positionEstimated) {
        position.z += kSpawnLift;
    }
    return position;
}

} // namespace soulwarden::backend::world

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
    /** The start of the action, on the world clock. */
    WorldClock::time_point started{};
    /**
     * The end of an action that has an end (the reaction to a hit), on the world clock: the
     * creature then has no action of the server, and a session that observes it later gets
     * none. No value: the action stays (the death).
     */
    std::optional<WorldClock::time_point> ends;
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
    /** True from the moment at which the health of the creature is 0. A dead creature does not get health again. */
    bool dead{false};
    /** The time of the death, on the world clock. Stage 3.5a starts the respawn time and the time of the body here. */
    WorldClock::time_point diedAt{};
    /**
     * The action that the server started: the death action of a dead creature, or the reaction
     * of a creature to a hit for the time of that action. No value: the idle action of the client.
     */
    std::optional<CreatureAction> action;
    /** Number of actions that the server started for this creature. The count of an action on the wire is this number. */
    std::uint32_t actions{0};
    /** The spawn point of the creature: an index into the spawn points of the creature data. No value: a command made it. */
    std::optional<std::uint32_t> spawnPoint;
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

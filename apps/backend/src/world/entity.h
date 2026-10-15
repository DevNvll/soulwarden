#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "gamedata/creature_data.h"
#include "gamedata/spell_data.h"
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
// An entity is the character of a player, a creature, or a spell (EntityKind). A creature and a
// spell have no character, no account, and no session: their key is their entity id. A function
// for a character (World::find_character, each system with a character id) never finds them.

namespace soulwarden::backend::world {

enum class EntityKind {
    /** The character of a player. */
    player,
    /** A creature of the world: an animal, a monster. The creature system makes it (world/creatures.h). */
    creature,
    /**
     * A spell of the world (stage 3.3b-S2a, world/spells.h): the vortex of Gravity Well. The spell system makes it at the
     * place of the cast, each session near it gets its record, and it goes away at its end or at a cancel. It has no
     * character, no account, and no session.
     */
    spell,
    /**
     * Development aid (tools/world probe, stage 3.3b-S2a, live check L4): an entity of a slice of the game whose
     * states are only the fragments of a command. Each session near it gets its record (the metadata state and the
     * fragments) and its stop. Only a development command makes it.
     */
    probe,
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
    /** The cooldowns of the abilities of a character (stage 3.3a). Only the owner gets them. */
    cooldowns,
    /**
     * The status effects of an entity: of a creature (stage 3.3a: the icons at its name plate), of a character (stage
     * 3.3b-S1: the maps and the tray of the owner, the public maps of an observer; world/statuses.h).
     */
    status,
    /** The heart rune charge of a character (stage 1.3b, slot 48). Only the owner gets it. */
    charge,
    /**
     * The speed table of a character (stage 3.3b-S2a, S2a-I10; slot 43, StatMultiplierTable): the multipliers of the run
     * and of the sprint from its statuses. Only the owner gets it.
     */
    statMultipliers,
    /** The spell state of a spell entity (stage 3.3b-S2a, world/spells.h): its row, its place, its caster. */
    spell,
    /** The delayed event states of a spell entity: the time that is left for each event of its slice. */
    delayedEvents,
    /**
     * The place of a spell entity whose slice has no Spell component (stage 3.3b-S2b: the trap of the musket): the
     * PositionInTheWorld state of its slice (type 13).
     */
    spellPlace,
    /**
     * The movement of a character for its observers (stage 3.3b-S4): its position, its direction, and its action while it
     * makes the dash of an ability (Charge). The owner does not get it: its client moves its character itself.
     */
    motion,
    /** The entitlements of the account of a character (stage 1.1b, world/ownership.h): the bit vector. Only the owner gets it. */
    entitlements,
    /** The achievements of a character (stage 1.1b): the bit vector. Only the owner gets it. */
    achievements,
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
    /**
     * The mana regeneration (stage 1.3b, I14): the mana comes back from this time on, at the rate of the base vitals of a
     * player (a spend sets the delay of the data). No value: the mana is full, or the backend has no rate.
     */
    std::optional<WorldClock::time_point> manaRegenFrom;
    /** The mana in one second of the regeneration (the base vitals of a player; the mana state sends it while the mana is not full). */
    float manaRegenRate{0.0F};
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
    /**
     * The bonus of the equipment and of the effects on the character (stage 1.3b: the food, a perk with an attribute
     * value): the gear system sets it (world/gear.h). It is not in the database. The attribute state sends it in the map
     * `attributeBonuses`, apart from the base values.
     */
    std::array<std::uint32_t, attribute::kCount> bonus{};
    /** The value of an attribute with no point (the first row of the attribute tables of the game). */
    std::uint32_t minimum{5};
    /** The points that the character can spend: the points of its level minus the spent points. The progress system computes it. */
    std::uint32_t unspent{0};

    /** @return The value of an attribute with the points of the character and with no bonus (field 0 of the attribute state). */
    [[nodiscard]] std::uint32_t base(unsigned type) const {
        return minimum + spent[type];
    }
    /** @return The value of an attribute that the rules use: the base value and the bonus. */
    [[nodiscard]] std::uint32_t value(unsigned type) const {
        return minimum + spent[type] + bonus[type];
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
 * The damage that one character did to a creature in its life (world/rewards.h): the credit of a
 * kill. The rewards of a kill go to the characters whose entry passes the credit rule.
 */
struct CreditEntry {
    /** The entity of the character. The id of an entity is not used again. */
    EntityId character{0};
    std::string characterId;
    /** The sum of the damage of its hits (not more than the health that the creature had). */
    float damage{0.0F};
    /** The time of its last hit, on the world clock. */
    WorldClock::time_point last{};
};

/**
 * One status effect on a creature (stage 3.3a, world/abilities.h): a stun, a root, a slow, or a
 * weaken of an ability. The values come from the status row of the effect (game data); the
 * creature system keeps the effect until its end on the world clock (Creatures::apply_effect).
 */
struct CreatureEffect {
    /** The id text of the status row ("WarHammer_Shockwave_Stun"). */
    std::string status;
    /** The number of the effect on the wire (gamedata::StatusRow::index). */
    std::uint16_t index{0};
    /** True: each observer gets the icon of the effect at the name plate of the creature. */
    bool shown{false};
    /** True: the creature does not move and does not attack. */
    bool stun{false};
    /** True: the creature does not move (it turns and attacks). */
    bool root{false};
    /** The factor of the speed of the creature (1: no slow). */
    float speed{1.0F};
    /** The part of the damage of the creature that the effect adds (-0.1: a weaken of 10 percent). */
    float damage{0.0F};
    /** The part of the armor of the creature that the effect adds (-0.05: a rend of a perk; stage 1.3b). */
    float armor{0.0F};
    /** The effect ends at the next hit that the creature takes (a stun of Shockwave). */
    bool endsOnHit{false};
    /** The end of the effect on the world clock. */
    WorldClock::time_point ends{};
    /** The entity of the character whose ability made the effect. */
    EntityId source{0};
};

/**
 * The forced movement of a creature by the impact of a hit (stage 3.3b-S4, world/forced_motion.h): a pull or a push
 * with the values of the damage row (gamedata::DamageImpact), on the world clock. The creature system (its part
 * ForcedMotion) starts it, moves the creature at each step, and ends it.
 */
struct ForcedMove {
    /** The damage row of the hit (for the log and the tools). */
    std::string row;
    /** The start of the movement and its end at the latest (the length of the reaction), on the world clock. */
    WorldClock::time_point started{};
    WorldClock::time_point ends{};
    /** The time of the last step. */
    WorldClock::time_point stepped{};
    /** The place of the creature at the start. */
    state::WorldPosition start;
    /**
     * Mode of a decay rate above 0 (client code at image RVA 0x48A0D30): the travel at the time t is
     * `distance` * (1 - decay^(30 t)) in the unit direction `direction` (east, north) from `start`.
     */
    std::array<float, 2> direction{};
    float distance{0.0F};
    float decay{0.0F};
    /**
     * Mode of a decay rate of 0 or less (client code at image RVA 0x488B8F0): `speed` meters in each second to the point
     * (`toPoint`, a pull) or away from it (a push); a pull stops 0.1 m from the point.
     */
    bool constantSpeed{false};
    state::WorldPosition point;
    float speed{0.0F};
    bool away{false};
    /** The travel that the movement made so far (meters, on the line of the data: a wall can make it shorter). */
    float travelled{0.0F};
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
    /** The time of the death, on the world clock: the respawn time of its spawn point starts here (world/creatures.h). */
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
    /**
     * The characters that hurt the creature in its life, one entry for each character (the credit
     * of a kill, world/rewards.h). The entries go away when the creature has its full health again
     * at its anchor (the reset).
     */
    std::vector<CreditEntry> credit;
    /** The status effects on the creature (stage 3.3a). The creature system removes each one at its end. */
    std::vector<CreatureEffect> effects;
    /**
     * The end of a knockdown or a stagger of an ability (stage 3.3a): until this time the
     * creature does not move and does not attack. No value: none.
     */
    std::optional<WorldClock::time_point> knockedUntil;
    /**
     * The forced movement of the creature (stage 3.3b-S4): a pull or a push of a hit. While it runs, the behavior does not
     * move the creature and starts no attack. No value: none.
     */
    std::optional<ForcedMove> forced;

    /** @return True while an effect holds the creature: no movement and no attack (a stun, a knockdown). */
    [[nodiscard]] bool held(WorldClock::time_point now) const {
        if (knockedUntil && now < *knockedUntil) {
            return true;
        }
        for (const CreatureEffect& effect : effects) {
            if (effect.stun && now < effect.ends) {
                return true;
            }
        }
        return false;
    }
    /** @return True while a root holds the creature at its place (it turns and attacks). */
    [[nodiscard]] bool rooted(WorldClock::time_point now) const {
        for (const CreatureEffect& effect : effects) {
            if (effect.root && now < effect.ends) {
                return true;
            }
        }
        return false;
    }
    /** @return The factor of the speed of the creature: the strongest slow (a rule of this project). */
    [[nodiscard]] float speed_factor(WorldClock::time_point now) const {
        float factor = 1.0F;
        for (const CreatureEffect& effect : effects) {
            if (now < effect.ends && effect.speed < factor) {
                factor = effect.speed;
            }
        }
        return factor < 0.0F ? 0.0F : factor;
    }
    /** @return The factor of the damage of the creature: 1 plus the sum of the weakens (not less than 0). */
    [[nodiscard]] float damage_factor(WorldClock::time_point now) const {
        float part = 0.0F;
        for (const CreatureEffect& effect : effects) {
            if (now < effect.ends) {
                part += effect.damage;
            }
        }
        return part < -1.0F ? 0.0F : 1.0F + part;
    }
    /** @return The part of the armor of the creature that its effects add (a rend: -0.05; stage 1.3b). The strongest rend counts. */
    [[nodiscard]] float armor_part(WorldClock::time_point now) const {
        float part = 0.0F;
        for (const CreatureEffect& effect : effects) {
            if (now < effect.ends && effect.armor < part) {
                part = effect.armor;
            }
        }
        return part;
    }
};

/**
 * The cooldown of one ability of a character (stage 3.3a, world/abilities.h): a value of the
 * character in the world, with the character id as its key (roadmap, rule MP-1).
 */
struct AbilityCooldown {
    /** The start of the cooldown, its end, and the earliest end that a change leaves (the minimum time of the data). */
    WorldClock::time_point start{};
    WorldClock::time_point end{};
    WorldClock::time_point minEnd{};
    /**
     * The end of the cooldown of the client (stage 1.3b, I8): the client starts its own cooldown at a press that it plays,
     * and it does not play a press in it (live check I8). A perk or a node that the client knows changes it as the end;
     * a command of the server does not (the server has no message for the client cooldown).
     */
    WorldClock::time_point clientEnd{};
    /**
     * True while the summon of the cast lives (stage 3.3b-S2b: the Pylon; the action IceMagic_IcePylon_Cooldown of the game
     * data holds an indefinite cooldown, and the ordinary cooldown starts at the end of the summon). The owner gets the end
     * as all ones (client setter at image RVA 0x6923930: duration -1).
     */
    bool indefinite{false};
};

/** What gave a character a status (stage 3.3b-S1, world/statuses.h). */
enum class StatusSourceKind : std::uint8_t {
    /** A development command or an admin command. */
    command,
    /** A potion, a food, a dust (world/consumables.h). */
    consumable,
    /** The activation of a heart rune (world/runes.h). */
    rune,
    /** A perk of an equipped item: the status that an affix keeps (world/gear.h). Not on the wire: the client has the items. */
    gear,
    /** The caster effect of an ability row (SelfApplyStatusEffect of a learned node or of a perk). */
    ability,
    /** The end status of a status (OnEndStatusEffect). */
    status,
    /** The area of a spell entity (stage 3.3b-S2b: the Beacon): the status ends when the character leaves the area or the spell ends. */
    spell,
};

/** The source of a status of a character. The status system removes the statuses of a source together (a gear item). */
struct StatusSource {
    StatusSourceKind kind{StatusSourceKind::command};
    /** The item id, the perk, the ability row, or the status of the source. */
    std::string id;
    /** The paperdoll slot of an item of the gear. 0: none. */
    std::uint32_t slot{0};
    /** The entity of the character that gave the status (the caster). 0: none. */
    EntityId entity{0};

    bool operator==(const StatusSource&) const = default;
};

/**
 * One status effect of a status row on a character (stage 3.3b-S1): a food, a potion, a dust, the status of a heart
 * rune, the status of a perk of the gear, a caster effect of an ability. The status system (world/statuses.h) keeps the
 * statuses of a character with the character id as their key, and it puts a copy into its entity for the encoders and
 * the rules of the other systems. A character has one status of a status row at most (the maps of the client have one
 * entry for each status number); a new application of the same row changes its stacks and its end.
 */
struct ActiveStatus {
    /** The id text of the status row ("FoodCONT5", "WarHammer_WreckingBall_Fortify"). */
    std::string status;
    /** The number of the status on the wire (gamedata::StatusRow::index), and the key of the tray: the CRC-32 of the lower-case id. */
    std::uint16_t index{0};
    std::uint32_t trayKey{0};
    /** The potency (1, or the potency of a perk) and the stack count (StackMax). */
    float potency{1.0F};
    std::uint8_t stacks{1};
    /** The start and the end on the world clock. A permanent status (the gear) has no end. */
    WorldClock::time_point started{};
    WorldClock::time_point ends{};
    bool permanent{false};
    /** The time of the next tick of a status with a TickRate and a heal or a mana value. No value: no tick. */
    std::optional<WorldClock::time_point> nextTick;
    /** StopOnHitCount: the hits that the character can take before the status ends. 0: no such limit. */
    std::uint32_t hitsLeft{0};
    StatusSource source;
    /** A number of the status system: a new application or a new end has a new number (the due list). */
    std::uint64_t serial{0};
    /** The marks of the data for the wire: ShowInUITray (the tray of the owner), a public status (ShowInNameplates, AlwaysReplicate, ForceReplicateToRemotes: the observers get it), and false for a status of the gear (not on the wire). */
    bool tray{false};
    bool observed{false};
    bool replicated{true};
    /** ShowInNameplates: the icon of the status at the name plate (the compact map of an observer). */
    bool nameplate{false};
};

/** The cooldown of one consumable item of a character (stage 1.3b): the key of the client is the item id. */
struct ItemCooldown {
    /** The CRC-32 of the lower-case item id: the key of the map of the client (live check I7). */
    std::uint32_t item{0};
    WorldClock::time_point end{};
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

/** The values that the equipment and the effects give a character (stage 1.3b). The gear system sets them (world/gear.h). */
struct GearSummary {
    /** The part of the maximum health that the perks and the effects add (0.047: 4.7 percent more). */
    float healthPart{0.0F};
    /** The part of the damage of each hit (BaseDamage of a passive perk), and of the damage of a type (DMG<type>). */
    float baseDamage{0.0F};
    std::unordered_map<std::string, float> damage;
    /** The parts of the armor (PhysicalArmor, ElementalArmor), and the part of a damage type that the character does not take (ABS<type>). */
    float physicalArmor{0.0F};
    float elementalArmor{0.0F};
    std::unordered_map<std::string, float> absorption;
    /** The part of the damage that the character takes less (BaseDamageReduction). */
    float damageReduction{0.0F};
    /** The part of the maximum health that comes back in each second (a regeneration perk of an ability row). */
    float regeneration{0.0F};
    /**
     * The parts of the speed of a walk and of a sprint that the statuses add (MoveSpeedMod, SprintSpeedMod, with the
     * limits of their categories: Haste +0.4, Slow -0.5; stage 3.3b-S1). The check of the position reports uses them.
     */
    float moveSpeed{0.0F};
    float sprintSpeed{0.0F};
    /**
     * The critical chance and the critical damage parts of the statuses (CritChanceModifier, CritDamageModifier with the limits of
     * their categories; stage 3.3b-S5). The critical rule adds them at each hit (world/passives.h).
     */
    float critChance{0.0F};
    float critDamage{0.0F};
    /** The equip load of the armor (tenths: the client shows 38.75 as 3.8) and its class: 0 light, 1 medium, 2 heavy, 3 overburdened. */
    float equipLoad{0.0F};
    std::uint8_t loadClass{0};
};

/** The kind of a hit (stage 3.3b-S5: the conditions DamageIsMelee and DamageIsRanged of a passive rule). */
enum class HitKind : std::uint8_t {
    /** A melee contact (an attack, or a melee phase of an ability). */
    contact,
    /** A projectile. */
    projectile,
    /** An area or a spell (not melee and not ranged). */
    area,
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

/** The kind of the rules of a spell entity (world/spells.h). */
enum class SpellRole : std::uint8_t {
    /** The phases of a cast at its place (stage 3.3b-S2a: the vortex of Gravity Well). */
    area,
    /** A summon with health that shoots the spell of its AutoSpell component (stage 3.3b-S2b: the Pylon of the ice gauntlet). */
    summon,
    /** A trap that triggers one time at a creature in its detection volume (stage 3.3b-S2b: the trap of the musket). */
    trap,
    /** An area that follows its target and gives its statuses to the friends in it (stage 3.3b-S2b: the Beacon of the life staff). */
    aura,
};

/** A trigger of an entity of the slice of a spell (stage 3.3b-S2b): the events of the slice that start at its trigger run from `at`. */
struct SpellTrigger {
    std::string entity;
    WorldClock::time_point at{};
};

/**
 * The values of a spell entity (stage 3.3b-S2a, world/spells.h): its spell row, its caster, and its times on the world clock.
 * Only the spell system changes them. The record of the spell (states/spell_state.cpp) comes from them.
 */
struct SpellBody {
    /** The id text of the spell row ("Spell_GreatAxe_GravityWell_Vortex") and the CRC-32 of its lower-case id (the key of the row on the wire). */
    std::string spellId;
    std::uint32_t key{0};
    /** The slice of the spell (game data, spell-data.json). Null: the backend has no data of the slice, and no session observes the entity. */
    const gamedata::SpellSlice* slice{nullptr};
    /** The caster: its entity, its actor id (the caster reference of the Spell state), and its character. */
    EntityId caster{0};
    ActorId casterActor;
    std::string casterCharacter;
    /** The start and the end of the spell on the world clock. */
    WorldClock::time_point started{};
    WorldClock::time_point ends{};
    /** EndOnCasterDeath of the spell row (game data): the death of the caster ends the spell. */
    bool endsOnCasterDeath{false};
    /** The cast of the abilities system whose phases the spell runs (world/abilities.h). 0: none. */
    std::uint64_t cast{0};
    /** Stage 3.3b-S2b: the kind of its rules. */
    SpellRole role{SpellRole::area};
    /** The entity that the spell follows (field 6 of the Spell state: the Beacon on a character or a creature). 0: none. */
    EntityId attached{0};
    ActorId attachedActor;
    /** The row of the base vitals tables of a summon or a trap (game data): its armor. Null: no health. Entity::vitals has the health. */
    const gamedata::SpellVitalsRow* vitalsRow{nullptr};
    /** True from the start of its end chain (a death, the end of the life of a summon, a trigger): no shot, no damage, no trigger. */
    bool ending{false};
    /** The triggers of the entities of its slice, in their sequence (the death chain of the Pylon, the trigger of the trap). */
    std::vector<SpellTrigger> triggers;
};

/** Development aid: the slice of a probe entity (EntityKind::probe) and its interest distance. */
struct ProbeBody {
    /** The asset id of the slice (the 16 bytes of the asset catalog) and its sub id. */
    std::array<std::uint8_t, 16> asset{};
    std::uint32_t subId{2};
    /** A session starts to observe the probe at this distance (meters), and it stops 20 m farther. */
    float interestRadius{50.0F};
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
    /** Spells: the spell row, the caster, and the times (stage 3.3b-S2a). */
    SpellBody spell;
    /** Development aid: the slice of a probe entity. */
    ProbeBody probe;
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
    /**
     * The direction of the body of a character for its observers (degrees from north, to the west; stage 3.3b-S4: the
     * direction of a dash). No value: the observer view has the direction north (the rotation of the record of stage 0.6b).
     */
    std::optional<float> facing;
    Progression progression;
    Attributes attributes;
    SkillSet skills;
    /** The items, the equipment, and the coin of the character. The inventory system changes them (world/inventory.h). */
    Bag bag;
    Equipment equipment;
    Wallet wallet;
    /** The combat values of the character. The combat system changes them (world/combat.h). */
    CombatState combat;
    /** The values of the equipment and the effects of the character. The gear system changes them (world/gear.h). */
    GearSummary gear;
    /**
     * The speed multipliers of the run and of the sprint (the keys MoveSpeedMod and SprintSpeedMod of the speed table, slot
     * 43; stage 3.3b-S2a): 1 plus the speed parts of the gear summary, in the limits of the data. The gear system sets them.
     */
    float moveMultiplier{1.0F};
    float sprintMultiplier{1.0F};
    /** The life state of the character. The health system changes it (world/health.h). */
    Life life;
    /**
     * The cooldowns of the abilities of the character, with the ability id as the key. The
     * abilities system (world/abilities.h) keeps them with the character id as their key and
     * puts a copy here for the cooldown state of the owner.
     */
    std::map<std::uint32_t, AbilityCooldown> cooldowns;
    /**
     * The cooldowns of the consumable items of the character (stage 1.3b), for the field 3 of the cooldown state. The
     * consumable system keeps them with the character id as their key and puts a copy here.
     */
    std::vector<ItemCooldown> itemCooldowns;
    /** The status effects of the character (stage 3.3b-S1): a copy of the values of the status system (world/statuses.h). */
    std::vector<ActiveStatus> statuses;
    /** The heart rune charge of the character (stage 1.3b), 0 to 100 percent; a copy of the value of the rune system. */
    float runeCharge{0.0F};
    /**
     * The entitlements of the account of the character and the achievements of the character (stage 1.1b): the bit index
     * of each one in the vector of its state, in ascending order. The ownership system sets them (world/ownership.h).
     */
    std::vector<std::uint32_t> entitlements;
    std::vector<std::uint32_t> achievements;
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

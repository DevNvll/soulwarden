#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "gamedata/ability_data.h"
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
// An entity is the character of a player, a creature, a spell, or an object of the level data (EntityKind). A creature, a
// spell, and an object have no character, no account, and no session: their key is their entity id. A function for a
// character (World::find_character, each system with a character id) never finds them.

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
    /**
     * An object of the level data that a player can use (stage 2.5a, world/interactions.h): a lore page, a person. The client
     * makes the object itself from the level data; the entity carries the state of the server (Enabled, HasInteractors of
     * each Interact component) to each session near it, with the managed actor id of its placement. The interaction system
     * makes it when the state of the object first differs from the level data, and keeps it while the backend runs.
     */
    object,
    /**
     * A projectile of a creature in the air (stage 3.4f-A, world/projectiles.h): the shot of an archer. The projectile system makes it
     * at the release of the shot, each session near it gets its record (the metadata state of the projectile slice and the Projectile
     * state, type 16), and it goes away at the end of the flight. It has no character, no account, and no session.
     */
    projectile,
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
    /**
     * The Interactor state of a character (stage 2.5a, slot 26, type 3752): the G of the object of its committed interaction.
     * The owner and each observer get it.
     */
    interactor,
    /** The Interact states of an object (stage 2.5a, type 2930): Enabled and HasInteractors of each Interact component. */
    interact,
    /**
     * The PositionInTheWorld state of a server child (stage 2.5b, type 13, the slot of its slice): its place and its rotation. With
     * the metadata state it is the smallest record that shows a station, its prompt, and its window (live check L4).
     */
    objectPlace,
    /** The BuildableController state of a server child (stage 2.5b, type 1594): m_initialState of its slice (5: Complete). */
    buildable,
    /** The Ownership state of a server child (stage 2.5b, type 3217): the permission rule (live checks L5, L6). */
    ownership,
    /**
     * The Projectile state of a projectile entity (stage 3.4f-A, type 16, the slot of the Projectile component of its slice): the
     * launch (the place, the velocity, the time, the owner, the attack, the ammunition, the damage table and row), and at its end the
     * collision (the contact, its time, the collided bit).
     */
    projectile,
    /**
     * The Stamina state of a character (stage 3.4f-B, slot 42, type 4297, world/stamina.h): the stamina, its maximum, the winded time and
     * the delay of the regeneration that are left, and the two multipliers. Only the owner gets it; the first record of a character with
     * its full stamina has no fragment.
     */
    stamina,
    /**
     * The Gathering state of a character (stage 3.5b.1, slot 36, type 81; docs/systems/chests.md): the progress of the timed open of a
     * chest (live check L3: the ring of the prompt). Only the owner gets it (the remote player slice has no slot 36).
     */
    gathering,
    /**
     * The LootTracker state of a character (stage 3.5b.1, slot 33, type 982): the loot claims of the character on the chests near it
     * (live check L2: a Looted entry removes the prompt of the chest). Only the owner gets it. Stage 3.5b.2: also the entries of its loot
     * bags (Entity::bagTracker).
     */
    lootTracker,
    /**
     * The selected Container of a character (stage 3.5b.2, slot 76, type 1755; docs/systems/loot-bags.md): the items of the loot bag that
     * the character opened (live check L5: the panel of the bag shows them). Only the owner gets it.
     */
    selectedContainer,
    /**
     * The GlobalStorage state of a character (stage 3.5b.2, slot 77, type 2938): a map of the actor of each loot bag of the character to
     * its items (live check L5: the item list under the prompt of a bag). Only the owner gets it.
     */
    globalStorage,
    /** The LootDrop state of a loot bag (stage 3.5b.2, type 2027, the slot of its slice): m_rarityLevel. */
    lootDrop,
    /**
     * The GatherableController state of a gathering node or of the body of a creature (stage 3.6a, type 12, the slot of its slice;
     * docs/systems/gathering.md): Healthy 4 or Depleted 8. Each observer gets it (the node state is shared: research Q1).
     */
    gatherable,
};

/**
 * The Gathering state of a character (stage 3.5b.1; MB::GatheringComponentReplicatedState, type 81, slot 36; research gathering-stage.md
 * Q2): the values of group 1. The client applies fields 0 to 3 when the field `reference` changes, and fields 6 and 7 when the field
 * `syncReference` changes (apply 0x4905130): the chest system adds one to both at each change. Live check L3 of stage 3.5b.1: the ring
 * of the prompt runs `remaining` / `speed` seconds; the status Done (3) and Failure (2) end the gather in the client.
 */
struct GatherProgress {
    /** None 0, Success 1 (the open runs), Failure 2 (a cancel), Done 3 (the completion). */
    std::uint8_t status{0};
    /** m_amtRemaining: the time that is left (seconds). */
    float remaining{0.0F};
    /** m_gatherEfficiency: the speed of the progress (1: the time of the data). */
    float speed{1.0F};
    /** m_numGathering. */
    std::uint8_t gatherers{0};
    std::uint32_t reference{0};
    /** m_isGathering and m_gatherableGDEID (the G of the chest; 0x00000000FFFFFFFF: none). */
    bool active{false};
    std::uint64_t target{0x00000000FFFFFFFFULL};
    std::uint32_t syncReference{0};
};

/**
 * One entry of the LootTracker state of a character (stage 3.5b.1; type 982, slot 33, field 0, a map; research chests-ground-loot.md
 * Q2: the 58-byte entry, readers 0x3727A00 and 0x3727C60, writer 0x375AFA0).
 */
struct LootTrackerEntry {
    /** The actor id of the chest (the key of the map). */
    std::array<std::uint8_t, 16> actor{};
    /** InstancedLootState: 0 Initial, 1 Rolled, 2 Looted, 3 RegeneratedForMission, 4 NotOnRequiredQuest, 5 Locked, 6 Unlocked. */
    std::uint32_t state{0};
    /** InstancedLootType: 0 POILoot. */
    std::uint32_t kind{0};
    /** The reset time and the looted time: server time in microseconds (live check L8: the client does not end a claim at it). */
    std::uint64_t resetTime{0};
    std::uint64_t lootedTime{0};
    bool privileged{false};
    state::WorldPosition position;
    std::uint32_t tag{0};
    std::uint8_t selector{0};

    bool operator==(const LootTrackerEntry& other) const {
        return actor == other.actor && state == other.state && kind == other.kind && resetTime == other.resetTime
               && lootedTime == other.lootedTime && privileged == other.privileged && position.x == other.position.x
               && position.y == other.position.y && position.z == other.position.z && tag == other.tag && selector == other.selector;
    }
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
     * The base maximum of the mana (ManaMax of the base vitals of a player, before the MaxMana parts; stage 3.5b.2). The vitals state sends
     * it: the client multiplies the ManaMax of the vitals state with its own MaxMana parts (live check of the final run of stage 3.5b.2: a
     * vitals state with 110 gave the client the maximum 121).
     */
    float manaBase{100.0F};
    /**
     * The mana regeneration (stage 1.3b, I14): the mana comes back from this time on, at the rate of the base vitals of a
     * player (a spend sets the delay of the data). No value: the mana is full, or the backend has no rate.
     */
    std::optional<WorldClock::time_point> manaRegenFrom;
    /** The mana in one second of the regeneration (the base vitals of a player; the mana state sends it while the mana is not full). */
    float manaRegenRate{0.0F};
    /**
     * The factor of the mana regeneration (stage 3.5b.2): 1 plus the ManaRate parts of the threshold abilities, the perks, and the effects
     * (the client predicts the gain with the same rate, stat 0x30). The progress system sets it with the maximum.
     */
    float manaRateFactor{1.0F};
    /**
     * The stamina (stage 3.4f-B, world/stamina.h): the regeneration starts at this time (a payment sets the delay of the data; no value:
     * the stamina is full or has no rule), and it is off until `staminaHeldUntil` (a dodge, to its mark CanTransition). At 0 the
     * character is winded to `windedUntil` and exhausted until its stamina is full again (no dodge in that time).
     */
    std::optional<WorldClock::time_point> staminaRegenFrom;
    WorldClock::time_point staminaHeldUntil{};
    WorldClock::time_point windedUntil{};
    bool exhausted{false};
    /**
     * The world revision of the last heal of the character that shows its number on the screen of its owner (stage 3.3b-S6, live
     * check L3): the vitals state of the owner after it has bit 1 of healthChangeFlags (the client shows the gain from its own old
     * health). 0: none.
     */
    Revision healShown{0};
    /**
     * The last heals with a number (code review of stage 3.3b-S6, finding S6-R7): the world revision, the health before the heal, and the
     * health after it. The vitals state of the owner sends each one as its own state with bit 1 (the number is exactly its gain), and a
     * change with no number before or after it as a state with no bit 1. The newest last. A heal stays for kHealStepKeep on the world
     * clock, also when more heals come before the next state of the owner (code review of stage 3.3b-M1, finding M1-R8: nine life
     * steals of one strike lost the first number with a limit of 8); kHealSteps is a bound of the memory only (the legal work of 2 s:
     * 64 claims each second of 32 targets at most, and the ticks; a decision of an agent).
     */
    struct HealStep {
        Revision revision{0};
        float from{0.0F};
        float to{0.0F};
        std::chrono::steady_clock::time_point time{};
    };
    static constexpr std::size_t kHealSteps = 512;
    static constexpr std::chrono::milliseconds kHealStepKeep{2000};
    std::vector<HealStep> healSteps;
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
    /**
     * The shot of stage 3.4f-A: true while the action of its basic shot plays (its generation and its end are attackGeneration and
     * attackEnds), the time from which it can start its next shot, its walk back to the middle of its band, and the time before it walks
     * back again. `closing` (stage 3.4f-B): it walks into its band from farther; it walks on to the hold distance of the band.
     */
    bool shooting{false};
    WorldClock::time_point shotReady{};
    bool retreating{false};
    bool closing{false};
    WorldClock::time_point retreatPauseUntil{};
    /**
     * The area attacks of stage 3.4f-C: the time from which the creature can start its next area attack, and the index of its next one
     * (the usable area attacks of its slice in turn). The action of an area attack plays as the action of a shot (`shooting`).
     */
    WorldClock::time_point areaReady{};
    std::uint32_t nextArea{0};
    /** The status actions of stage 3.4f-D: the time from which the creature can start its next status action (its heal, its buff). */
    WorldClock::time_point statusReady{};
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
 * Stage 3.4f-D: the heals and the buffs of a creature (a status action of its slice: the self heal of the Inquisitor Cleric, the Howl of
 * the Evil Knight hound) are effects too: a heal at each tick of the row, the DMG<type> parts of the row, a haste.
 */
struct CreatureEffect {
    /** The id text of the status row ("WarHammer_Shockwave_Stun"). */
    std::string status;
    /** The status row (game data; stage 3.4f-D): its typed values (DMG<type>), its categories (a dispel), its tick. Null: none. */
    const gamedata::StatusRow* row{nullptr};
    /** The number of the effect in the creature system (stage 3.4f-D): the key of its entries of the due list. */
    std::uint64_t serial{0};
    /**
     * The heal of one tick (stage 3.4f-D): Health plus HealthModifierPercent of the row times the maximum health, times the potency (the
     * self heal of the Cleric: 0.045). The tick comes each `tickRate`, the first one at the next tick of the world (DelayInitialTick: one
     * TickRate later; the rule of the statuses of a character). 0: no heal. `nextTick` keeps the time of the next tick on the cadence
     * of the effect also after the last tick before the end: a later end (the same row again) queues this tick (review SU-R1).
     */
    float healPart{0.0F};
    float healFixed{0.0F};
    std::chrono::milliseconds tickRate{0};
    std::optional<WorldClock::time_point> nextTick;
    /**
     * Stage B1: the damage of one tick of a damage over time with its icon (the burn of Flame Arrow: HealthModifierDamageBased of the row times
     * the damage base of the rule that gave it), its damage type (DamageType of the row), and the rune charge of the source at a tick
     * (SourceRuneChargeOnTick). The source (a character) is the attacker of each tick. 0: no damage.
     */
    float tickDamage{0.0F};
    std::string tickType;
    float tickCharge{0.0F};
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
    /** The part of the damage of the creature that the effect adds (-0.1: a weaken of 10 percent): DMGStandard of the row. */
    float damage{0.0F};
    /** True: the row has a DMG<type> part (stage 3.4f-D): the part of a hit is the part of its type (Creature::damage_factor). */
    bool typedDamage{false};
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
    /**
     * The time of the latest first record of the creature for a session (a slice that needs the spawn action; code review of stage
     * 3.4f-B, finding CS-R4): that session shows the spawn action, and the action that plays comes kSpawnLead later. The shot and the cast
     * of the creature wait to the end of that lead (Creatures::first_record).
     */
    WorldClock::time_point recordedAt{};
    /** True from the moment at which the health of the creature is 0. A dead creature does not get health again. */
    bool dead{false};
    /**
     * The skinning of the body (stage 3.6a, world/gathering.h): the slot of the GatherableController component of the slice (0: no
     * state of the server) and the state (4 Healthy: the body can be skinned; 8 Depleted: it was skinned).
     */
    std::uint32_t skinSlot{0};
    std::uint8_t skinState{0};
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
    /**
     * The idle record of the creature (user test 2, UT2-2): the action "Idle" of its slice (CreatureSlice::idleAction), with a new start
     * count at each end of an action of the server, of a walk, and of a stun (Creatures::rest). A session sends it when the creature has
     * no action of the server and no locomotion: the client plays the idle (a looping fragment) and does not keep the last pose of the
     * action that ended, and a later state with no scope fields keeps a valid record (the scope fields with no record make the client end
     * its own idle at each tick, client code 0x2A2BC20). An action and a walk clear it. No value: the creature had no action yet, or its
     * slice has no idle action (the scope fields with no record).
     */
    std::optional<CreatureAction> idle;
    /**
     * The end of the hit stun of the last hit (user test 2, UT2-9): HitStun_AI of the damage row, in frames of 1/30 s. Until this time
     * the behavior does not move the creature, does not turn it, and starts no attack: the reaction of the hit plays. No value: none.
     */
    std::optional<WorldClock::time_point> hitStunUntil;
    /**
     * The chained reactions of the creature (user test 2: Chained_Reaction_Count of the action Reaction; a reaction that starts while the
     * reaction before it plays is one more in the chain, a decision of an agent: the count is a value of the behavior tree, which is not in
     * the game files) and the end of the reaction limit (the tag ReactionLimiter_Active: the reaction of the third chained reaction and the
     * status AI_Reaction_Limiter of 2 s after it, game data). While it holds, a light or heavy attack plays no reaction.
     */
    std::uint32_t reactionChain{0};
    std::optional<WorldClock::time_point> reactionLimitUntil;
    /**
     * The start count (CreatureAction::generation) of the reaction that holds the reaction limit while it plays (code review of user test 2,
     * finding U2-R3): the status AI_Reaction_Limiter of 2 s starts at its real exit (EffectsToApplyOnExit of the action Reaction: its
     * natural end, a step, or a new action), not at its natural end.
     */
    std::optional<std::uint16_t> reactionLimitAction;
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
    /**
     * @return The factor of the speed of the creature: the strongest slow (a rule of this project), times 1 plus the strongest haste (stage
     *         3.4f-D: the MoveSpeedMod 0.2 of the Howl of the Evil Knight hound; a decision of an agent: one haste counts, as one slow).
     */
    [[nodiscard]] float speed_factor(WorldClock::time_point now) const {
        float slow = 1.0F;
        float haste = 1.0F;
        for (const CreatureEffect& effect : effects) {
            if (now < effect.ends) {
                slow = std::min(slow, effect.speed);
                haste = std::max(haste, effect.speed);
            }
        }
        return std::max(slow, 0.0F) * haste;
    }
    /**
     * @return The factor of the damage of the creature for a hit of a damage type: 1 plus the sum of the parts of its effects, not less than
     *         0. The part of an effect is the DMG<type> of its row (stage 3.4f-D, the rule of the damage of a character: the DMG part of the
     *         type of the hit; the Howl of the hound: 0.1 for each type), or the DMGStandard of the row for a row with no DMG part of a type
     *         (an effect of an ability with only that value). An empty type: DMGStandard (stage 3.3a).
     */
    [[nodiscard]] float damage_factor(WorldClock::time_point now, std::string_view type = {}) const;
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

inline float Creature::damage_factor(WorldClock::time_point now, std::string_view type) const {
    float part = 0.0F;
    for (const CreatureEffect& effect : effects) {
        if (now >= effect.ends) {
            continue;
        }
        if (effect.row != nullptr && effect.typedDamage && !type.empty() && type != "Standard") {
            std::string column = "DMG";
            column += type;
            part += effect.row->cells.number(column);
        } else {
            part += effect.damage;
        }
    }
    return part < -1.0F ? 0.0F : 1.0F + part;
}

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

/**
 * The kind of a heal (stage 3.3b-S6, world/health.h): it selects the received modifiers of the target (the columns of the status
 * data: HealMod for each heal, ConsumableHealMod, NonConsumableHealMod, DmgPctToHealthHealMod) and the number on the screen.
 */
enum class HealKind : std::uint8_t {
    /** A heal of an ability or of a status of an ability (the heal base of the healer): NonConsumableHealMod. */
    ability,
    /** A potion, a food, a dust (a status of a consumable): ConsumableHealMod. */
    consumable,
    /** A part of the damage of a hit (DmgPctToHealth): NonConsumableHealMod and DmgPctToHealthHealMod. */
    lifeSteal,
    /** A status of the gear (the regeneration of an earring): NonConsumableHealMod, no number. */
    regeneration,
};

/**
 * The source of a heal of an ability (stage 3.3b-S6): the healer and the heal base of the healer at the cast (the weapon of the cast
 * at its gear score and the Focus of the healer; a decision of the user, world/health.h). A status keeps it: its ticks heal with this
 * base (the client has a cached heal scaling value for each source of a status: StatusEffectSource 0x5CB7F20, m_healScalingValue).
 */
struct HealSource {
    /** The entity and the character of the healer. 0: no healer (a consumable, the gear). */
    EntityId healer{0};
    std::string healerCharacter;
    /** The heal base: the base damage of the weapon at its gear score times the scaling of Focus. 0: none. */
    float base{0.0F};
    /** The damage row of the spell of the heal (LifeStaff_DivineEmbrace): the condition DamageTableRow of a rule with OnHealed. */
    std::string row;
    /** The ability table of the weapon of the heal (its progression id): the learned nodes whose OnHealed rules count. 0: none. */
    std::uint32_t table{0};

    bool operator==(const HealSource&) const = default;
};

/**
 * The source of the damage of a status (stage 3.4f-C: the poison of the mushroom of a creature): the creature, its core damage at the
 * application (a snapshot: the damage of its level and kind, gamedata::CreatureData::damage), and its level. A status of the data with a
 * negative HealthModifierPercent or HealthModifierDamageBased hurts its character at each tick with it (world/statuses.h).
 */
struct DamageSource {
    EntityId attacker{0};
    float base{0.0F};
    std::uint32_t level{0};

    bool operator==(const DamageSource&) const = default;
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
    /** Stage 3.3b-S6: the source of the heal of a status with HealthModifierDamageBased (the healer and its heal base at the cast). */
    HealSource heal;
    /** Stage 3.4f-C: the source of the damage of a status with a negative health term (a creature and its core damage at the application). */
    DamageSource damage;
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
    /**
     * The mana parts of the perks and the effects (stage 3.5b.2): MaxMana and ManaRate. The progress system adds the parts of the threshold
     * abilities of the attributes (Foc_Bonus_100_1: MaxMana 0.1, ManaRate 0.2) and sets the maximum and the regeneration factor.
     */
    float manaPart{0.0F};
    float manaRatePart{0.0F};
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
    /**
     * The heal parts of the statuses (stage 3.3b-S6, with the limits of their categories): the received parts of a heal that the
     * character takes (HealMod for each heal; ConsumableHealMod, NonConsumableHealMod, DmgPctToHealthHealMod for one kind; the game
     * texts: "reduces healing taken", "increased healing from non-consumables and lifesteal"), and the outgoing part of a heal that the
     * character gives (HealScalingValueMultiplier; live check L9: "outgoing healing is reduced by 3% per stack").
     */
    float healTaken{0.0F};
    float healConsumable{0.0F};
    float healNonConsumable{0.0F};
    float healLifeSteal{0.0F};
    float healOutgoing{0.0F};
    /** The StaminaRate part of the statuses (stage 3.4f-B): the regeneration of the stamina is 1 + this part times its rate. */
    float staminaRate{0.0F};
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
    /**
     * The hits of the last cast of each ability whose row has TrackHitCount (stage 3.3b-M2): the ability (the CRC-32 of its id text)
     * and the count, 255 at most. The abilities system sets them (Abilities: 0 at an accepted press, one more at each accepted hit of
     * a strike of its fragment), and the ability state of the owner (slot 46, fields hitDataAbilityIds and hitDataNumHits) has them:
     * the client reads them for the condition IfAbilityNumHits (the exit of Whirlwind with Unending Winds). An ability stays in the
     * list with the count 0 after its first cast.
     */
    std::vector<std::pair<std::uint32_t, std::uint8_t>> abilityHits;
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
    /**
     * Stage 3.3b-M2: a shield that follows its caster and ends each projectile of a hostile shooter in its radius (the projectile shield
     * of Maelstrom with Absorb: Spell_Upgrade_GreatAxe_Maelstrom_ProjectileConsume).
     */
    shield,
    /** Stage 3.3b-M2: the picture of a spell with no rule of the server (the ground of Mighty Gavel). */
    visual,
    /**
     * Stage 3.4f-C: a ground area of a creature whose AreaStatusEffectComponent gives its statuses to the enemies in its volume (the
     * mushroom of the Dryad Shaman: AI_Poison_55; the area of the Ancient Guardian mage).
     */
    statusArea,
    /** Stage 3.4f-C: a ground area of a creature with a short damage window: one hit of its spell row (the lightning strike of the Undead Shaman). */
    strike,
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
    /**
     * Stage 3.4f-D: the rotation of a ground area of a creature, the direction of its caster at the cast (degrees, 0: north; client code
     * 0x676AAF0: the rotation of the spawn request is the rotation of the caster). The Spell state and the PositionInTheWorld state have
     * it (the shockwave of the Evil Knight goes to the front of the knight). No value: the identity rotation.
     */
    std::optional<float> yaw;
};

/** The reference of an object in a request of a client (stage 2.5a): its actor id, its G, and the facet target of a bound. */
struct InteractionReference {
    std::array<std::uint8_t, 16> actor{};
    std::uint64_t group{0};
    std::uint64_t target{0};

    bool operator==(const InteractionReference&) const = default;
};

/**
 * The active interaction of a character (stage 2.5a, world/interactions.h): one committed use of an object (a lore page that
 * the client reads, a conversation, a station). Only the interaction system changes it. It is a value of the world, not of the
 * database: a restart of the backend or a new world entry has no active interaction (research interaction-stage.md, the temporary
 * state).
 */
struct ActiveInteraction {
    /** The object of the interaction data (a level object) or the template (a server child), the bound, and the option row. */
    std::uint32_t object{0};
    std::uint32_t bound{0};
    std::uint32_t option{0};
    InteractionReference reference;
    std::uint8_t action{0};
    /** The start on the world clock, and the number of the use (a new use has a new number). */
    WorldClock::time_point started{};
    std::uint64_t serial{0};
    /** The entity of a server child (stage 2.5b). 0: a level object. */
    EntityId instance{0};
    /** The generation of the source: the number of the server child (a new child has a new number; a level object: 0). */
    std::uint64_t generation{0};
    /** A timed use (an option with a Delay Time, stage 2.5b): its end on the world clock, and its completion (one time). */
    std::optional<WorldClock::time_point> due;
    bool completed{false};
};

/**
 * A service that a source gave a character (stage 2.5b, the base of the shops 5.1 and the transmog 5.6; research Q4,
 * "NPC services"): a later operation with no source in its message (a purchase 3741 has no NPC id) checks it. Only the interaction
 * system changes it. It is a value of the world: a restart or a new world entry has none.
 */
struct ServiceGrant {
    /** The kind of the service ("shop") and the ids that the source offers (the offer set of the source). */
    std::string kind;
    std::vector<std::uint32_t> ids;
    /** The entity of the character at the grant: a new world entry has a new entity id. */
    EntityId worldEntry{0};
    /** The source: the object of the data or the template, its bound, its server child and generation, its reference. */
    std::uint32_t object{0};
    std::uint32_t bound{0};
    EntityId instance{0};
    std::uint64_t generation{0};
    InteractionReference source;
    /** The revision of the offer of the source (the conversation state of a later stage). */
    std::uint64_t revision{0};
    WorldClock::time_point openedAt{};
    /** An end of the grant on the world clock (a later stage; the option data has no timeout: no value). */
    std::optional<WorldClock::time_point> deadline;
};

/** One key of the GlobalStorage map of a character (stage 3.5b.2, slot 77): the actor of a loot bag and the items of its part. */
struct StorageKey {
    std::array<std::uint8_t, 16> actor{};
    std::vector<ItemInstance> items;

    bool operator==(const StorageKey& other) const {
        if (actor != other.actor || items.size() != other.items.size()) {
            return false;
        }
        for (std::size_t index = 0; index < items.size(); ++index) {
            const ItemInstance& a = items[index];
            const ItemInstance& b = other.items[index];
            if (a.instance != b.instance || a.quantity != b.quantity || a.index != b.index) {
                return false;
            }
        }
        return true;
    }
};

/** The server state of one Interact component of an object entity (stage 2.5a): a copy of the values of the interaction system. */
struct ObjectBound {
    /** The slot of the state 2930 in the record (game data). */
    std::uint32_t slot{0};
    /** Enabled (field 0): false hides the prompt of the client (live check L2). */
    bool enabled{true};
    /** HasInteractors (field 1): the number of characters that use the bound. */
    std::uint32_t users{0};
};

/** The values of an object entity (EntityKind::object, stage 2.5a; a server child: stage 2.5b). */
struct ObjectBody {
    /** The index of the object in the interaction data (a level object), or of its template (a server child). */
    std::uint32_t index{0};
    /** The slice asset id of the placement and its sub id: the metadata state of the record. */
    std::array<std::uint8_t, 16> asset{};
    std::uint32_t subId{2};
    std::vector<ObjectBound> bounds;
    /**
     * True: a server child (stage 2.5b): an actor of the id allocator, and the client shows it only after the record of the
     * server, with its place (live check L4). False: an object of the level data (the client has it).
     */
    bool spawned{false};
    /** The slot of its PositionInTheWorld state and its rotation (x, y, z, w). 0: no place state (a level object). */
    std::uint32_t positionSlot{0};
    std::array<float, 4> rotation{0.0F, 0.0F, 0.0F, 1.0F};
    /** The BuildableController state (slot, m_initialState) and the Ownership state (slot, permission rule). Slot 0: none. */
    std::uint32_t buildableSlot{0};
    std::uint8_t buildableState{0};
    std::uint32_t ownershipSlot{0};
    std::uint8_t permissionRule{0};
    /**
     * A private object (stage 3.5b.2: a loot bag): only a session whose character is in this list observes it (the eligible characters;
     * docs/systems/loot-bags.md). Empty: each session near it.
     */
    std::vector<std::string> viewers;
    /** The LootDrop state of a loot bag (stage 3.5b.2): the slot of its slice (0: none) and m_rarityLevel. */
    std::uint32_t lootDropSlot{0};
    std::uint32_t rarity{0};
    /**
     * The node state of a gathering node (stage 3.6a, world/gathering.h): the slot of its GatherableController component (0: not a node
     * with a state of the server) and the state (4 Healthy, 8 Depleted; client enum 0x3378D60).
     */
    std::uint32_t nodeSlot{0};
    std::uint8_t nodeState{0};
};

/** Development aid: the slice of a probe entity (EntityKind::probe) and its interest distance. */
struct ProbeBody {
    /** The asset id of the slice (the 16 bytes of the asset catalog) and its sub id. */
    std::array<std::uint8_t, 16> asset{};
    std::uint32_t subId{2};
    /** A session starts to observe the probe at this distance (meters), and it stops 20 m farther. */
    float interestRadius{50.0F};
    /**
     * The actor id of the probe (stage 2.5a, live check L2: the record of a level placement with its managed actor id). No value:
     * an id of the id allocator.
     */
    std::optional<ActorId> actor;
};

/**
 * The body of a projectile entity (stage 3.4f-A, EntityKind::projectile): what its Projectile state has (research creature-ranged.md
 * R0, live checks L1 and L2). The projectile system sets it; the encoder of the state reads it.
 */
struct ProjectileBody {
    /** The projectile slice (game data: the asset, the sub id, the slot of the Projectile component). Never null for an entity. */
    const gamedata::ProjectileSliceData* slice{nullptr};
    /** The number of the flight in the projectile system (a log line, the development list). */
    std::uint64_t number{0};
    /** The creature that shot it: the owner field of the state (its entity id). */
    EntityId source{0};
    /** The attack index, the ammunition key, the damage table key, and the row of the release (RangedRelease). */
    std::uint32_t attackIndex{0};
    std::uint32_t ammoKey{0};
    std::uint32_t tableKey{0};
    std::uint16_t rowIndex{0};
    /** The launch: the place, the velocity (m/s), and the time on the world clock (the time anchor of the state). */
    state::WorldPosition origin;
    std::array<float, 3> velocity{};
    WorldClock::time_point launched{};
    /**
     * The end of the flight at a contact (a character, a wall or the ground, a shield): the contact place and its time. The record then
     * has the collision state (the collided bit 0x01). No value: in the air, or an end with no contact (the range, the life time, the
     * end of the source).
     */
    std::optional<state::WorldPosition> contact;
    WorldClock::time_point contactAt{};
    /** True after the end of the flight: no new session observes it, and the projectile system removes it at its hold time. */
    bool ended{false};
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
    /** Objects (stage 2.5a): the object of the interaction data and the state of its bounds. */
    ObjectBody object;
    /** Projectiles of a creature (stage 3.4f-A): the launch and the end of the flight. */
    ProjectileBody shot;
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
    /**
     * The committed interaction of the character (stage 2.5a): a copy of the value of the interaction system. No value: none.
     * `interactionEnded` is the world revision of the last end of one (the Interactor state of a session that got the begin
     * before it has the empty value first: research K8).
     */
    std::optional<ActiveInteraction> interaction;
    Revision interactionEnded{0};
    /** The service that a source gave the character (stage 2.5b): a copy of the value of the interaction system. No value: none. */
    std::optional<ServiceGrant> serviceGrant;
    /** The progress of the open of a chest (stage 3.5b.1): the chest system changes it. */
    GatherProgress gathering;
    /** The loot claims of the character on the chests near it (stage 3.5b.1): a copy of the values of the chest system. */
    std::vector<LootTrackerEntry> lootTracker;
    /** The LootTracker entries of the loot bags of the character (stage 3.5b.2): a copy of the values of the loot bag system. */
    std::vector<LootTrackerEntry> bagTracker;
    /** The items of the loot bag that the character opened (stage 3.5b.2, slot 76): the loot bag system sets them. */
    Bag selectedContainer;
    /** The items of each loot bag of the character (stage 3.5b.2, slot 77): the loot bag system sets them. */
    std::vector<StorageKey> storageMap;
    /** Development aid: fragments of a command, one for each slot at most. Empty in a product run. */
    std::vector<RawFragment> rawFragments;

    // Persistence of the position. Only the world object uses these members.
    /** True when the position is not the position of the row. */
    bool positionDirty{false};
    /** True while the character moves: the save rule then needs a larger distance. */
    bool moving{false};
    /**
     * The yaw of the camera of the client of a character (radians from north, positive to the left; stage 3.3b-M2: the box of Blood Lust in
     * front of the look). The world keeps the value of the last accepted report.
     */
    float look{0.0F};
    /**
     * The velocity of a character from its accepted positions (stage 3.4f-A: the lead of a creature shot; World::velocity_of): the
     * place and the time of the last sample, and the velocity from the sample before it (m/s).
     */
    state::WorldPosition velocitySample;
    WorldClock::time_point velocitySampleAt{};
    std::array<float, 3> velocity{};
    WorldClock::time_point velocityAt{};
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

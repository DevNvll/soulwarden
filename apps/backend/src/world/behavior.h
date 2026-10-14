#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_set>

#include "world/entity.h"
#include "world/ids.h"
#include "world/load_meter.h"
#include "world/world_clock.h"

// The behavior of a creature: it gets a target, goes to it, attacks it, and goes back to its
// home. It is a part of the creature system (world/creatures.h): only that system changes a
// creature.
//
// The original behavior ran on the server of the game, and the game files do not have its
// rules (the behavior trees). Thus the behavior is a design of this project (protocol notes
// 4.17): simple rules, the same for each creature family, with the values of the game data
// where the game data has a value (the vision, the leash, the speed, and the attacks of the
// slice of a creature: gamedata/creature_data.h). Each value that the game data does not give
// is a constant of the namespace behavior_rules below.
//
// The rules:
//
//   Sense.  A creature at rest gets a character as its target when the character is in its
//           vision: not farther than the vision distance of its slice, in the angle of its
//           slice to each side of its direction, and with no solid of the navigation data
//           between the two (a wall stops the vision; a low object does not). A creature gets
//           the character that hurts it as its target, for each distance and direction. A
//           creature has one target.
//   Way.    A creature with a target goes to it in steps, with the run speed of its slice,
//           until the target is in the reach of its next attack with a free line to it. The
//           place of each step comes from the navigation of the world (world/navigation.h): a
//           path of the navigation data, around a wall and up stairs, and the height of a step
//           is the height of its floor. A creature that stands on no free floor of the data
//           does not leave its place: it turns, and it attacks in its reach. A creature whose
//           target has no way to it loses the target and goes home, and its vision does not
//           take that character again for some seconds.
//   Attack. In its reach the creature starts an attack of its slice: an action that each
//           observer plays. The world tests each contact of the attack at its time on the
//           world clock: the target is in the world, lives, and is in the reach and in the
//           angle of the contact. One contact that passes is one damage result
//           (world/health.h). A target that went away gets no damage. After the action and a
//           pause the creature decides again. Two contacts of one attack that are 0.15 s or
//           less from each other are one hit. A slice with light attacks and heavy attacks
//           (gamedata/creature_data.h) makes its light attacks in their sequence and one heavy
//           attack after each three of them, and the pause after a heavy attack is longer.
//   Crowd.  A creature does not end a step of its way to its target at the place of a
//           different creature: it stops 0.5 m before it, or it waits a step, and it attacks
//           from its place when its target is in the reach of its attack. The way home has no
//           such rule.
//   Shelter. For 10 s after a character came to a place (a world entry, a travel, a respawn)
//           the vision of no creature takes it as a target, and a creature that had it as its
//           target loses it. The time starts when its client reports a position from the
//           place. A creature that the character hurts gets it as its target in this time too.
//   Back.   A creature loses its target when the target is dead, left the world, is ignored
//           by the creatures, or is too far, and when the creature is farther from its home
//           than its leash. It then goes to its home, takes no target by its vision on the
//           way, and has its full health at its home.
//
// A creature at rest needs no time: only the creatures of the set `m_active` (a target, the
// way back) are in the step, and the vision rule runs from the characters, with the position
// index of the world. World thread only.

namespace soulwarden::backend::world {

class World;
class Creatures;

/** The constants of the behavior that the game data does not give (rules of this project). */
namespace behavior_rules {
/** The behavior decides and moves each creature one time in this interval of the world clock. */
inline constexpr std::chrono::milliseconds kStepInterval{200};
/** A step is not longer than the way of this time: the world clock of a test, or a slow tick, can jump. */
inline constexpr std::chrono::milliseconds kLongestStep{1000};
/** The pause between the end of an attack and the next attack of a creature, and the pause after a heavy attack. */
inline constexpr std::chrono::milliseconds kAttackPause{1500};
inline constexpr std::chrono::milliseconds kHeavyPause{2500};
/** A creature with light attacks and heavy attacks makes one heavy attack after this number of light attacks. */
inline constexpr std::uint32_t kLightAttacks = 3;
/**
 * A creature does not end a step of its way nearer than `kCrowdDistance` to a different
 * creature, in the last `kCrowdRange` of its way to the reach of its attack (meters).
 */
inline constexpr float kCrowdDistance = 0.5F;
inline constexpr float kCrowdRange = 3.0F;
/**
 * A creature that waited this number of steps for a creature with a different target, or with
 * no target, goes on through it: a creature at rest in a door, and two creatures that go to
 * each other on one path, do not stop a fight for ever. Creatures with the same target wait
 * for each other.
 */
inline constexpr std::uint32_t kCrowdPatience = 10;
/**
 * The vision of no creature takes a character as a target for this time after the character
 * came to a place: a world entry, a travel, a respawn. The time starts when the client of the
 * character reports a position from the place, and it ends `kShelterLongest` after the
 * arrival at the latest (a client that reports nothing).
 */
inline constexpr std::chrono::milliseconds kShelterTime{10000};
inline constexpr std::chrono::milliseconds kShelterLongest{60000};
/** A creature stops this far inside the reach of its next attack, and not nearer than `kNearest` (meters). */
inline constexpr float kReachMargin = 0.6F;
inline constexpr float kNearest = 1.0F;
/** A contact hurts a target that is not farther than its reach plus this (meters), and whose height is this near. */
inline constexpr float kContactTolerance = 0.3F;
inline constexpr float kContactHeight = 3.0F;
/** A contact hurts a target in this angle to each side of the direction of the creature (degrees). */
inline constexpr float kContactAngle = 70.0F;
/** A creature loses a target that is farther than this (meters): the target travelled away. */
inline constexpr float kLoseDistance = 60.0F;
/** The leash of a slice whose data has no leash distance (meters). */
inline constexpr float kDefaultLeash = 40.0F;
/** A creature that does not walk loses a target that is farther than this (meters), when its slice has no vision distance for a fight. */
inline constexpr float kDefaultCombatSight = 30.0F;
/** A creature is at its home when it is this near (meters). */
inline constexpr float kHomeDistance = 0.05F;
/** A creature that turns on its place gets a new movement state when its direction changed by this angle (degrees). */
inline constexpr float kTurnAngle = 8.0F;
/** The vision of a creature looks at a character whose height is this near (meters). */
inline constexpr float kSenseHeight = 12.0F;
/** The vision of a creature does not take a character that it found no way to for this time. */
inline constexpr std::chrono::milliseconds kNoWayPause{8000};
} // namespace behavior_rules

/** Counts of the behavior, for the log and for the tests. */
struct BehaviorCounts {
    /** Steps of the behavior, and creatures that the steps looked at. */
    std::uint64_t steps{0};
    std::uint64_t creatureSteps{0};
    /** Creatures at rest that the vision rule looked at. */
    std::uint64_t sensed{0};
    /** New targets: by the vision, and by a hit. */
    std::uint64_t targetsBySight{0};
    std::uint64_t targetsByHit{0};
    /** Targets that a creature lost, and the ones of them that had no way to them. */
    std::uint64_t targetsLost{0};
    std::uint64_t targetsNoWay{0};
    /** Looks of the vision rule at a character in its shelter time, and steps that a different creature made shorter. */
    std::uint64_t sheltered{0};
    std::uint64_t crowded{0};
    /** Attacks that the creatures started, contacts that hurt a target, and contacts that did not. */
    std::uint64_t attacks{0};
    std::uint64_t contactsHit{0};
    std::uint64_t contactsMissed{0};
    /** Position changes of creatures. */
    std::uint64_t moves{0};
    /** Creatures that came back to their home. */
    std::uint64_t returns{0};
    /** The largest number of creatures in the step at one time. */
    std::size_t largestActive{0};
};

class Behavior {
public:
    Behavior(World& world, Creatures& creatures);

    /** The world calls it in each tick, through the creature system: the contacts that are due, and one step in each interval. */
    void tick();

    /** The creature system made a creature: its home, and if it can walk. */
    void made(Entity& creature);
    /** A character did hurt a creature that lives: the creature gets that character as its target. */
    void hurt_by(Entity& creature, EntityId attacker);
    /** A creature died: it has no target and no attack. */
    void died(Entity& creature);
    /** The world removed an entity. */
    void removed(const Entity& entity);

    /**
     * A character came to a place (a world entry, a travel, a respawn): the vision of no
     * creature takes it as a target for `kShelterTime`, and each creature that has it as its
     * target loses it. A character that hurts a creature is the target of that creature also
     * in this time. `waitsForClient`: the time starts at the first position that the client
     * reports from the place (`client_arrived`).
     */
    void shelter(Entity& character, bool waitsForClient);
    /** The client of a character reported a position: the shelter time of a character that waits starts. */
    void client_arrived(Entity& character);
    /** @return True while the vision of no creature takes the character as a target. */
    [[nodiscard]] bool sheltered(const Entity& character) const;

    /**
     * The creatures ignore a character (a command): they do not get it as a target by their
     * vision or by a hit, and a creature that has it as its target loses it. The value is a
     * value of the character in the world, with the character id as its key: it stays when
     * the session of the character ends, until the backend stops.
     */
    void set_ignored(const std::string& characterId, bool ignored);
    [[nodiscard]] bool ignores(const std::string& characterId) const {
        return m_ignored.count(characterId) != 0;
    }

    /** @return The number of creatures that are in the step now: a target, or the way back. */
    [[nodiscard]] std::size_t active_count() const {
        return m_active.size();
    }
    [[nodiscard]] const BehaviorCounts& counts() const {
        return m_counts;
    }
    /** @return The real time of the steps of the behavior (a measurement). */
    [[nodiscard]] const LoadMeter& load() const {
        return m_load;
    }

private:
    /** One contact of an attack that plays: the time is the key of the map. */
    struct PendingContact {
        EntityId creature{0};
        /** The character that the attack started at: a contact hurts only this entity. */
        EntityId target{0};
        /** The start count of the action of the attack: a contact of an action that a different action replaced does nothing. */
        std::uint16_t generation{0};
        std::uint32_t attack{0};
        std::uint32_t contact{0};
    };

    void step(float seconds);
    void sense();
    void think(Entity& creature, float seconds);
    void go_home(Entity& creature, float seconds);
    void start_attack(Entity& creature, Entity& target, std::uint32_t attack);
    /** @return The index of the next attack of a creature: its light attacks in their sequence, and a heavy attack after each `kLightAttacks` of them. */
    [[nodiscard]] static std::uint32_t next_attack(const Creature& creature);
    /**
     * @return The length of the last step of a creature to its target: `way`, or less when a
     * different creature stands at the end of the step (`kCrowdDistance`). 0: the creature stays.
     */
    [[nodiscard]] float free_way(const Entity& creature, const Entity& target, float way) const;
    /** @return The different creature at the end of a step of `length` meters to the target, or null. */
    [[nodiscard]] const Entity* blocker(const Entity& creature, const Entity& target, float length) const;
    /** @return A different creature that lives and stands `kCrowdDistance` or less from a place. Null: none. */
    [[nodiscard]] const Entity* taken(const Entity& creature, const state::WorldPosition& place) const;
    void contact(const PendingContact& pending);
    void take_target(Entity& creature, const Entity& target, const char* cause, float distance);
    void lose_target(Entity& creature, const char* cause);
    /** The creature found no way to its target: it loses it, and its vision does not take it again for some time. */
    void no_way(Entity& creature, const Entity& target);
    /** @return True when the creature stands on a free floor and walks. Asks the navigation until it has the data of the place. */
    [[nodiscard]] bool can_walk(Entity& creature);
    void arrive(Entity& creature);
    /** Turns a creature to a place, and marks its movement state when the direction changed enough. */
    void turn_to(Entity& creature, const state::WorldPosition& place);
    /** @return The entity of a target that a creature can fight: a character that is in the world, lives, and is not ignored. Null: none. */
    [[nodiscard]] Entity* fightable(EntityId id);

    World& m_world;
    Creatures& m_creatures;
    /** The creatures that the step looks at. */
    std::set<EntityId> m_active;
    /** The contacts of the attacks that play, in the sequence of their times. */
    std::multimap<WorldClock::time_point, PendingContact> m_contacts;
    /** The characters that the creatures ignore: character ids. */
    std::unordered_set<std::string> m_ignored;
    std::optional<WorldClock::time_point> m_lastStep;
    BehaviorCounts m_counts;
    LoadMeter m_load;
};

/** @return The direction from one place to a different one as the angle of a creature: degrees, 0 is north, 90 is west. */
[[nodiscard]] float yaw_to(const state::WorldPosition& from, const state::WorldPosition& to);
/** @return The angle between the direction of a creature and the line to a place, 0 to 180 degrees. */
[[nodiscard]] float angle_to(const state::WorldPosition& from, float yaw, const state::WorldPosition& to);

} // namespace soulwarden::backend::world

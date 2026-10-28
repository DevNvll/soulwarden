#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>

#include "gamedata/creature_data.h"
#include "world/behavior_tree.h"
#include "world/tree_binding.h"
#include "world/entity.h"
#include "world/ids.h"
#include "world/load_meter.h"
#include "world/world_clock.h"

// The behavior of a creature: it gets a target, goes to it, attacks it, and goes back to its
// anchor; with no target it walks its patrol or wanders. It is a part of the creature system
// (world/creatures.h): only that system changes a creature.
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
//           does not leave its place: it turns, and it attacks in its reach. For a target with
//           no way to it, see "No way".
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
//   No way. A creature whose target has no way to it (a high stone, a closed yard) goes to the
//           place that it can get to that is nearest to the target (the path of the navigation
//           to that place), and it waits there with its target for `kNoWayHold`: it turns to
//           the target and attacks it when it is in the reach. A new attack starts that time
//           again. After it the creature goes back, and its vision does not take that character
//           again for some seconds.
//   Back.   A creature loses its target when the target is dead, left the world, is ignored
//           by the creatures, or is too far, and when the creature is farther from its anchor
//           than its leash. The anchor is the place where the creature took its target. The
//           creature then runs back to its anchor (the reset): on this way it takes no damage
//           and no target by a hit or by its vision (the vitals state has the mark
//           "invulnerable"), and at the anchor it has its full health. A way back with no path
//           follows the places of the way out (the trail) back; a creature jumps to its anchor
//           only when no session can observe it.
//   Shoot.  (stage 3.4f-A) A creature whose slice has the basic shot of an archer (gamedata::kBasicShotAction: the action FireBow
//           with its release, game data) shoots a character in a band of distances (the parameters of the node basic_shot of its
//           tree): in `far` or less, with a free line, it stands, turns to the target, and starts the action of the shot when it is
//           ready (`repeat` from the start of its last shot, or the end of that action when later); farther, or with no free line, it
//           walks to the target until `hold` with a free line; nearer than `near` it walks back to `middle` on a path inside its leash,
//           at most one time in `retreat_pause`
//           (with no such path it shoots from its place). Its melee attack wins in its reach: a target in the reach of the next melee
//           attack of the slice gets the melee attack. The action plays to its end (no other attack, no step, no turn in it). At the
//           time of the release of the data the shot flies (world/projectiles.h) in the direction of the aim: the place of the target
//           at the release and, with the lead flag of the data, its velocity (a constant-velocity intercept with the aim speed of the
//           data, at most the lead angle of the data from the direct line), then the fall of the slice, and at most the aim angle of
//           the data from the front of the creature. The direction does not follow the target later. A shot whose action a different
//           action replaced (a stun, a knockdown, a command), whose creature died, or whose target is not there at the release, does
//           not fly. These are rules of this project with the values of the game data (protocol notes 4.37, creature-behavior.md).
//   Roam.   A creature at rest with a patrol route (the spawner of its spawn point has one, game
//           data) walks from waypoint to waypoint with the ambient speed of its slice and the
//           walk action of its slice, waits at each waypoint for a time of its range (and plays
//           the task of the waypoint when the action table of its slice has an action with that
//           name), and follows the policy of the route (a loop, back and forth, at random). A
//           creature at rest with no route whose slice wanders goes to a random place of its
//           wander range near its home, waits, and goes again. After a reset the patrol or the
//           wander goes on from the anchor. Only a creature that a character is near roams:
//           the leave distance of its interest (the distance at which a session of that
//           character can observe it); a different creature stands where it is.
//
// The trees (stage BT1, world/behavior_tree.h). The rules above are the engine and the leaves of a tree. The engine holds a creature that
// a stun, a knockdown, a stagger, a forced movement, the hit stun, or an action that plays holds, and it runs the way back; in each
// other step of a creature in a fight or at rest the tree of the creature decides (the default tree, apps/backend/behavior/*.jsonc: the
// order of the rules above, and the values of the bands, the repeats, and the pauses as node parameters). Each leaf calls one rule:
// keep_target (Back: a target that is gone, the leash, too far), status_action, area_attack, basic_shot (Shoot), melee (Way, Attack,
// Crowd, No way), turn_to_target, roam (Roam). Stage BT2: the default tree is a set of subtrees (rest, fight, status, area, shot, cast,
// melee), and the binding file selects the tree of each creature (world/tree_binding.h); a tree that names an action that the slice of a
// creature does not have is refused for that creature (one log line), and the creature has the tree default.
//
// A creature that stands needs no time: only the creatures of the set `m_active` (a target, the
// way back) and of the set `m_roaming` (a roam near a character) are in the step, the vision
// rule runs from the characters, and the roam set comes from the characters too, with the
// position index of the world. World thread only.

namespace soulwarden::backend::world {

class World;
class Creatures;

/** The constants of the behavior that the game data does not give (rules of this project). */
namespace behavior_rules {
/** The behavior decides and moves each creature one time in this interval of the world clock. */
inline constexpr std::chrono::milliseconds kStepInterval{200};
/** A step is not longer than the way of this time: the world clock of a test, or a slow tick, can jump. */
inline constexpr std::chrono::milliseconds kLongestStep{1000};
/**
 * The pause between the end of an attack and the next attack of a creature, and the pause after a heavy attack: the values of the node
 * `melee` of the default tree (apps/backend/behavior/default-fight.jsonc; case BT2 checks that the file has them). From stage BT1 the
 * pause after an action is the parameter of the node that started it (CreatureMind::actionPause); the cases use these names.
 */
inline constexpr std::chrono::milliseconds kAttackPause{1500};
inline constexpr std::chrono::milliseconds kHeavyPause{2500};
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
/** The leash of a slice whose data has no leash distance (meters). */
inline constexpr float kDefaultLeash = 40.0F;
/** A creature that does not walk loses a target that is farther than this (meters), when its slice has no vision distance for a fight. */
inline constexpr float kDefaultCombatSight = 30.0F;
/** A creature is at its home when it is this near (meters). */
inline constexpr float kHomeDistance = 0.05F;
/**
 * Two places are on one floor for an arrival (at the anchor, at a place of the trail, at a
 * waypoint) when their heights are this near (meters). Two floors of one cell of the navigation
 * data are farther apart (the height of a creature, 1.8 m): a place on a different floor is no
 * arrival, and the navigation decides the way (code review of stage 3.4e, findings F1 and F2).
 */
inline constexpr float kFloorTolerance = 1.0F;
/** A creature that turns on its place gets a new movement state when its direction changed by this angle (degrees). */
inline constexpr float kTurnAngle = 8.0F;
/** The vision of a creature looks at a character whose height is this near (meters). */
inline constexpr float kSenseHeight = 12.0F;
/** The vision of a creature does not take a character that it found no way to for this time. */
inline constexpr std::chrono::milliseconds kNoWayPause{8000};
/** A creature with no way to its target waits this long at the place nearest to it (with the target), then it goes back. */
inline constexpr std::chrono::milliseconds kNoWayHold{12000};
/** The trail of a fight: one place for each meter of the way, and this number of places at most. */
inline constexpr float kTrailSpacing = 1.0F;
inline constexpr std::size_t kTrailLength = 512;
/** The world selects the creatures that roam (near a character) one time in this interval of the world clock. */
inline constexpr std::chrono::milliseconds kRoamScanInterval{1000};
/** A creature is at a waypoint or at a place of its wander when it is this near (meters). */
inline constexpr float kRoamArrival = 0.3F;
/** The speed of a walk with no target for a slice with no ambient speed and no walk speed (meters in one second). */
inline constexpr float kDefaultAmbientSpeed = 1.5F;
/** Places that a creature tries for a walk of its wander in one step. */
inline constexpr unsigned kWanderTries = 6;
// The wait of a creature after a walk of its wander (kWanderWaitShortest 4 s to kWanderWaitLongest 12 s, stage 3.4e) and the pause after a
// roam goal with no way (kRoamNoWayPause 3 s) are parameters of the node roam of the subtree default/rest from stage BT2
// (default-rest.jsonc: wait_shortest, wait_longest, no_way_pause), with the same values.
/** A shot needs the target this near in height (meters): the height limit of the vision of a creature. */
inline constexpr float kShotHeight = kSenseHeight;
// The bands of a shot (kShotFar, kShotHold, kShotNear, kShotMiddle, kShotRepeat; kCastFar, kCastHold, kCastNear, kCastMiddle,
// kCastRepeat), the retreat pause (kRetreatPause), the repeats of an area attack (kAreaRepeat, kAreaOnlyRepeat, kAreaHoldMargin), the
// repeat of a status action (kStatusRepeat), the light attacks before a heavy one (kLightAttacks), and the distance at which a creature
// loses its target (kLoseDistance) are parameters of the nodes of the default tree from stage BT1 (default-fight.jsonc), with the same
// values. docs/systems/creature-behavior.md has the source of each one.
/**
 * The presentation of the spawn action of a new creature (protocol notes 4.14): a session gives the action that plays this long after a
 * first record with the spawn action (rep_session.cpp). A creature whose slice needs the spawn action starts no shot in this time after
 * its creation (code review of stage 3.4f-A, finding CR-R1: the arrow came before the windup of FireBow in each observer).
 */
inline constexpr std::chrono::milliseconds kSpawnLead{1500};
/**
 * The origin of a shot: this high above the feet of the creature and this far in front of it (meters; a decision of an agent: the data
 * has the joint of the bow, and the backend has no animated body; live check L2: the arrow leaves the bow of the archer).
 */
inline constexpr float kMuzzleHeight = 1.5F;
inline constexpr float kMuzzleForward = 0.6F;
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
    /** Creatures that came back to their anchor. */
    std::uint64_t returns{0};
    /** The largest number of creatures in the step at one time. */
    std::size_t largestActive{0};
    /** Steps of creatures that roam, the largest number of them at one time, waypoints and places of a wander that they reached. */
    std::uint64_t roamSteps{0};
    std::size_t largestRoaming{0};
    std::uint64_t waypoints{0};
    std::uint64_t wanderPlaces{0};
    /** Roam goals with no way, holds at the place nearest to a target with no way, ways back on the trail, and jumps to the anchor. */
    std::uint64_t roamNoWay{0};
    std::uint64_t holds{0};
    std::uint64_t retraces{0};
    std::uint64_t jumps{0};
    /** Hits that a creature on its way back did not take. */
    std::uint64_t immuneHits{0};
    /**
     * The shots of stage 3.4f-A: the actions of a shot that the creatures started, the releases that flew, the releases that did not
     * fly (the action was replaced, the creature died, the target was not there, the projectile system refused), and the walks back.
     */
    std::uint64_t shots{0};
    std::uint64_t releases{0};
    std::uint64_t releasesCancelled{0};
    std::uint64_t retreats{0};
    /**
     * The area attacks of stage 3.4f-C: the actions that the creatures started, the casts that made an area, and the casts that made none
     * (the action was replaced, the creature died, the target was not there, the spell system refused).
     */
    std::uint64_t areaAttacks{0};
    std::uint64_t casts{0};
    std::uint64_t castsCancelled{0};
    /**
     * The status actions of stage 3.4f-D: the actions that the creatures started, the marks that gave their statuses, and the marks that
     * gave none (a different action replaced the status action, the creature died, the data has no such status row).
     */
    std::uint64_t statusActions{0};
    std::uint64_t statusMarks{0};
    std::uint64_t statusMarksCancelled{0};
    /** Casts of an area at the place of the caster (stage 3.4f-D: the place rule of the client for a spell with no ranged attack data). */
    std::uint64_t castsAtCaster{0};
    /** The steps of the trees of the creatures (stage BT1), the nodes that they ran, and the reloads of the trees. */
    std::uint64_t treeSteps{0};
    std::uint64_t treeNodes{0};
    std::uint64_t treeReloads{0};
    /**
     * Stage BT2: the bindings of a creature to a tree that its slice does not fit (a named action that the slice does not have: the
     * creature has the tree default), and the trees of a command (`set_tree`).
     */
    std::uint64_t treeRefusals{0};
    std::uint64_t treeCommands{0};
};

/** Where the client puts the spell of a cast of an AI (stage 3.4f-D, client code 0x679DBB0; Behavior::place_rule). */
enum class CastPlace : std::uint8_t {
    /** The aim point of the target (AIAimAtTarget, UseAllAvailableTargets, and the ranged attack data of the spell row: 0x5C35450). */
    aim,
    /** The position of the target (AIAimAtTarget with no UseAllAvailableTargets: one record of the current target, no lead). */
    target,
    /** The position of the caster (AIAimAtTarget false, or the target loop of UseAllAvailableTargets with no ranged attack data). */
    caster,
    /** AIAimAtTarget false with ranged attack data: a shot of the ranged attack data (0x5BFBAF0); the backend has no such rule. */
    unsupported,
};

/** The aim of a release (stage 3.4f-A, Behavior::aim): the origin and the unit direction of the shot. */
struct ShotAim {
    state::WorldPosition origin;
    std::array<float, 3> direction{};
    /** The point that the creature aimed at (after the lead, before the fall of the slice), for a log line and a test. */
    state::WorldPosition point;
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
    /**
     * Stage QG5c2 (world/encounters.h): a member of an encounter gets a participant as its target (the AITargetCollector of the root of the
     * recipe; the aggro delay of the manager). The rule of hurt_by: a creature with a target keeps it.
     */
    void provoke(Entity& creature, EntityId character);
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
    /** @return The number of creatures that roam now (a patrol or a wander near a character). */
    [[nodiscard]] std::size_t roaming_count() const {
        return m_roaming.size();
    }
    /** @return True when the creature roams now. */
    [[nodiscard]] bool roaming(EntityId id) const {
        return m_roaming.count(id) != 0;
    }
    /** @return The patrol route of a creature: the route of its spawn point (game data). Null: none. */
    [[nodiscard]] const gamedata::PatrolRoute* route_of(const Entity& creature) const;
    /**
     * A development command: the anchor of a creature in a fight is a different place, and
     * the first place of its trail (the place where it took its target is the second one). A
     * test of the way back with no path to the anchor.
     * @return False when the creature has no target.
     */
    bool move_anchor(Entity& creature, const state::WorldPosition& anchor);
    /**
     * A forced movement of the creature ended (stage 3.3b-S4, world/forced_motion.h): its path is of no use from its new
     * place (the next step searches again), and a creature in a fight keeps the new place in its trail.
     */
    void moved_by_force(Entity& creature);
    /**
     * A development command and the tests (stage 3.4f-A): the creature starts its basic shot at a character now, with no band and no
     * repeat time. The release, the aim, and the flight are the rules of a shot of the behavior.
     * @return False: the creature has no basic shot, is dead, holds, or the target is not a character that lives.
     */
    bool command_shot(Entity& creature, EntityId target);
    /**
     * A development command and the tests (stage 3.4f-C): the creature starts its next usable area attack at a character now, with no
     * distance and no repeat time. The casts and the places are the rules of an area attack of the behavior.
     * @return False: the creature has no usable area attack, is dead, holds, or the target is not a character that lives.
     */
    bool command_area(Entity& creature, EntityId target);
    /**
     * @return The place of a cast of a ground area at a target (stage 3.4f-C): the place of the target at the cast (the velocity of the target
     *         times the time of the aim speed of the cast with its lead flag, at most its lead angle from the line), at most the CastDistance of
     *         the spell row from the creature, on the floor of the navigation data (the floor nearest to the height of the target).
     */
    [[nodiscard]] state::WorldPosition area_place(const Entity& creature, const Entity& target, const gamedata::AreaCast& cast) const;
    /** @return Where the client puts the spell of a cast (stage 3.4f-D, client code 0x679DBB0 with the values of the clip and of the row). */
    [[nodiscard]] CastPlace place_rule(const gamedata::AreaCast& cast) const;
    /**
     * @return The distance in which a creature casts an area attack (stage 3.4f-D): the CastDistance of the spell row, or the Radius of the
     *         row for a spell at the caster (its CastDistance is 0: the target must be in the sphere of the spell; a decision of an agent).
     */
    [[nodiscard]] float area_range(const gamedata::AreaCast& cast) const;
    /**
     * A development command and the tests (stage 3.4f-D): the creature starts a status action of its slice now (the action with the name, or
     * its first one), with no condition and no repeat time. Its marks give their statuses to the creature (Creatures::give_status).
     * @return False: the creature has no such status action, is dead, or holds.
     */
    bool command_status_action(Entity& creature, std::string_view action);
    /**
     * @return The aim of a release of a creature at a target (stage 3.4f-A): the origin at the creature, the place of the target and
     *         its velocity (World::velocity_of) for the lead of the data, the fall of the projectile slice, and the angle limits.
     */
    [[nodiscard]] ShotAim aim(const Entity& creature, const Entity& target, const gamedata::RangedRelease& release) const;
    /**
     * The trees of the creatures (stage BT1): loads the embedded trees and the `*.jsonc` files of a folder (empty: none), and binds each
     * creature of the world to its tree again (the default tree). A file that is not correct is refused (one log line for each one, and
     * `report`); the earlier tree of its name stays. A development route (reload) and the start of the behavior call it.
     * @return False when the set has no tree "default": the creatures then do nothing.
     */
    bool reload_trees(const std::string& folder, tree::LoadReport& report);
    [[nodiscard]] const tree::TreeSet& trees() const {
        return m_trees;
    }
    /** The binding file of the world (stage BT2): the program file `bindings.jsonc`, or the file of the folder of the trees. */
    [[nodiscard]] const tree::BindingSet& bindings() const {
        return m_bindings;
    }
    /** The results of `set_tree`. */
    enum class TreeCommand : std::uint8_t { done, unknownTree, refused };
    /**
     * A development command (stage BT2): the creature gets the tree of this name now (its state starts again), until a reload of the trees
     * or its removal. A tree that names an action that the slice of the creature does not have is refused (`cause`), and the creature
     * keeps its tree. The binding of the file is not used for it (CreatureBrain::binding: "a command").
     */
    TreeCommand set_tree(Entity& creature, std::string_view name, std::string& cause);
    /** A hit of a character did not hurt a creature on its way back (the combat system): for the counts. */
    void immune_hit() {
        ++m_counts.immuneHits;
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

    /** One cast of an area attack that plays (stage 3.4f-C): the time is the key of the map. */
    struct PendingCast {
        EntityId creature{0};
        EntityId target{0};
        /** The start count of the action of the area attack: a cast of an action that a different action replaced does nothing. */
        std::uint16_t generation{0};
        std::uint32_t area{0};
        std::uint32_t cast{0};
    };

    /** One mark of a status action that plays (stage 3.4f-D): the time is the key of the map. */
    struct PendingStatus {
        EntityId creature{0};
        /** The start count of the status action: a mark of an action that a different action replaced does nothing. */
        std::uint16_t generation{0};
        std::uint32_t action{0};
        std::uint32_t mark{0};
    };

    /** One release of a shot that plays (stage 3.4f-A): the time is the key of the map. */
    struct PendingRelease {
        EntityId creature{0};
        EntityId target{0};
        /** The start count of the action of the shot: a release of an action that a different action replaced does nothing. */
        std::uint16_t generation{0};
        std::uint32_t attack{0};
        std::uint32_t release{0};
    };

    /** The values of the node `melee` of a tree (world/behavior_tree.h, param::kLightBeforeHeavy...). */
    struct MeleeValues {
        std::uint32_t lightBeforeHeavy{0};
        std::chrono::milliseconds pause{0};
        std::chrono::milliseconds heavyPause{0};
    };
    /** The distances and the repeat time of the shot of a slice: the band of an archer, or of a caster (stage 3.4f-B). */
    struct ShotBand {
        /** The outer edge and the inner edge of the band, the hold distance of a walk into the band, and the middle of a walk back. */
        float outer{0.0F};
        float hold{0.0F};
        float inner{0.0F};
        float middle{0.0F};
        std::chrono::milliseconds repeat{0};
    };
    /** The values of the node `basic_shot` for a slice (its band by `CreatureSlice::caster`), and the melee cadence of its tree. */
    struct ShotValues {
        ShotBand band;
        std::chrono::milliseconds retreatPause{0};
        std::chrono::milliseconds pause{0};
        std::uint32_t lightBeforeHeavy{0};
    };
    struct AreaValues {
        std::chrono::milliseconds repeat{0};
        std::chrono::milliseconds onlyRepeat{0};
        float holdMargin{0.0F};
        std::chrono::milliseconds pause{0};
    };
    struct StatusValues {
        std::chrono::milliseconds repeat{0};
        std::chrono::milliseconds pause{0};
    };
    /** The values of the node `roam` (stage BT2): the waits of the wander in milliseconds (a random time between them), the pause with no way. */
    struct RoamValues {
        float waitShortest{0.0F};
        float waitLongest{0.0F};
        std::chrono::milliseconds noWayPause{0};
    };

    void step(float seconds);
    void sense();
    /**
     * The step of a creature in a fight (stage BT1): the rules of the engine that hold the creature (StepCause), else its tree. Before the
     * trees this was `think`.
     */
    void decide(Entity& creature, float seconds);
    /** @return The rule of the engine that holds a creature in a fight now (and the end of an action that played), or StepCause::tree. */
    StepCause engine_holds(Entity& creature, WorldClock::time_point now);
    /** One step of the tree of a creature (in a fight, or at rest). */
    void run_tree(Entity& creature, float seconds);
    tree::Status run_node(Entity& creature, const tree::CompiledTree& tree, std::size_t index, float seconds);
    /** @return The target of a fight of the creature of the tree that runs (fightable), one look-up in a step. */
    Entity* step_target(const Entity& creature);
    /** The leaves (stage BT1): each one is a rule of the behavior before the trees, with the parameters of its node. */
    bool keep_target(Entity& creature, float loseDistance);
    void melee(Entity& creature, Entity& target, float seconds, const MeleeValues& values);
    /**
     * Binds a creature to the tree of its binding (stage BT2, world/tree_binding.h), or to the tree default when its slice does not fit the
     * tree (`fits`): the lists of its state get one value for each node, each key, and each named action.
     */
    void bind(Entity& creature);
    /** Gives a creature a tree that fits its slice (the named actions in `actions`): its state starts again. */
    void attach(Entity& creature, const std::shared_ptr<const tree::CompiledTree>& tree, std::vector<std::int32_t> actions);
    /**
     * @return True when the slice has each action that the tree names (stage BT2): `actions` gets the index of each one in the status actions
     *         or the area attacks of the slice. False with `cause` for the first one that the slice does not have.
     */
    [[nodiscard]] static bool fits(const tree::CompiledTree& tree, const gamedata::CreatureSlice& body, std::vector<std::int32_t>& actions,
                                   std::string& cause);
    /** Loads the binding file of the program and of a folder (stage BT2); a file that is not correct is refused (`report`). */
    void load_bindings(const std::string& folder, tree::LoadReport& report);
    /**
     * @return The parameters of the first node of a kind of the tree of a creature, or of the default tree (a development command; for a
     *         shot the first node with the band of the slice). Null: neither tree has such a node (a tree of a folder; the command then does
     *         nothing: code review of stage BT1, finding R4).
     */
    [[nodiscard]] const float* command_values(const Entity& creature, tree::NodeKind kind) const;
    [[nodiscard]] static MeleeValues melee_values(const float* values);
    [[nodiscard]] static ShotValues shot_values(const gamedata::CreatureSlice& body, const float* values, std::uint32_t lightBeforeHeavy);
    [[nodiscard]] static AreaValues area_values(const float* values);
    [[nodiscard]] static StatusValues status_values(const float* values);
    [[nodiscard]] static RoamValues roam_values(const float* values);
    /** @return The named action of a node (its index in the status actions or the area attacks of the slice), or -1 (each one). */
    [[nodiscard]] static std::int32_t named_action(const Entity& creature, const float* values, std::size_t place);
    /** @return The light attacks before a heavy one of the tree of a creature (the melee rule of a shot), or of the default tree. */
    [[nodiscard]] std::uint32_t cadence_of(const Entity& creature) const;
    /**
     * @return True while the hit stun of the last hit holds the creature (Creature::hitStunUntil, user test 2): no step, no turn, no attack.
     *         The fight, the way back, and the roam check it (code review of user test 2, finding U2-R5). An ended hit stun goes.
     */
    bool hit_stunned(Entity& creature, WorldClock::time_point now);
    /**
     * The shot rules of a step of a creature with a basic shot and a character as its target (stage 3.4f-A).
     * @return False: the melee rules decide (the target is in the reach of the next melee attack).
     */
    bool think_shot(Entity& creature, Entity& target, std::uint32_t shot, float seconds, const ShotValues& values);
    void start_shot(Entity& creature, Entity& target, std::uint32_t shot, const ShotValues& values);
    void release(const PendingRelease& pending);
    /**
     * The area rules of a step of a creature with an area attack and a character as its target (stage 3.4f-C). @return False: the other
     * rules decide (not ready, not in the CastDistance, or no free line, for a creature with a different attack). `named`: the area attack
     * that the node names (stage BT2), or -1 for the next one of the slice.
     */
    bool think_area(Entity& creature, Entity& target, float seconds, const AreaValues& values, std::int32_t named = -1);
    /** @return The indexes of the area attacks of a slice whose casts the spell system can make (Spells::area_usable). */
    [[nodiscard]] std::vector<std::uint32_t> usable_areas(const gamedata::CreatureSlice& body) const;
    void start_area(Entity& creature, Entity& target, std::uint32_t area, const AreaValues& values);
    void cast(const PendingCast& pending);
    /**
     * The status rules of a step of a creature with a status action (stage 3.4f-D). @return True: the creature started a status action (or
     * one plays). `only`: the status action that the node names (stage BT2), or -1 for each one of the slice.
     */
    bool think_status(Entity& creature, Entity& target, const StatusValues& values, std::int32_t only = -1);
    /** @return True when the creature wants this status action now: its heal at low health, or its buff that it does not have. */
    [[nodiscard]] bool wants(const Entity& creature, const gamedata::CreatureStatusAction& action) const;
    void start_status_action(Entity& creature, std::uint32_t index, const StatusValues& values);
    void status_mark(const PendingStatus& pending);
    /**
     * A walk of a creature to its target on its path to `hold` (horizontal, with a free line), with the rules of a target with no way and
     * of the crowd (the walk of a shot, stage 3.4f-A; of an area attack, stage 3.4f-C).
     */
    void walk_to(Entity& creature, Entity& target, float hold, bool sees, float away, float seconds);
    void go_home(Entity& creature, float seconds);
    void start_attack(Entity& creature, Entity& target, std::uint32_t attack, const MeleeValues& values);
    /**
     * @return The index of the next attack of a creature: its light attacks in their sequence, and a heavy attack after each
     *         `lightBeforeHeavy` of them (the parameter of the node `melee`).
     */
    [[nodiscard]] static std::uint32_t next_attack(const Creature& creature, std::uint32_t lightBeforeHeavy);
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
    /** Turns a creature to a direction (degrees), and marks its movement state when the direction changed enough. */
    void turn(Entity& creature, float yaw);
    /** Keeps the place of a step of a fight in the trail of the creature (CreatureMind::trail). */
    void record_trail(Entity& creature);
    /** One step of the way back on the trail (a way back with no path). */
    void retrace(Entity& creature, float seconds);
    /** @return True when a session can observe the creature: a character with a session is not farther than the leave distance of its interest. */
    [[nodiscard]] bool observed(const Entity& creature) const;
    /**
     * @return True when each session that observes a creature whose slice needs the spawn action shows the action that plays: kSpawnLead
     *         after its creation and after the latest first record of a session (findings CR-R1 and CS-R4). True for a slice with no
     *         spawn action.
     */
    [[nodiscard]] static bool spawn_lead_over(const Entity& creature, WorldClock::time_point now);
    /** Selects the creatures that roam: the creatures at rest with a route or a wander that a character is near. */
    void select_roamers();
    /** @return True when a creature roams when a character is near: it walks, and it has a route that is not complete, or a wander. */
    [[nodiscard]] bool roams(Entity& creature);
    /** One step of the roam of a creature: a step to its goal, its wait, or its next goal (the values of its node `roam`). */
    void roam(Entity& creature, float seconds, const RoamValues& values);
    /** Gives a creature the next goal of its roam (CreatureMind::roamGoal). @return False: no goal now. */
    bool next_roam_goal(Entity& creature, const RoamValues& values);
    /** The creature is at the goal of its roam: the wait, the task, the next waypoint. */
    void at_roam_goal(Entity& creature, const RoamValues& values);
    /** The next waypoint of a route by its policy. */
    void next_waypoint(CreatureMind& mind, const gamedata::PatrolRoute& route);
    /** A creature that roamed stands where it is: no character is near it, or it took a target. */
    void stand(Entity& creature);
    /** @return The entity of a target that a creature can fight: a character that is in the world, lives, and is not ignored. Null: none. */
    [[nodiscard]] Entity* fightable(EntityId id);

    World& m_world;
    Creatures& m_creatures;
    /** The creatures that the step looks at: a target, the way back. */
    std::set<EntityId> m_active;
    /** The creatures at rest that roam (a patrol, a wander) because a character is near them. */
    std::set<EntityId> m_roaming;
    std::optional<WorldClock::time_point> m_lastRoamScan;
    /** The contacts of the attacks that play, in the sequence of their times. */
    std::multimap<WorldClock::time_point, PendingContact> m_contacts;
    /** The releases of the shots that play (stage 3.4f-A), in the sequence of their times. */
    std::multimap<WorldClock::time_point, PendingRelease> m_releases;
    /** The casts of the area attacks that play (stage 3.4f-C), in the sequence of their times. */
    std::multimap<WorldClock::time_point, PendingCast> m_casts;
    /** The marks of the status actions that play (stage 3.4f-D), in the sequence of their times. */
    std::multimap<WorldClock::time_point, PendingStatus> m_statusMarks;
    /** The characters that the creatures ignore: character ids. */
    std::unordered_set<std::string> m_ignored;
    std::optional<WorldClock::time_point> m_lastStep;
    BehaviorCounts m_counts;
    LoadMeter m_load;
    /** The trees of the creatures (stage BT1), and the default tree of each creature. */
    tree::TreeSet m_trees;
    std::shared_ptr<const tree::CompiledTree> m_default;
    /** The binding file (stage BT2), and the refusals that have a log line (tree name and slice path: one line for each pair). */
    tree::BindingSet m_bindings;
    std::set<std::pair<std::string, std::string>> m_refusalsLogged;
    /**
     * The target of the creature of the tree that runs (stage BT1): the leaves of one step look it up one time. A step does not remove an
     * entity, does not kill a character, and does not change who the creatures ignore; a new target id of the creature looks up again.
     */
    struct StepTarget {
        EntityId creature{0};
        EntityId target{0};
        Entity* entity{nullptr};
    };
    StepTarget m_stepTarget;
};

/** @return The direction from one place to a different one as the angle of a creature: degrees, 0 is north, 90 is west. */
[[nodiscard]] float yaw_to(const state::WorldPosition& from, const state::WorldPosition& to);
/** @return The angle between the direction of a creature and the line to a place, 0 to 180 degrees. */
[[nodiscard]] float angle_to(const state::WorldPosition& from, float yaw, const state::WorldPosition& to);

} // namespace soulwarden::backend::world

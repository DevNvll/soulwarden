#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gamedata/combat_tables.h"
#include "state/store.h"

// The creature data: the usual spawn points of the creatures in an area of the world, and for
// each kind of creature what the backend needs for its record (protocol notes 4.14).
//
// A kind is one row of the vitals tables of the game with one category row and one alias: its
// ids on the wire, its name, its health, and its bodies. A body is one slice of the game: the
// asset id that a client instantiates, and the slot of each component that has a replicated
// state. An alias can have more than one slice (a left arm or a right arm, a man or a woman):
// the world selects one for each creature.
//
// The data also has what the behavior of a creature needs (protocol notes 4.17): the sense
// distances, the leash distances, and the speeds of each slice, the attacks of each slice (an
// action with its contact times and damage rows), the damage of each kind and of each level,
// and the terrain height at each spawn point. A file of a tool from before stage 3.4b (format
// 1) has none of these: the creatures of such a file stand at their spawn points. From stage
// 3.4e (format 3) the data has the patrol route of each spawn point whose spawner has one, and
// the wander values and the ambient speed of each slice (protocol notes 4.14): the creatures of
// a file of format 1 or 2 have no patrol and no wander.
//
// The data is game data: the tool tools/gamedata/creatures.sh makes the file from the game
// copy of the user, and the file stays outside the repository. Without the file the backend
// starts and works, and the world has no creature.

namespace soulwarden::backend::gamedata {

/** File name of the creature data in the game data directory of the backend. */
inline constexpr const char* kCreatureDataFile = "creatures.json";

/**
 * The tag state of the animation fragments of a creature on the wire: 12 bytes (the field
 * globalFragTags of a movement state). The tags of a creature (its weapons) select the variant
 * of each fragment: a fragment of a creature with a weapon has no variant with no tag.
 */
inline constexpr std::size_t kFragmentTagBytes = 12;
using FragmentTags = std::array<std::uint8_t, kFragmentTagBytes>;

/**
 * The behavior values of a slice: fields of its Perception and Blackboard components (game
 * data). The meaning of each value in the behavior is a rule of this project (world/behavior.h).
 */
struct CreatureBehavior {
    /** The vision of a creature at rest: the angle to each side of its direction (degrees) and the distance (meters). */
    float sightAngle{0.0F};
    float sightDistance{0.0F};
    /** The vision of a creature in a fight. */
    float combatAngle{0.0F};
    float combatDistance{0.0F};
    /** The largest distance of a creature from its home, at rest and in a fight (meters). 0: the slice has no value. */
    float restLeash{0.0F};
    float combatLeash{0.0F};
    /** The speed of a walk and of a run in a fight (meters in one second). */
    float walkSpeed{0.0F};
    float runSpeed{0.0F};
    /**
     * The speed of a creature with no target: a patrol, a wander (meters in one second; the
     * blackboard value AmbientNavMaxSpeed, or the speed of the navigation configuration
     * "Wander"). 0: the slice has no value (format 2), and the behavior uses the walk speed.
     */
    float ambientSpeed{0.0F};
    /**
     * True: a creature of this slice with no patrol route wanders near its home (the blackboard
     * value Enable_Wander). The distance of a place of a wander from the home, in meters
     * (WanderDistanceMin, WanderDistanceMax).
     */
    bool wander{false};
    float wanderNearest{0.0F};
    float wanderFarthest{0.0F};
};

/** The policy of a patrol route: the field m_pathTravesalPolicy of the patrol component (client reflection 0x3B6E9F0). */
enum class PatrolPolicy : std::uint8_t {
    /** After the last waypoint the first one again. */
    loop = 0,
    /** Back and forth: after the last waypoint the waypoints before it, to the first one. */
    bounce = 1,
    /** A different waypoint of the route, at random. */
    random = 2,
    /** The value 3 of the client ("UNDEFINED"): the behavior uses the loop. */
    undefined = 3,
};

/** One waypoint of a patrol route (game data: the waypoint component AIWaypoint of the spawner). */
struct PatrolWaypoint {
    state::WorldPosition position;
    /** The direction of the waypoint (degrees, 0 north, 90 west). The creature takes it at the waypoint when `align`. */
    float yaw{0.0F};
    /**
     * The wait of a creature at the waypoint, in seconds: a time from `minWait` to `maxWait`
     * (m_minDuration, m_maxDuration). A negative value (-1 in the game files): no wait, or a
     * wait for ever (the behavior, world/behavior.h).
     */
    float minWait{0.0F};
    float maxWait{0.0F};
    /** The task of the waypoint (m_taskName, a task of the behavior tree of the game). Empty: none. */
    std::string task;
    /** m_matchAlignment: the creature takes the direction of the waypoint. */
    bool align{false};
};

/** The patrol route of a spawn point: the patrol component of its spawner, with the places of its placement. */
struct PatrolRoute {
    PatrolPolicy policy{PatrolPolicy::loop};
    std::vector<PatrolWaypoint> points;
};

/** One contact of an attack: the moment at which the attack can hurt its target. */
struct CreatureContact {
    /** The time of the contact from the start of the action, in seconds. */
    float time{0.0F};
    /** The largest distance of a target that the contact can hurt, in meters (a value of the tool). */
    float reach{0.0F};
    /** The damage as a part of the damage of the creature, and the damage that the row adds. */
    float factor{0.0F};
    float added{0.0F};
    /** The damage type: its name, its number on the wire, and its category. */
    std::string type;
    std::uint8_t wire{0};
    DamageCategory category{DamageCategory::direct};
    /** Name of the damage row of the game. For a log line only. */
    std::string row;
    /**
     * Stage B4: the AttackType of the damage row ("Light", "Heavy", "Magic", or empty; research bow-build-2.md Q5: each creature damage row of the
     * game data has one): the condition AttackType of a rule of a hit that the character takes (Enchanted Ward). A file of an older tool has none.
     */
    std::string attackType;
    /** The heart rune charge of the character that takes the hit (HitRuneCharge of the row, stage 1.3b). */
    float hitCharge{0.0F};
    /**
     * True: the contact is `kContactGap` or less after the contact before it. The two are one
     * hit (two hit volumes of one swing), and the world does not test this one. The backend
     * sets it at the load of the data.
     */
    bool repeat{false};
};

/**
 * Rules of this project for the attacks of a slice (the game files have no rule for them; the
 * behavior of the creatures uses them, world/behavior.h).
 *
 * Two contacts of one attack that are this near in time are one hit (seconds): 242 of the
 * 2,091 attacks of the supported build have two contacts with the same damage row at the same
 * time, for the two hit volumes of one swing.
 */
inline constexpr float kContactGap = 0.15F;
/**
 * A heavy attack: the sum of the damage parts of its hits is more than `kHeavyRatio` times the
 * smallest sum of the attacks of its slice, and more than `kHeavyWeight`.
 */
inline constexpr float kHeavyRatio = 1.5F;
inline constexpr float kHeavyWeight = 0.5F;

/**
 * One Homing clip of an attack fragment (stage U3, research creature-combat-feel.md Q1; game data: ProcLayer/Procedural type="Homing" of
 * the variant of the attack). The client applies a clip only to its own character (client code 0x255A140, 0x2564FC0, 0x2565210; live
 * check LC-Q1-2: no call of the move for a creature): the server moves and turns the creature (world/attack_motion.h).
 */
struct HomingClip {
    /** The start of the clip in the fragment (seconds) and its own duration (the ExitTime of the next Blend; below 0: to the end). */
    float start{0.0F};
    float duration{-1.0F};
    /** MoveToTarget: the clip moves the creature (else it only turns it). */
    bool moveToTarget{false};
    /** The move: to MoveToDistance from the target, in MoveToDuration (the smoothstep of 0x2565210), at most MaxMoveDistance from the
     *  origin of the clip (0: no limit); MoveToVelocity above 0 is the velocity rule (0x25653F0). */
    float moveToDistance{0.0F};
    float moveToDuration{0.0F};
    float moveToVelocity{0.0F};
    float maxMoveDistance{0.0F};
    /** The turn: TurnRate degrees each second, or with UseExponential a part of the angle that is left for each 1/30 s (0x25666A0). */
    float turnRate{0.0F};
    bool exponential{false};
    /** The wedge of the target at the start of the clip: Radius (with UseRadiusforAITarget), MaxAngle (to each side), Height. */
    float radius{0.0F};
    float maxAngle{0.0F};
    float height{0.0F};
    bool lockTarget{false};
    bool moveAfterTarget{false};
    bool upwards{false};
    bool useRadius{false};
    bool turnAfterTarget{false};
};

/** One attack of a slice: an action of its action table with one contact or more. */
struct CreatureAttack {
    /** Index of the action in CreatureSlice::actions. */
    std::uint32_t action{0};
    /** The time of the action, in seconds: the creature starts no other action before its end. */
    float length{0.0F};
    /** The contacts, in the sequence of their times (the backend sorts them at the load of the data). */
    std::vector<CreatureContact> contacts;
    /** The sum of the damage parts of its hits (a contact with `repeat` is no hit), and the number of the hits. */
    float weight{0.0F};
    std::uint32_t hits{0};
    /** True for a heavy attack of its slice (`kHeavyRatio`, `kHeavyWeight`). */
    bool heavy{false};
    /** The Homing clips of the fragment of the attack, in the sequence of their starts (stage U3). Empty: none (a file of an older tool). */
    std::vector<HomingClip> homing{};
};

/** An angle range of a condition of a reaction table row (degrees; client code 0x2C07B70: min - tolerance < angle < max + tolerance). */
struct AngleRange {
    float min{0.0F};
    float max{0.0F};
    float tolerance{0.0F};
};

/**
 * One row of a reaction table (stage U3, research Q2; game data: ReactionTableData of the table that the activity SetReactionFragment of
 * the action names). The client walks the rows in their order and takes the first one that holds (client code 0x2B9F1C0).
 */
struct ReactionRow {
    /** The power of the row: -1 for each power ("*"). */
    std::int32_t power{-1};
    /** The angle ranges of its side condition (damaged_front, _back, _left, _right). Empty: each angle. */
    std::vector<AngleRange> angles{};
    /** The fragment index of the row (the tool takes the fragment of the action for a name that the slice does not have). */
    std::int32_t fragment{-1};
    /** The length of the variant of the fragment (seconds; below 0: not known) and the start of its HitStun clip (below 0: none). */
    float length{-1.0F};
    float hitStun{-1.0F};
    /** AlignToDirection and AlignOffsetAngle (degrees): the creature turns so that the hit comes from the offset. */
    bool align{false};
    float alignOffset{0.0F};
};

/** One entry of the activity ReactionMotion of a reaction action (stage U3, research Q5): Default, or a conditional with its name. */
struct PushScale {
    std::string name;
    float scale{1.0F};
    /** PushBackScale: it scales only a displacement away from the source (client code 0x48A0690). */
    float pushBack{1.0F};
};

/** A reaction action of a slice: its table, its push scales, and the ExitOffsetSeconds of its end (stage U3). */
struct ReactionTable {
    std::vector<ReactionRow> rows;
    /** Default first. */
    std::vector<PushScale> motion;
    float exit{0.0F};
    /** The largest power of a row with a power (the backend sets it at the load): a larger power takes it (research Q2, item 4). */
    std::int32_t largestPower{-1};
    /** @return The entry of a name, else the Default entry. */
    [[nodiscard]] const PushScale& push(std::string_view name) const;
};

/** The additive reaction of a slice (stage U3): the action Reaction_Additive, its scope, and its table. */
struct AdditiveReaction : ReactionTable {
    std::uint32_t action{0};
    /** The scope of its fragment in the scope definitions of the controller (the wolf: 4, "Additive"). */
    std::uint32_t scope{0};
};

/**
 * The capsule of the character of a creature slice (stage 3.3b-S3): the QueryShapeCapsule of the CharacterComponent of the
 * slice (game data). The segment of the capsule has the length `height` on `axis` (a direction in the frame of the
 * creature: x to its right, y to its front, z up); `radius` is around it, and `offset` moves it in the same frame. An axis
 * of 0 makes it a sphere. The projectile system (world/projectiles.h) puts the lowest point of the capsule at the feet of
 * the creature (a decision of this project: the game files do not say where the capsule is).
 */
struct CreatureCapsule {
    float radius{0.0F};
    float height{0.0F};
    std::array<float, 3> axis{0.0F, 0.0F, 1.0F};
    std::array<float, 3> offset{};
};

/**
 * The hit facts of a slice (stage 3.3b-S5, the members `behind` and `head` of the tool). The angles of its DamageReceiver
 * (radians): a hit whose source is at this angle from the front of the creature or more is a hit from behind
 * (m_isHitFromBehindMinAngle), and a critical hit from behind (m_isHitByCritFromBehindMinAngle).
 */
struct CreatureBehind {
    float hit{0.0F};
    float critical{0.0F};
};

/**
 * The head volume of a slice (stage 3.3b-S5): the segment of its capsule in the frame of the creature at its feet (x to its
 * right, y to its front, z up; the bind pose of its bone from the skeleton of the game), and its radius.
 */
struct CreatureHead {
    std::array<float, 3> a{};
    std::array<float, 3> b{};
    float radius{0.0F};
};

/**
 * One projectile slice of the ranged attacks (stage 3.4f-A, the list "projectiles" of the file; research creature-ranged.md Q2
 * and R0, live checks L1 and L2): what a server projectile of a creature needs for its record and its flight. Game data.
 */
struct ProjectileSliceData {
    /** Path of the slice in the game files. For a log line only. */
    std::string path;
    /** Asset id and sub id of the slice: the metadata state of the projectile entity has them. */
    std::array<std::uint8_t, 16> asset{};
    std::uint32_t subId{0};
    /** Id of the root entity of the slice file. */
    std::uint64_t root{0};
    /** Slot of the Projectile component of the slice (its m_replicationIndex): the Projectile state (type 16) goes there. */
    std::uint32_t slot{0};
    /** The flight values of the Projectile component: m_baseSpeed (m/s), m_extraSpeed, m_timeForMaxSpeed, m_accel (m/s²). */
    float speed{0.0F};
    float extraSpeed{0.0F};
    float timeForMaxSpeed{0.0F};
    std::array<float, 3> accel{};
    /** m_flyingLifetime: the end of a flight with no hit (seconds); m_stuckLifetime: the time of a stuck projectile. */
    float flyingLifetime{0.0F};
    float stuckLifetime{0.0F};
};

/**
 * One release of a ranged attack (stage 3.4f-A): the CAGE-RangedAttack event of the fragment of its action, with its joins
 * (creature_behavior.py). Game data.
 */
struct RangedRelease {
    /** The time of the release from the start of the action, in seconds. */
    float time{0.0F};
    /** The profile and the attack of the ranged attack data (names for a log line), and the index of the attack on the wire. */
    std::string profile;
    std::string attack;
    std::uint32_t attackIndex{0};
    /** The ammunition (its AmmoID, empty with none) and the ammunition key on the wire (the CRC-32 of its lower-case AmmoID; 0: none). */
    std::string ammo;
    std::uint32_t ammoKey{0};
    /** Index into CreatureData::projectiles. True: the slice of the clip (SliceOverride), not the slice of the ammunition. */
    std::uint32_t projectile{0};
    bool sliceOverride{false};
    /** The damage table of the slice (its unique name and its key on the wire) and the place of the row among its rows. */
    std::string table;
    std::uint32_t tableKey{0};
    std::uint32_t rowIndex{0};
    /** The damage of the release: the row (the factor, the added damage, the type, the rune charge). Its time and reach are 0. */
    CreatureContact damage;
    /** The aim values of the clip: AILeadTarget, ProjectileSpeed (the speed of the aim), AILeadTargetMaxAngle, AIAimMaxAngle (degrees). */
    bool lead{false};
    float aimSpeed{0.0F};
    float leadAngle{0.0F};
    float aimAngle{0.0F};
    /** AIMissMinDistance and AIMissMaxDistance (meters). The backend keeps them; no rule uses them (creature-behavior.md). */
    float missMin{0.0F};
    float missMax{0.0F};
    /** The values of the attack of the profile: m_maxRange, m_projectileRadius, m_projectileAccelDelayDist (meters). */
    float range{0.0F};
    float radius{0.0F};
    float accelDelay{0.0F};
};

/** One ranged attack of a slice (stage 3.4f-A): an action of its action table with one release or more. */
struct CreatureRangedAttack {
    /** Index of the action in CreatureSlice::actions. */
    std::uint32_t action{0};
    /** The time of the action, in seconds: the creature starts no other action before its end. */
    float length{0.0F};
    /** The releases, in the sequence of their times (the backend sorts them at the load of the data). */
    std::vector<RangedRelease> releases;
};

/**
 * The shapes of a ground area of a creature (stage 3.4f-C; tools/gamedata/spells.py, area_shape). Game data (the shape of the spell
 * slice), with a live check of each spell slice record (creature_slices.json, areaSpells).
 */
enum class AreaShape : std::uint8_t {
    /** A place slice whose AreaStatusEffectComponent gives its statuses to the enemies in its volume (the mushroom of the Dryad Shaman). */
    status,
    /** A spell slice whose detection volume with no shape is active for a short window: one hit of the spell row (the lightning strike). */
    strike,
};

/**
 * One cast of a ground area (stage 3.4f-C): a CAGE-CastSpell event of the fragment of an action of the slice, with the values of the event
 * (creature_behavior.py). Game data.
 */
struct AreaCast {
    /** The time of the cast from the start of the action, in seconds. */
    float time{0.0F};
    /** The spell (SpellName) and the shape of its slice. */
    std::string spell;
    AreaShape shape{AreaShape::status};
    /** AIAimAtTarget, AILeadTarget, ProjectileSpeed (the speed of the aim), AILeadTargetMaxAngle, AIAimMaxAngle (degrees). */
    bool aimAtTarget{false};
    bool lead{false};
    float aimSpeed{0.0F};
    float leadAngle{0.0F};
    float aimAngle{0.0F};
    /** AIMissMinDistance and AIMissMaxDistance (meters). The backend keeps them; no rule uses them (creature-behavior.md). */
    float missMin{0.0F};
    float missMax{0.0F};
    /** UseAllAvailableTargets. The backend keeps it; the cast goes to the target of the creature (creature-behavior.md). */
    bool allTargets{false};
    /** A strike: the damage of the row of its spell (the damage table of the slice; its time and reach are 0). */
    CreatureContact damage;
};

/** One area attack of a slice (stage 3.4f-C): an action of its action table with one cast or more. */
struct CreatureAreaAttack {
    /** Index of the action in CreatureSlice::actions. */
    std::uint32_t action{0};
    /** The time of the action, in seconds: the creature starts no other action before its end. */
    float length{0.0F};
    /** The casts, in the sequence of their times (the backend sorts them at the load of the data). */
    std::vector<AreaCast> casts;
};

/**
 * One mark of a status action (stage 3.4f-D): a CAGE-Named event of the fragment of the action, at which the action list of the slice puts
 * statuses on the creature itself (an IfThenElse of the action with IfMannequinMarker of the name of the mark, and ModifyStatusEffects with
 * EffectsToApplyOnEnter). Game data (creature_behavior.py).
 */
struct StatusMark {
    /** The time of the mark from the start of the action, in seconds. */
    float time{0.0F};
    /** The ids of the status rows (AI_InqCleric_Rekindle). */
    std::vector<std::string> statuses;
};

/**
 * One status action of a slice (stage 3.4f-D): an action of its action table that puts statuses on the creature itself at its marks (the
 * self heal InqCleric_Rekindle_Self of the Inquisitor Cleric, the Howl of the Evil Knight hound). Only the actions of the list
 * `statusActions` of tools/gamedata/creature_slices.json (each with its live check) are in the data.
 */
struct CreatureStatusAction {
    /** Index of the action in CreatureSlice::actions. */
    std::uint32_t action{0};
    /** The time of the action, in seconds: the creature starts no other action before its end. */
    float length{0.0F};
    /** The marks, in the sequence of their times. */
    std::vector<StatusMark> marks;
};

/**
 * The basic shot of an archer (stage 3.4f-A, a rule of this project for the scope of the stage): the ranged attack of the action
 * with this name is the one ranged attack that a creature uses (the action FireBow of 20 slices of the supported build: one
 * release, the ammunition of the slice). The other ranged attacks of the data (a charged or a fast shot, the shot of a caster) are
 * in the data, and no creature uses them yet. The tool has the same name (creature_behavior.py, BASIC_SHOT_ACTION).
 */
inline constexpr const char* kBasicShotAction = "FireBow";
/**
 * The basic cast of a caster (stage 3.4f-B, a rule of this project for the scope of the stage, research creature-ranged.md part B): the
 * action Cast_One_Projectile of the data (one release of the attack Basic of its profile: the Skeleton Mage and the Ancient Guardian
 * mage with AI_Mage_Sphere index 125, the Dryad Shaman and the Damned Priest with AI_Spell_Ball, the Dynasty Summoner, the Swamp Shaman)
 * is the basic shot of a slice that has no FireBow. The creature fights with the band of a caster (world/behavior.h). The tool has the
 * same names (creature_behavior.py, BASIC_RANGED_ACTIONS).
 */
inline constexpr const char* kBasicCastAction = "Cast_One_Projectile";
/**
 * Stage QG9a (docs/systems/creature-behavior.md, a rule of this project): the declared basic cast of a boss slice whose action list has no
 * kBasicCastAction: the one projectile action of the slice that the creature tool reads (a ranged attack of the file) is its basic cast,
 * with the band of a caster. The boss trees of the story instances are not in the client (research story-instances.md section 3); the
 * other actions of the list (Spawn_Crystal, Exhausted, Summon_Minion, Spell_Ice_Storm, Spell_Ice_Spikes of Medea) wait for a later stage.
 */
struct DeclaredCast {
    const char* slice;
    const char* action;
};
inline constexpr DeclaredCast kDeclaredCasts[] = {
    {"slices/encounters/msq/ai_ww_medea_caster_console.dynamicslice", "Spell_Frostbolt"},
};

/** One slice of a creature. */
struct CreatureSlice {
    /** One component of the slice that has a facet: a target of a client message (protocol notes 4.2). */
    struct Component {
        /** Id of the entity of the component in the slice file. */
        std::uint64_t entity{0};
        std::uint64_t component{0};
        /** Name of the entity and of the component class. For a log line only. */
        std::string name;
    };
    /**
     * One action of the action table of the slice. The position of an action in the table is
     * its index in the scope record of a movement state (protocol notes 4.14).
     */
    struct Action {
        std::string name;
        /** Name of the animation fragment of the action, and its index in a scope record. -1: no fragment. */
        std::string fragment;
        std::int32_t fragmentIndex{-1};
        /** True: the action starts only when the server starts it (the mark ServerStarted of the game files). */
        bool serverStarted{false};
    };
    /** One component of the slice that has a replicated state. */
    struct Slot {
        /** Index of the component in a state record (`m_replicationIndex`). */
        std::uint32_t slot{0};
        /** Name of the component class, or its type id when the tool does not know the class. */
        std::string component;
        /** Type index of the state type in the supported client build. 0: not known. */
        std::uint32_t stateType{0};
    };

    /** Path of the slice in the game files. For a log line only. */
    std::string path;
    /** Asset id and sub id of the slice: the metadata state of a creature has them. */
    std::array<std::uint8_t, 16> asset{};
    std::uint32_t subId{0};
    /** Id of the root entity of the slice file. */
    std::uint64_t root{0};
    /**
     * `m_sliceSpawnRadius` of the Aoi component of the slice, in meters: the distance at which
     * a session starts to observe a creature with this body (a rule of this project).
     */
    float interestRadius{0.0F};
    /** Slots of the movement component (action list), of the vitals component, and of the marker component. 0: none. */
    std::uint32_t movementSlot{0};
    std::uint32_t vitalsSlot{0};
    std::uint32_t markerSlot{0};
    /**
     * Stage QG5c1: the slot of the PositionInTheWorld component of a stationary target (member `place`: a totem, a nest; the list
     * `targets` of tools/gamedata/creature_slices.json). Such a slice has no movement component: its record has its place in this state
     * (type 13), and it never moves, turns, reacts, or attacks. 0: a creature with a movement component.
     */
    std::uint32_t placeSlot{0};
    std::vector<Slot> slots;
    std::vector<Component> components;
    /** The action table of the slice. Empty: the tool found no grid file for the slice. */
    std::vector<Action> actions;
    /**
     * Index of the action that a creature with this slice plays when it dies: the action
     * "Death", or "Death_FB" for a slice with no such action. No value: the slice has none with
     * a fragment, and a dead creature keeps its pose.
     */
    std::optional<std::uint32_t> deathAction;
    /**
     * Index of the action that a creature with this slice plays when it takes a hit: the action
     * "Reaction". No value: the slice has none with a fragment, and the creature keeps its pose.
     */
    std::optional<std::uint32_t> hitAction;
    /**
     * Index of the action that a creature with this slice plays while it walks or runs: the
     * action "Move_Combat" (410 of the 412 slices that fight have it). Its fragment is a blend
     * of the locomotion of the slice that the client selects from the speed of the creature: a
     * Grey Wolf gallops with the speed of its run, and stands at the speed 0. The client plays
     * no walk with no such action: the creature slides in its pose (live proof, user test 1).
     * No value: the slice has none with a fragment, and the creature slides.
     */
    std::optional<std::uint32_t> moveAction;
    /**
     * Index of the action that a creature with this slice plays while it walks with no target
     * (a patrol, a wander): the action "Move" (live check of stage 3.4e: a Grey Wolf walks with
     * its legs at its ambient speed). No value: the slice has none with a fragment, and the
     * creature walks with `moveAction`.
     */
    std::optional<std::uint32_t> walkAction;
    /**
     * Index of the action "Stun" (the fragment r_Stun_Loop of the slices with it: 432 of 503):
     * a creature with this slice plays it while a stun effect holds it (stage 3.3a, live check
     * L6). No value: the slice has none with a fragment, and a stunned creature keeps its pose.
     */
    std::optional<std::uint32_t> stunAction;
    /**
     * The fragment index of the reaction of each power level (r_R0_F to r_R4_F of the fragment
     * file of the controller of the slice: the reaction to a hit from the front). The action
     * "Reaction" with one of them plays the reaction of that power (stage 3.3a: a knockdown
     * plays power 4). -1: the slice has no such fragment, and the reaction keeps the fragment
     * of its action. A file of a tool from before stage 3.3a has none.
     */
    std::array<std::int32_t, 5> reactions{-1, -1, -1, -1, -1};
    /**
     * The length of the variant of each reaction fragment of the slice, in seconds (user test 2, the member `reactionLengths` of the
     * tool): its end blend, else the end of its animation (the animation file). The reaction to a hit and its push last this time
     * (protocol notes 4.26: the client pushes a creature to the end of the reaction fragment). Below 0: not known (the reaction keeps
     * Creatures::kReactionTime). A file of a tool from before user test 2 has none.
     */
    std::array<float, 5> reactionLengths{-1.0F, -1.0F, -1.0F, -1.0F, -1.0F};
    /**
     * The reaction table of the action Reaction of the slice (stage U3): the side and the power select the row, its fragment, its length,
     * and its HitStun clip. No value: a file of an older tool, or the slice has no table (the backend keeps the rule of user test 2).
     */
    std::optional<ReactionTable> reactionTable;
    /** The additive reaction of the slice (stage U3). No value: none. */
    std::optional<AdditiveReaction> additive;
    /**
     * Index of the action "Idle" of the slice (user test 2, UT2-2): the action that the client of an observer selects itself when it gets
     * no state of the server (client code 0x4838EE0: the action whose name hash is the CRC-32 of "idle"). The server gives a creature its
     * record at the end of an action of the server, at the end of a walk, and at the end of a stun: the client then plays the idle and
     * does not keep the last pose of the action. No value: the slice has none with a fragment (the scope fields with no record).
     */
    std::optional<std::uint32_t> idleAction;
    /**
     * Slot of the status effect component of the slice (the component "StatusEffects", state
     * type 4236): the creature state of its status effects goes there (stage 3.3a). 0: none.
     */
    std::uint32_t statusSlot{0};
    /**
     * Name of the action that the server starts when it makes a creature with this slice (the
     * field m_spawnAction of the action list component, game data). The file has it only for
     * a slice whose body and name plate show in the client only after that action (the
     * picture tours of the slices: protocol notes 4.14). Empty: the creature starts with no
     * action of the server.
     */
    std::string spawnActionName;
    /** Index of that action in the action table. No value: the slice needs no spawn action. */
    std::optional<std::uint32_t> spawnAction;
    /**
     * The tag state of the animation fragments of a creature with this slice: the tags of its
     * weapons, as the bits of the tag definition file of its controller (the tool computes
     * them). All 0: the slice has no tag.
     */
    FragmentTags tags{};
    /** The behavior values of the slice. No value: a creature with this slice stands. */
    std::optional<CreatureBehavior> behavior;
    /** The capsule of the character of the slice (stage 3.3b-S3). No value: the slice has none, or a file of an older tool. */
    std::optional<CreatureCapsule> capsule;
    /** The angles of a hit from behind and the head volume (stage 3.3b-S5). No value: the slice has none, or a file of an older tool. */
    std::optional<CreatureBehind> behind;
    std::optional<CreatureHead> head;
    /** The attacks of the slice. Empty: a creature with this slice does not attack. */
    std::vector<CreatureAttack> attacks;
    /** The indexes of its attacks that are not heavy, and of its heavy attacks, in the sequence of the data. */
    std::vector<std::uint32_t> lightAttacks;
    std::vector<std::uint32_t> heavyAttacks;
    /** The ranged attacks of the slice (stage 3.4f-A). Empty: a file of an older tool, or the slice has none. */
    std::vector<CreatureRangedAttack> ranged;
    /**
     * The index in `ranged` of its basic shot (kBasicShotAction, or with no such action kBasicCastAction; the backend sets it at the load
     * of the data). No value: none.
     */
    std::optional<std::uint32_t> basicShot;
    /** True: the basic shot is the cast of a caster (kBasicCastAction, stage 3.4f-B): the band of a caster. */
    bool caster{false};
    /** The area attacks of the slice (stage 3.4f-C). Empty: a file of an older tool, or the slice has none. */
    std::vector<CreatureAreaAttack> areas;
    /** The status actions of the slice (stage 3.4f-D): its heals and buffs of itself. Empty: a file of an older tool, or none. */
    std::vector<CreatureStatusAction> statusActions;
    /**
     * m_lowHealthPercentThreshold of the Vitals component of the slice (stage 3.4f-D; game data: 0.15 for the Inquisitor Cleric): the part
     * of the maximum health at which the creature is at low health (the input LowHealth of the target curves of the AI). A heal of a
     * status action starts at it (world/behavior.h). No value: a file of an older tool, or the slice has no such field.
     */
    std::optional<float> lowHealth;
    /**
     * The server trees that the slice names (stage BT2; tools/gamedata/creature_behavior.py, read_server_trees): the asset paths of
     * m_behaviorTreeAssets of its BehaviorTreeComponent, lower case, with no folder and no suffix ("wolf/wolf_base"; research BT0: one in
     * 500 slices, two in the 3 bombers). The tree files are not in the client: a name is a binding key of the behavior trees
     * (world/tree_binding.h), not a tree. Empty: a file of an older tool.
     */
    std::vector<std::string> serverTrees;

    /**
     * @return True when a creature with this slice gets a target and attacks it: the slice has a sense, a speed, and an attack (a
     *         melee attack, or from stage 3.4f-A its basic shot, or from stage 3.4f-C an area attack).
     */
    [[nodiscard]] bool fights() const {
        return behavior && behavior->sightDistance > 0.0F && behavior->runSpeed > 0.0F
               && (!attacks.empty() || basicShot.has_value() || !areas.empty());
    }
    /** @return The basic shot of the slice, or null. */
    [[nodiscard]] const CreatureRangedAttack* basic_shot() const {
        return basicShot && *basicShot < ranged.size() ? &ranged[*basicShot] : nullptr;
    }

    /** @return True for a stationary target (stage QG5c1): a slice with a place slot and no movement component. */
    [[nodiscard]] bool stationary() const {
        return movementSlot == 0 && placeSlot != 0;
    }

    /** @return The index of the action with this name that has a fragment, or no value. */
    [[nodiscard]] std::optional<std::uint32_t> find_action(std::string_view name) const;

    /** @return The facet target of one component for an entity group id. */
    [[nodiscard]] std::uint64_t target(std::uint64_t group, const Component& component) const {
        return (component.entity ^ root ^ group) + component.component;
    }
    /** @return The component whose target for this entity group id is `target`, or null. */
    [[nodiscard]] const Component* find(std::uint64_t group, std::uint64_t target) const;
};

/** One slice of the alias of a kind. */
struct CreatureBody {
    /** Index into CreatureData::slices. */
    std::uint32_t slice{0};
    /** Weight of this slice in the selection. */
    std::uint32_t percent{0};
};

struct CreatureKind {
    /** Id of the kind for a command: the name of its vitals row, with a number when more than one kind has the row. */
    std::string id;
    /** Names of the vitals row and of the category row, and their ids on the wire (CRC-32 of the lower-case name). */
    std::string vitals;
    std::string category;
    std::uint32_t vitalsId{0};
    std::uint32_t categoryId{0};
    /** English name of the category row: the name that the client shows. For a list only. */
    std::string name;
    /** English name of the vitals row. */
    std::string vitalsName;
    std::string family;
    std::string creatureType;
    /** The health of the kind as a part of the health of its level. */
    float healthFactor{1.0F};
    /** The damage of the kind as a part of the base damage of its level. */
    float damageFactor{1.0F};
    /** The level of the most spawn points of the kind. */
    std::uint32_t level{1};
    std::vector<CreatureBody> bodies;
    /** Number of spawn points of the kind in the file. */
    std::uint32_t points{0};
    /**
     * Stage U3 (research Q5; game data): the buff buckets of the base vitals row give the status AI_Elite_Unbreakable_Grit_Buff (the
     * condition PromotedElite of the reaction actions: the push scales of the slice), or AI_NoReactions (the condition NoReaction: no
     * reaction and no additive reaction). A file of an older tool: false.
     */
    bool promotedElite{false};
    bool noReactions{false};
    /**
     * Stage QG5c2 (docs/systems/encounters.md): the member slice of an encounter recipe whose kind this is (a kind with no spawn point:
     * the encounter system makes its creatures). Empty: a kind of the spawn points.
     */
    std::string member;
};

/** One place of the world where the game has a creature. */
struct SpawnPoint {
    /** Id of the placement in the game files: the stable key of the spawn point. */
    std::string id;
    /** Index into CreatureData::kinds. */
    std::uint32_t kind{0};
    std::uint32_t level{1};
    state::WorldPosition position;
    /** Direction of the creature: the angle about the vertical axis, in degrees. */
    float yaw{0.0F};
    /** The two respawn values of the spawner, in seconds. Stage 3.5a uses them. */
    float respawnMin{0.0F};
    float respawnMax{0.0F};
    /**
     * The terrain height at the place of the point, in meters. No value: the file does not have
     * it (a file of format 1, or a place with no height map).
     */
    std::optional<float> ground;
    /** The patrol route of the point: an index into CreatureData::routes. No value: none (a file before format 3). */
    std::optional<std::uint32_t> route;
    /**
     * The phases of the day in which the point makes its creature (stage 2.2, format 4: the four flags m_night, m_dawn, m_day, and
     * m_dusk of the TimeOfDayConstraintComponent of its spawner slice; world/world_time.h: bit 0 night, bit 1 dawn, bit 2 day, bit 3
     * dusk). A spawner with no constraint, and each point of a file before format 4: each phase (0x0F). 0: no phase (the spawn rule
     * makes no creature).
     */
    std::uint8_t phases{0x0F};
    /**
     * True: the creature of the point leaves the world when the phase is not one of its phases (m_despawnOnTimeEnd of the constraint).
     * False: it stays, and the point makes no new creature in a phase that is not one of its phases.
     */
    bool leavesAtPhaseEnd{false};
    /**
     * The two cooldown flags of a spawner with a time constraint (format 4; m_cooldownAfterDespawn, m_cooldownAfterConstraintFail):
     * true: the end of its phases (a removal) starts the respawn time of the point; true: a look at the point out of its phases starts
     * the respawn time of the point. False for each point spawner of the supported build; true for the random encounter of the Mirage
     * Elemental. The rule of the flags is a rule of an agent from their names: the server code is not in the client.
     */
    bool cooldownAfterPhaseEnd{false};
    bool cooldownOutOfPhase{false};
};

/** The phase mask of a spawn point with each phase (no time constraint). */
inline constexpr std::uint8_t kEachPhase = 0x0F;

/** Limits of a patrol route (the loader refuses a file with a route outside them). */
inline constexpr std::size_t kMaxRoutePoints = 64;
/** A waypoint is not farther than this from its spawn point (horizontal, meters). */
inline constexpr float kLongestPatrol = 200.0F;
/** The longest wait at a waypoint (seconds). */
inline constexpr float kLongestWait = 3600.0F;

/** The values of one level of the level table of the game. */
struct CreatureLevel {
    std::uint32_t level{1};
    /** The health of a creature of the level, before the factor of its kind. */
    float health{0.0F};
    /** The base damage of a creature of the level, before the factor of its kind and of its attack. 0: the file has none. */
    float damage{0.0F};
    /** The gear score of a creature of the level: the armor of a player works against it. 0: the file has none. */
    float gearScore{0.0F};
};

/**
 * A creature that walks is at the terrain height. A creature whose spawn point is farther than
 * this from the terrain height (a floor of a building, a bridge, a cave) does not leave its
 * place: the terrain is not its floor (a rule of this project, protocol notes 4.17).
 */
inline constexpr float kTerrainTolerance = 1.0F;

class CreatureData {
public:
    /**
     * Reads the file of the tool. A file that is not complete (a member that is not there, an
     * index outside its list, a slice with no movement slot, a position that is not a
     * number) is not used: the data then stays empty. The same for a file whose parts do not
     * fit together: a kind or a spawn point with a level that the level table does not have, a
     * movement, vitals, or marker slot that is not the slot of that component in the slot list
     * of its slice, a slice with the null asset id, a kind whose health (the health of a level
     * times its factor) is 0 or not finite for a level of the table, a spawn action that the
     * action table of its slice does not have with a fragment, and a member "counts" with a
     * wrong form or with a number that is not the size of its list. The same for the behavior
     * data: an attack whose action the action table does not have with a fragment, an attack
     * with no contact, a contact after the end of its attack, a reach, a speed, or a distance
     * that is not a number of its range, and a damage type with no number or no category.
     * @return False when the file is not there or is not complete. `error()` then has the cause.
     */
    [[nodiscard]] bool load(const std::string& path);

    /**
     * Sets the data with no file (tests), with the same check as the data of a file.
     * @return False when the data is not complete: the object then has no data.
     */
    [[nodiscard]] bool set(std::vector<CreatureSlice> slices,
                           std::vector<CreatureKind> kinds,
                           std::vector<SpawnPoint> spawnPoints,
                           std::vector<std::pair<std::uint32_t, float>> levels);
    /**
     * The same, with the damage and the gear score of each level, the routes, the projectile slices of the ranged attacks, and the
     * capsule of the character of a player (stage 3.4f-A).
     */
    [[nodiscard]] bool set_full(std::vector<CreatureSlice> slices,
                                std::vector<CreatureKind> kinds,
                                std::vector<SpawnPoint> spawnPoints,
                                std::vector<CreatureLevel> levels,
                                std::vector<PatrolRoute> routes = {},
                                std::vector<ProjectileSliceData> projectiles = {},
                                std::optional<CreatureCapsule> playerCapsule = std::nullopt);

    [[nodiscard]] bool loaded() const {
        return !m_kinds.empty();
    }
    /** @return Why the last load gave no data. Empty after a load that gave data. */
    [[nodiscard]] const std::string& error() const {
        return m_error;
    }
    [[nodiscard]] const std::vector<CreatureSlice>& slices() const {
        return m_slices;
    }
    [[nodiscard]] const std::vector<CreatureKind>& kinds() const {
        return m_kinds;
    }
    [[nodiscard]] const std::vector<SpawnPoint>& spawn_points() const {
        return m_spawnPoints;
    }
    /** @return The patrol routes of the spawn points (SpawnPoint::route). */
    [[nodiscard]] const std::vector<PatrolRoute>& routes() const {
        return m_routes;
    }
    /** @return The projectile slices of the ranged attacks (stage 3.4f-A; RangedRelease::projectile). */
    [[nodiscard]] const std::vector<ProjectileSliceData>& projectiles() const {
        return m_projectiles;
    }
    /**
     * @return The capsule of the character of a player (the CharacterComponent of slices/player.dynamicslice, game data; stage
     *         3.4f-A: the body that a creature shot hits). No value: a file of an older tool.
     */
    [[nodiscard]] const std::optional<CreatureCapsule>& player_capsule() const {
        return m_playerCapsule;
    }
    /**
     * Stage QG5c1 (the member keptOut of the tool): the cause of a vitals row or a spawn category whose entries the file does not have (the
     * client does not show the slice, an encounter entry, an override entry). Null: the file names no cause (a name of no entry of the open
     * world, or a file of an older tool). The support manifest names it in the reason of a kill task.
     */
    [[nodiscard]] const std::string* kept_out(std::string_view name) const;
    /**
     * Stage QG5c2: @return The kind of a creature member slice of an encounter recipe (the member "members" of the tool), or no value; the
     *         cause of a member that the file does not have goes to `cause` when not null.
     */
    [[nodiscard]] std::optional<std::uint32_t> member_kind(std::string_view slice, std::string* cause = nullptr) const;
    /** Sets the member kinds with no file (tests): the lower-case member slice to its kind index (or -1) and its cause. */
    void set_members(std::unordered_map<std::string, std::pair<std::int64_t, std::string>> members) {
        m_members = std::move(members);
    }
    /** Sets the kept-out causes with no file (tests): lower-case names. */
    void set_kept_out(std::unordered_map<std::string, std::string> causes) {
        m_keptOut = std::move(causes);
    }
    /** @return The number of slices with a basic shot (stage 3.4f-A). */
    [[nodiscard]] std::size_t shooting_slices() const {
        return m_shootingSlices;
    }
    /** @return The number of slices with an area attack (stage 3.4f-C). */
    [[nodiscard]] std::size_t area_slices() const {
        return m_areaSlices;
    }
    /** @return The number of slices with a status action (stage 3.4f-D). */
    [[nodiscard]] std::size_t status_action_slices() const {
        return m_statusActionSlices;
    }
    /** @return The route of a spawn point, or null. */
    [[nodiscard]] const PatrolRoute* route_of(const SpawnPoint& point) const {
        return point.route && *point.route < m_routes.size() ? &m_routes[*point.route] : nullptr;
    }
    /** @return The number of spawn points with a patrol route, and of kinds with a slice that wanders. */
    [[nodiscard]] std::size_t routed_points() const {
        return m_routedPoints;
    }
    /** @return The number of spawn points with a time constraint (stage 2.2: a phase mask that is not each phase). */
    [[nodiscard]] std::size_t phased_points() const {
        return m_phasedPoints;
    }
    [[nodiscard]] std::size_t wandering_kinds() const {
        return m_wanderingKinds;
    }

    /** @return The kind with this id (the compare ignores the case of the letters A to Z), or null. */
    [[nodiscard]] const CreatureKind* find_kind(std::string_view id) const;

    /**
     * @return The maximum health of a kind at a level: the health of the level (the level table
     *         of the game) times the health factor of the kind. This is a rule of this project
     *         (protocol notes 4.14). 0 when the level table has no row for the level.
     */
    [[nodiscard]] float health(const CreatureKind& kind, std::uint32_t level) const;
    /** @return True when the level table has a row for the level. */
    [[nodiscard]] bool has_level(std::uint32_t level) const {
        return m_levels.count(level) != 0;
    }
    /**
     * @return The damage of a kind at a level before the factor of an attack: the base damage of
     *         the level times the damage factor of the kind. 0 when the table has no such value.
     */
    [[nodiscard]] float damage(const CreatureKind& kind, std::uint32_t level) const;
    /** @return The gear score of a creature of a level. 0 when the table has no such value. */
    [[nodiscard]] float gear_score(std::uint32_t level) const;
    /** @return The number of slices that have an attack and the values for the way to a target. */
    [[nodiscard]] std::size_t fighting_slices() const {
        return m_fightingSlices;
    }
    /** @return The number of kinds with a slice that fights, and of spawn points of these kinds. */
    [[nodiscard]] std::size_t fighting_kinds() const {
        return m_fightingKinds;
    }
    [[nodiscard]] std::size_t fighting_points() const {
        return m_fightingPoints;
    }
    /**
     * @return The number of spawn points of a kind that fights whose creature does not leave
     *         its place: the point has no terrain height, or it is farther than
     *         kTerrainTolerance from it.
     */
    [[nodiscard]] std::size_t rooted_points() const {
        return m_rootedPoints;
    }
    /** @return The largest vision distance at rest of a slice that fights, in meters. 0: no slice fights. */
    [[nodiscard]] float largest_sight() const {
        return m_largestSight;
    }
    /** @return True when a creature of this spawn point can walk on the terrain. */
    [[nodiscard]] static bool on_terrain(const SpawnPoint& point) {
        return point.ground && std::abs(point.position.z - *point.ground) <= kTerrainTolerance;
    }

    /**
     * Finds the kinds for a search text. Each word of the text must be in the id, the name, the
     * category, or the family of a kind (the compare ignores the case of the letters A to Z).
     * An empty text finds each kind.
     * @param total When not null: the number of all kinds that the text finds.
     * @return The kinds in the sequence of the file.
     */
    [[nodiscard]] std::vector<const CreatureKind*> search(std::string_view text,
                                                          std::size_t offset,
                                                          std::size_t limit,
                                                          std::size_t* total = nullptr) const;

private:
    void clear();
    [[nodiscard]] bool check();
    void build_keys();

    std::vector<CreatureSlice> m_slices;
    std::vector<CreatureKind> m_kinds;
    std::vector<SpawnPoint> m_spawnPoints;
    std::vector<PatrolRoute> m_routes;
    std::vector<ProjectileSliceData> m_projectiles;
    std::optional<CreatureCapsule> m_playerCapsule;
    /** Stage QG5c1: the kept-out names of the file (lower case) to their cause. */
    std::unordered_map<std::string, std::string> m_keptOut;
    /** Stage QG5c2: the creature member slices of the encounter recipes: the kind index (-1: none) and the cause. */
    std::unordered_map<std::string, std::pair<std::int64_t, std::string>> m_members;
    std::size_t m_shootingSlices{0};
    std::size_t m_areaSlices{0};
    std::size_t m_statusActionSlices{0};
    std::size_t m_routedPoints{0};
    std::size_t m_phasedPoints{0};
    std::size_t m_wanderingKinds{0};
    /** Level to the values of the level. */
    std::unordered_map<std::uint32_t, CreatureLevel> m_levels;
    std::size_t m_fightingSlices{0};
    std::size_t m_fightingKinds{0};
    std::size_t m_fightingPoints{0};
    std::size_t m_rootedPoints{0};
    float m_largestSight{0.0F};
    /** Lower-case kind id to the position in the list. */
    std::unordered_map<std::string, std::size_t> m_byId;
    std::vector<std::string> m_searchTexts;
    std::string m_error;
};

} // namespace soulwarden::backend::gamedata

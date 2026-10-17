#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gamedata/ability_data.h"
#include "gamedata/combat_tables.h"
#include "gamedata/weapon_actions.h"
#include "world/attack_sequence.h"
#include "world/entity.h"
#include "world/ids.h"
#include "world/position_report.h"
#include "world/projectiles.h"
#include "world/spells.h"
#include "world/world_clock.h"

// The abilities of a player (stage 3.3a, protocol notes 4.22): the keys of the ability bar (Q, R,
// F), the cooldown, the damage and the area of an ability, and the status effects on creatures.
//
// The client starts an ability itself (live check L1): a key press puts a press bit into its
// input entry (402, 115, 139 for the three places of the bar), the client plays the ability
// and starts its own cooldown with no answer of the server, and it does not play a press in
// that cooldown (it sends the bits of the press). The client has no message for an ability
// start and none for a refusal (L2). Thus the backend:
//
//   Cast.     The session gives each press of a new input step to this system (one cast for
//             each input sequence). The system finds the character from the session and the
//             ability from the bar of the weapon in its hand (the key gives the place), and it
//             checks the life, the lock of the input (a travel that the client did not
//             confirm), the weapon, the node of the ability, the rule and the data of the
//             ability, the cooldown, and that no other cast plays. A press that it refuses
//             makes no cast, no cost, and no hit, and it gets no message.
//   Cooldown. An accepted cast starts the cooldown of the data (the cooldown table of the game)
//             on the world clock. The cooldown is a value of the character, with the character
//             id as its key: a new session and a new world entry have it. The owner gets the
//             cooldown state (slot 45), whose countdown the HUD shows (live check L4).
//   Melee.    Charge, Maelstrom, Wrecking Ball, and the swing of Gravity Well: the client
//             reports each contact as for an attack (live check L3: no ability id, provider
//             FFFFFFFF). A contact in the window of a phase of a cast is a hit of that phase,
//             with the damage row of the phase (Combat::claim, Combat::strike): the hit check
//             of a melee attack stays. One hit of a creature for each phase.
//   Area.     Shockwave, Path of Destiny, and the vortex of Gravity Well: the client reports no
//             contact (L7). The world finds the creatures of the area at the time of the phase
//             with the position index, and a wall stops the area (Navigation::sees). One hit
//             of a creature for each phase.
//   Spell.    The projectile of Gravity Well makes a spell entity at its landing (stage 3.3b-S2a,
//             world/spells.h): the vortex. The entity runs the vortex phase at its start and the
//             burst at the time of its slice (game data), and the hits follow the entity: a
//             cancel ends the phases that did not come.
//   Effects.  A hit of a phase gives its effects to the creature through the creature system
//             (Creatures::apply_effect): a stun, a root, a slow, a weaken, a knockdown.
//
// The phases, their windows and times, their damage rows, and their effects are rules of this
// project (ability_rules below and Abilities::plan), with the values of the game data where
// the data has a value. World thread only.

namespace soulwarden::backend::world {

class World;

/** The ranged attack name of the projectile of Gravity Well (live check L7 of stage 3.3a: the shot item has its CRC-32). */
inline constexpr const char* kGravityWellAttack = "GravityWell";

/** The constants of the abilities that the game data does not give (rules of this project). */
namespace ability_rules {
/** Input steps of the client in one second. */
inline constexpr unsigned kStepsInSecond = 30;
/** The distance on the line of a projectile at which it touches a creature (meters). */
inline constexpr float kProjectileWidth = 1.0F;
/** The speed of the projectile of Gravity Well (meters in one second; not in the data that the tool reads). */
inline constexpr float kProjectileSpeed = 20.0F;
/** The time from the press to the throw of Gravity Well when the client sent no shot (the marker of the animation: 0.533 s). */
inline constexpr std::chrono::milliseconds kThrowTime{533};
/** A creature of an area is not higher or lower than this from the center of the area (meters). */
inline constexpr float kAreaHeight = 3.0F;
/**
 * The aim of an aimed heal (stage 3.3b-S6) ends this long after its key with no confirm (a decision of an agent: the client keeps its aim
 * until a confirm or a cancel that the server does not see; a click long after the key is an attack, not a confirm).
 */
inline constexpr std::chrono::milliseconds kAimKeep{10000};
/** The time of a knockdown (Wrecking Ball) and of a stagger (Path of Destiny with Seismic Waves). */
inline constexpr std::chrono::milliseconds kKnockdownTime{2000};
inline constexpr std::chrono::milliseconds kStaggerTime{1000};
/** The reaction power of a knockdown (the strongest reaction fragment of a slice: r_R4) and of a stagger. */
inline constexpr unsigned kKnockdownPower = 4;
inline constexpr unsigned kStaggerPower = 2;
/** Stimulated Reduction: each hit of Path of Destiny takes this part of its cooldown away, for this number of hits at most. */
inline constexpr float kCooldownPerHit = 0.05F;
inline constexpr unsigned kCooldownHits = 5;
/** Crowded Well: the burst of Gravity Well does this part more for each creature in the vortex, for this number at most. */
inline constexpr float kCrowdedPart = 0.10F;
inline constexpr unsigned kCrowdedMost = 5;
/**
 * Largest number of creatures that one phase of an area hurts, also across the circles of a path (a limit of the
 * cost; code review of stage 3.3a, finding F4). A project value: the spell rows and the ability rows of the game data
 * have no column for a count of targets.
 */
inline constexpr std::size_t kAreaMost = 32;
/**
 * A contact of a melee phase comes this much before the first time of the phase at most (the time of its first input
 * step from the press, on the world clock; code review of stage 3.3a, finding F1): the tolerance for a press and a
 * contact that come in reports with a different delay.
 */
inline constexpr std::chrono::milliseconds kPhaseEarly{250};
/**
 * The dash of Charge (stage 3.3b-S4, live check L7-C): with no creature in front the client moved its character 13.77 m
 * in 1.0 s (the root motion of the fragment Ability_GreatAxe_Engage, AnimationDrivenMotion with MotionMultiplier 1.0),
 * then 0.3 m slower and 0.67 m with the swing: 14.73 m in 1.6 s; its largest step was 0.602 m in 1/30 s (18.07 m/s).
 */
inline constexpr float kChargeDash = 13.77F;
inline constexpr std::chrono::milliseconds kChargeDashTime{1000};
inline constexpr float kChargeTravel = 14.73F;
inline constexpr float kChargePeak = 18.07F;
inline constexpr std::chrono::milliseconds kChargeLength{1600};
/**
 * The largest MotionMultiplier of the action GreatAxe_Engage (game data: 1.0, and 1.3 with the conditionals
 * Attack_Translation): the envelope of the position check takes the measured travel times this factor.
 */
inline constexpr float kChargeMultiplier = 1.3F;
/** The numbers of the actions of layer 0 of Charge in the client (live check L7-D): the state 7, the dash 59, the swing 2312. */
inline constexpr std::int32_t kChargeState = 7;
inline constexpr std::int32_t kChargeDashSequence = 59;
inline constexpr std::int32_t kChargeSwingSequence = 2312;
/** The dash of the server for a character with no client stops this far in front of the first creature on its line (meters). */
inline constexpr float kDashStop = 1.0F;
/** The observers of a character in the movement of a cast get a new movement state at most each this time. */
inline constexpr std::chrono::milliseconds kMotionMark{50};
/**
 * The largest number of abilities with a hit count of a character (CombatState::abilityHits; stage 3.3b-M2): a bound of the memory and of
 * the state of the owner (the abilities with TrackHitCount of the weapon trees; a decision of an agent).
 */
inline constexpr std::size_t kTrackedAbilities = 16;
/**
 * The body of a creature for the shapes of a strike that the server tests itself (the pull of Reap; stage 3.3b-M2): the shape reaches
 * this much farther (meters), and a creature up to kShapeHeight above or below the caster (a decision of an agent).
 */
inline constexpr float kShapeSlack = 0.5F;
inline constexpr float kShapeHeight = 2.0F;
/**
 * The axis of the shapes of a strike that the server tests (stage 3.3b-M2b): the direction of the move of the caster from the place of
 * the press, when it moved this far (meters; the root motion of Reap moves the body 1.5 m along its facing, and the homing of the client
 * turns the body to a target, protocol notes 4.34); else the direction of the camera at the press. A decision of an agent.
 */
inline constexpr float kFacingMove = 0.3F;
/**
 * The turn of the body to a creature that a strike of the cast hit (stage 3.3b-M2b): the homing of the client turns the body to its target
 * (the half angle of the Homing clips of the data: 60 and 55 degrees for Wrecking Ball). A creature that a strike hit, in front of the axis
 * within this angle (cosine), gets the shapes of the pull on the line to it. A decision of an agent.
 */
inline constexpr float kHomingTurnCosine = 0.5F;
/**
 * The aim of Penetrating Shot (stage B1) keeps this long with no shot (a decision of an agent: the client kept its aim for 58 s in the live
 * check of the stage and showed no limit; the shot item of its shoot action is the confirm, and the client sends it only from its aim, thus a
 * long keep cannot confirm a wrong cast; the limit bounds a value of a character that has no end event that the server sees).
 */
inline constexpr std::chrono::milliseconds kShotAimKeep{600000};
/**
 * A shot item of a cast of a ranged ability (stage B1) comes this much before the time of its clip on the world clock at most (the time of
 * the clip in the fragment, from the start of the fragment): the tolerance of the contact phases (kPhaseEarly, code review of stage 3.3a,
 * finding F1). Live check of stage B1: the client sent the arrows of Rapid Shot one input step before the times of the data.
 */
inline constexpr std::chrono::milliseconds kShotEarly{250};
/**
 * A shot item of a cast of a ranged ability (stage B1) comes this much after the end of the action of the cast at most (CastPlan::length from
 * the start of its fragment, or the time of its clip when that is later): the latest time of each shot (code review of stage B1, finding
 * B1-R1: an item 30 s after the cast shot the arrow that did not fly). A decision of an agent: the client sends the items at the times of its
 * clips (live check of stage B1), and the reports of a client on the loopback come with a delay far below this time.
 */
inline constexpr std::chrono::milliseconds kShotLate{2000};
} // namespace ability_rules

/** The abilities that this backend has (the build of stage 3.3a): the great axe and the war hammer. */
enum class AbilityKind : std::uint8_t {
    charge,
    maelstrom,
    gravityWell,
    wreckingBall,
    shockwave,
    pathOfDestiny,
    /** The counter of the rapier (stage 3.3b-S4): the window, and the counter action that the server starts. */
    riposte,
    /** Stage 3.3b-S2b: Ice Pylon of the ice gauntlet (a summon), Traps of the musket (a trap), Beacon of the life staff (an aura). */
    icePylon,
    traps,
    beacon,
    /** Stage 3.3b-S6: Divine Embrace of the life staff (a direct heal), Sacred Ground of the life staff (an area that heals). */
    divineEmbrace,
    sacredGround,
    /**
     * Stage 3.3b-M2: the other abilities of the great axe and the war hammer. Their hits are the strikes of the fragments of the data
     * (the weapon actions of format 2): Reap (its pull), Execute, Whirlwind (its spins and its exit by the hits), Mighty Gavel, Armor
     * Breaker, Clear Out (its push).
     */
    reap,
    executioner,
    whirlwind,
    mightyGavel,
    armorBreaker,
    clearOut,
    /**
     * Stage B1: the abilities of the bow of the build of the user. Their shots are the shot items of the client for the clips and the
     * spell casts of the fragment of their action (the ability actions of the projectile data, format 2): Penetrating Shot (an aim, then
     * one arrow that passes through creatures), Explosive Arrow (a projectile spell whose collision makes the explosion), Rapid Shot
     * (three arrows of one cast: a volley).
     */
    penetratingShot,
    explosiveArrow,
    rapidShot,
};

/** @return The English name of an ability, for a log line ("Wrecking Ball"). */
[[nodiscard]] const char* ability_name(AbilityKind kind);

/** What the system did with a press. */
enum class CastVerdict : std::uint8_t {
    accepted,
    /** The session is the owner of no entity. */
    noEntity,
    /** The backend has no ability data or no combat tables. */
    noData,
    /** The character is dead. */
    dead,
    /** The client shows the loading screen of a travel (the lock of its input). */
    locked,
    /** The character has no weapon with an ability table in its hand. */
    noWeapon,
    /** The place of the bar of the weapon is empty. */
    emptyPlace,
    /** The character does not have the node of the ability. */
    notLearned,
    /** The ability has no rule of this backend, or the data has no row for it. */
    noRule,
    /** The cooldown of the ability runs. */
    cooldown,
    /** A different cast of the character plays. */
    casting,
    /** The character has less mana than the cost of the cast (stage 1.3b, I14). */
    noMana,
    /**
     * The confirm of an aim: the place of the bar has a different ability than the aim now, or a different weapon is in the hand (code
     * review of stage 3.3b-S6, finding S6-R2).
     */
    aimChanged,
    /**
     * Stage U3 (research creature-combat-feel.md Q4; game data: IfEncumbered of each ability action and of CanAttack_<ability>): the bag of
     * the character is over its weight limit. The client plays no ability ("Encumbered - Cannot use abilities"): no cast, no cost, no
     * cooldown.
     */
    encumbered,
    /**
     * Stage U3 (research Q6; game data: each ability action is false while the switch of a weapon is pending, and the first action of the
     * new weapon comes 3 steps after the switch, grid rule 116): the client plays no press.
     */
    switching,
};
inline constexpr std::size_t kCastVerdictCount = 15;

/** @return A short text for a log line, for example "the cooldown runs". */
[[nodiscard]] const char* cast_verdict_text(CastVerdict verdict);

/** One press of a key of the bar, as the session gives it to the system. */
struct AbilityPress {
    /** The place of the bar: 0 (Q), 1 (R), 2 (F). */
    unsigned place{0};
    /** The input number of the entry with the press, and the input sequence of the session (AttackTrack::sequence). */
    std::uint16_t input{0};
    std::uint32_t sequence{0};
    /** The direction of the camera: yaw from north in radians, positive to the left. */
    float yaw{0.0F};
    /** True: the client of the session shows the loading screen of a travel that it did not confirm. */
    bool locked{false};
    /**
     * Stage 3.3b-S6: the press starts the cast of an aimed ability now (the confirm of an aim, Abilities::confirm, or the key with the
     * self cast input held); `self` true: the self cast input (the caster is the target).
     */
    bool confirmed{false};
    bool self{false};
    /** The ability that the press must find on the place of the bar (the confirm of an aim: the ability of the aim). 0: any. */
    std::uint32_t expected{0};
};

/** One shot of a client (an item of the second list of its input entry): the projectile of an ability. */
struct AbilityShot {
    std::uint16_t input{0};
    std::uint32_t sequence{0};
    /** The value "attack" of the item: the CRC-32 of the lower-case name of the ranged attack ("gravitywell"). */
    std::uint32_t attack{0};
    state::WorldPosition origin;
    /** The direction of the projectile (a unit vector). */
    std::array<float, 3> direction{};
    /** Stage B1: the value "row" of the item (the CRC-32 of the lower-case damage row of a clip; 0 for a spell), and the lock of the session. */
    std::uint32_t row{0};
    bool locked{false};
};

/**
 * One shot of a cast of a ranged ability (stage B1): a ranged clip or a projectile spell cast of the fragment of its action (game data:
 * the ability actions of the projectile data). The shot item of the client with its attack (and the row of a clip) is the shot; a cast
 * with no client gets its shots from the server at their times.
 */
struct ShotPlan {
    /** The time of the clip or of the cast in the fragment (from the start of the fragment). */
    std::chrono::milliseconds at{0};
    /** The ranged attack name ("TripleShot"): the shot item has the CRC-32 of its lower-case text. */
    std::string attack;
    /** The damage row of the clip in the data (the shot item has its CRC-32), and the row of the damage (a node override: Blood Soaked). */
    std::string clipRow;
    std::string row;
    /** A projectile spell of the fragment (Explosive Arrow: Spell_Ability_Bow_Explosive_Base or _Upgrade). Empty: a ranged clip. */
    std::string spell;
};

/** The shots of a cast of a ranged ability (stage B1). */
struct VolleyPlan {
    std::vector<ShotPlan> shots;
    /** The arrow passes through creatures (Penetrating Shot). */
    bool pierce{false};
    /**
     * The collision of the projectile makes the area of its SecondarySpellID (SpawnSecondaryOnCollision of the spell row: Explosive Arrow)
     * at the point of the collision: the burst phase of the plan, with the damage row of the offense of the slice of that spell.
     */
    bool explode{false};
};

/** The kind of a phase of a cast. */
enum class PhaseKind : std::uint8_t {
    /** A contact of the client in a window of input steps (a melee phase). */
    contact,
    /** The creatures in a circle around the caster at a time. */
    around,
    /** The creatures in the circles of a path in front of the caster at a time. */
    path,
    /** The creatures in the vortex of a projectile when the vortex starts (no damage), and at its burst. */
    vortex,
    burst,
    /**
     * The hit of a counter (stage 3.3b-S4: Ability_Rapier_Riposte_Out, ServerStarted): at an accepted counter the server
     * starts the counter action, and at `at` of that action it hits the attacker of the countered hit one time.
     */
    counter,
};

/** One effect that a hit of a phase gives to a creature. */
struct EffectPlan {
    /** The id text of the status row. Empty: a knockdown or a stagger (no status). */
    std::string status;
    /** The time from the hit to the effect. */
    std::chrono::milliseconds delay{0};
    /** A knockdown or a stagger: the power of the reaction (1 to 4), and its time. 0: not a knockdown. */
    unsigned power{0};
    std::chrono::milliseconds time{0};
};

/** One phase of a cast. */
struct PhasePlan {
    PhaseKind kind{PhaseKind::contact};
    /** The id text of the damage row. Empty: the phase does no damage (the start of a vortex). */
    std::string row;
    /** A contact phase: the window in input steps from the press. */
    unsigned firstStep{0};
    unsigned lastStep{0};
    /** An area phase: the time from the press (a vortex: from the start of the vortex). */
    std::chrono::milliseconds at{0};
    /** An area phase: the spell row of its shape. */
    std::string spell;
    /** The effects of a hit. */
    std::vector<EffectPlan> effects;
    /** Path of Destiny with Stimulated Reduction: each hit takes a part of the cooldown away. */
    bool cooldownPerHit{false};
    /** Gravity Well with Crowded Well: the burst does more for each creature in the vortex. */
    bool crowded{false};
    /**
     * Stage B1: the area takes each creature whose body (the capsule of its character, world::capsule_of) reaches into its sphere, not only a
     * creature whose place is in it: the explosion of Explosive Arrow at the point of its collision with the surface of a capsule (the
     * offense volume of the slice meets the hit volumes of the body; live check of the stage: the explosion at the surface of a wolf
     * capsule of 0.7 m was 1.6 m from the place of the wolf).
     */
    bool bodies{false};
    /**
     * Stage B1: the radius of the area from the offense of the slice of its spell (the explosion of Explosive Arrow: the radius of ExplosionDamage,
     * with its damage row; code review of stage B1, finding B1-R7: the area took the Radius of the spell row). No value: the Radius of the spell
     * row of the phase.
     */
    std::optional<float> radius;
    /**
     * A damage row whose impact (and no damage) the phase gives to each creature of its area (stage 3.3b-S4): the pull of
     * the vortex of Gravity Well (the child entity VortexPull of its slice: the row GreatAxe_GravityWell_Vortex, damage
     * factor 0). Empty: none.
     */
    std::string pull;
};

/** One action of layer 0 of a character for its observers in the movement of a cast (the numbers of the client: L7-D). */
struct MotionAction {
    /** The time from the press. */
    std::chrono::milliseconds at{0};
    std::int32_t state{0};
    std::int32_t sequence{0};
};

/**
 * The movement of the caster in a cast (stage 3.3b-S4): a dash or a leap that the client of the caster makes itself with
 * the root motion of its action (Charge). The server gives no second displacement: it accepts the positions of the client
 * in the envelope, and it moves only a character that no client plays (a remote test character).
 */
struct CasterMotion {
    /** The envelope of the position check: the distance (meters) and the speed of a step (m/s) more, for `length` from the press. */
    float distance{0.0F};
    float peak{0.0F};
    std::chrono::milliseconds length{0};
    /** The dash that the server makes for a character with no client: its distance on the line of the press and its time. */
    float dash{0.0F};
    std::chrono::milliseconds dashTime{0};
    /** The actions of layer 0 that the observers get, from the press; at `length` the character is at rest again. */
    std::vector<MotionAction> actions;
};

/**
 * The spell entity of a cast (stage 3.3b-S2b, world/spells.h): a summon that the server makes at its time (the Pylon), or a
 * projectile of a spell whose landing makes the entity (the trap, the Beacon). The values come from the game data.
 */
struct SpellPlan {
    SpellRole role{SpellRole::area};
    /** The projectile spell (the trap: Musket_Trap_Toss; the Beacon: Spell_Ability_Lifestaff_Beacon_Projectile). Empty: a summon. */
    std::string projectile;
    /** The spell of the entity (the Pylon: IceMagic_Spell_Pylon; the trap: Musket_Trap_Set; the Beacon: the SecondarySpellID of its projectile). */
    std::string spell;
    /**
     * The time from the press: the spawn of a summon (the Pylon: the cast at 0.6 s of its fragment, game data), or the throw of
     * the server when no shot item of the client came (a character with no client; live checks of stage 3.3b-S2b: the shot item
     * of the trap 8 input steps after the press, of the Beacon 4 steps).
     */
    std::chrono::milliseconds at{0};
    /** A trap: the status rows of its hit (the StatusEffect of the damage row of its offense). */
    std::vector<std::string> effects;
    /** A summon: the cost id that the spawn pays (the Pylon: the ManaCost of IceMagic_Spell_Pylon_UI). Empty: none. */
    std::string payment;
    /** True: the projectile also hits a character (TargetTypes All of the projectile spell of the Beacon). */
    bool characters{false};
    /**
     * An aura: the status rows of its spell row (StatusEffects), each one in the status data. The press resolves them, and a
     * row that the data does not have refuses the press before the payment (code review of stage 3.3b-S2b, finding F9).
     */
    std::vector<std::string> statuses;
};

/**
 * A direct heal of a cast (stage 3.3b-S6: Divine Embrace). At its time the cast chooses its target (the nearest friend of the caster
 * that the data permits, in the range of the spell and in sight, else the caster), pays the cost of its spell, and puts the statuses of
 * the spell on the target with the heal source of the caster. A heal with no target pays nothing.
 */
struct HealPlan {
    /** The spell row (Spell_LifeStaff_DivineEmbrace, or _ReducedCost with the node Privilege). */
    std::string spell;
    /** The time from the press (the cast at 1.0 s of the cast fragment: research abilities-magic.md, the animation data). */
    std::chrono::milliseconds at{0};
    /** The cost id of the spell (its ManaCost), paid with the heal. The press resolves it. */
    std::string payment;
    /** The status rows of the spell (StatusEffects). The press resolves them. */
    std::vector<std::string> statuses;
};

/** The plan of a cast: the phases for the nodes of the character. */
struct CastPlan {
    AbilityKind kind{AbilityKind::charge};
    /** The time in which no other cast of the character starts. */
    std::chrono::milliseconds length{0};
    std::vector<PhasePlan> phases;
    /** The movement of the caster (stage 3.3b-S4). No value: the cast does not move its caster. */
    std::optional<CasterMotion> motion;
    /**
     * Stage 3.3b-M2: the time from the press to the start of the fragment of the cast (a press in a buffered window of the fragment that
     * played starts the ability at the end of the window). The timed phases of the plan come this much later.
     */
    std::chrono::milliseconds delay{0};
    /**
     * A counter (stage 3.3b-S4, Riposte): the status row that the cast puts on its caster and that is the window of the
     * counter (Rapier_Riposte_Timer: 1 s, its absorption takes the countered hit). Empty: the cast has no counter.
     */
    std::string window;
    /** The spell entity of the cast (stage 3.3b-S2b). No value: none (or the vortex of Gravity Well, which its phases make). */
    std::optional<SpellPlan> spell;
    /** The direct heal of the cast (stage 3.3b-S6). No value: none. */
    std::optional<HealPlan> heal;
    /**
     * Stage 3.3b-M2: the contacts of the cast are the strikes of the fragment of its action in the weapon actions (format 2): the fragment
     * of the action, or the fragment that the action selects for the nodes and the perks of the character (`fragment`). The sequence of
     * the character plays it (world/attack_sequence.h): the windows, the hits of each strike, the transitions inside the ability (the exit
     * of Whirlwind, the uppercut of Charge). False: the contact phases of the plan (a file of format 1).
     */
    bool sequence{false};
    std::uint32_t fragment{0};
    /** The effects that each hit of a strike of the fragment gives (the knockdown of Wrecking Ball, the slow of Aftershock with an old file). */
    std::vector<EffectPlan> strikeEffects;
    /** The spells that the action casts at its start (CastSpell with no mark: the shield of Maelstrom with Absorb). */
    std::vector<std::string> actionSpells;
    /**
     * Stage 3.3b-S6: the key starts an aim of the client (the action list: Ability_Life_Staff_TouchOfLife, _HealingWave with
     * CastSpellTargeting), and the cast starts at the confirm of the same session (Abilities::confirm). A press of a character with
     * no client casts at once.
     */
    bool aimed{false};
    /**
     * Stage B1: the action of the ability in the ability action list of the weapon ("Bow_RapidShot"; the condition InAction of a row), and
     * its data (the projectile data of format 2: the fragment, its clips, its casts, its transitions to the abilities). The press resolves
     * them; with no data the press is refused (no rule).
     */
    std::string action;
    const gamedata::AbilityActionData* actionData{nullptr};
    /** Stage B1: the shots of the cast. No value: none (a plan of an earlier stage). */
    std::optional<VolleyPlan> volley;
    /**
     * Stage B1 (Penetrating Shot): the shot item of the client confirms the aim (the action Bow_Penetrating_Shoot: a click or a second key in
     * the aim; live check of the stage), not a click alone. The cast, its cooldown, and its shot start at the item.
     */
    bool shotConfirm{false};
};

/** A phase of a cast that a contact of a client fits (Combat::claim). */
struct MeleePhase {
    std::uint64_t cast{0};
    std::uint32_t phase{0};
    /** The damage row (an index of the combat tables). */
    std::uint32_t row{0};
    /** The weapon of the cast: its item id, its paperdoll slot (finding F1 of the review of stage 1.3b), and its gear score. */
    std::string itemId;
    std::uint32_t slot{0};
    std::uint32_t gearScore{0};
    /** The conversion of the gem of the weapon instance at the press (finding F3 of the review of stage 3.3b-S1). */
    std::string conversionType;
    float conversionPart{0.0F};
    /** True: this phase hurt this creature already. */
    bool done{false};
};

/** Counts of the abilities, for the log and for the tests. */
struct AbilityCounts {
    std::array<std::uint64_t, kCastVerdictCount> presses{};
    /** Hits of a contact phase, hits of an area phase, the creatures that an area looked at, shots, vortexes. */
    std::uint64_t contactHits{0};
    std::uint64_t areaHits{0};
    std::uint64_t areaLooked{0};
    std::uint64_t shots{0};
    std::uint64_t vortexes{0};
    /** Effects that the abilities gave, and effects that a creature did not take (dead, on its way back). */
    std::uint64_t effects{0};
    std::uint64_t effectsRefused{0};
    /** Hits that a wall stopped. */
    std::uint64_t blocked{0};
    /** Counters (stage 3.3b-S4): the hits that a window countered, and the counter hits. */
    std::uint64_t counters{0};
    std::uint64_t counterHits{0};
    /**
     * Direct heals (stage 3.3b-S6): the heals of a friend, the heals of the caster itself, the heals with no target (no payment), and
     * the friends that the choice of the target looked at (the load).
     */
    std::uint64_t healsFriend{0};
    std::uint64_t healsSelf{0};
    std::uint64_t healsRefused{0};
    std::uint64_t healLooked{0};
    /** The keys of an aimed ability that started an aim of the client, and the confirms that started its cast (stage 3.3b-S6). */
    std::uint64_t aims{0};
    std::uint64_t confirms{0};
    /**
     * Stage 3.3b-M2: the hits of a strike of a fragment, the creatures that a strike with no damage moved (the pull of Reap), the creatures
     * that it looked at, the casts that started later than their press (a buffered window of the fragment that played), the spells of a
     * fragment or of an action, and the casts of a spell of a passive rule.
     */
    std::uint64_t strikeHits{0};
    std::uint64_t follows{0};
    std::uint64_t followLooked{0};
    /** The creatures in a pull shape above the limit of kAreaMost (code review of stage 3.3b-M2, finding M2-R10). */
    std::uint64_t followOverLimit{0};
    std::uint64_t delayedStarts{0};
    std::uint64_t fragmentSpells{0};
    std::uint64_t ruleSpells{0};
    /**
     * Stage B1: the shots of the volleys that flew (the shot items of the client and the shots of the server for a character with no
     * client), the shot items that a volley did not take (too early, a repeated input, a different attack), the aims that a shot item
     * confirmed, the explosions of a collision, and the presses that a window of a bow fragment started later (a buffered window).
     */
    std::uint64_t volleyShots{0};
    std::uint64_t volleyRefused{0};
    std::uint64_t shotConfirms{0};
    std::uint64_t explosions{0};
    std::uint64_t windowDelays{0};
};

/** The check of a strike of a cast (Combat::claim_strike, stage 3.3b-M2). */
struct AbilityStrike {
    bool valid{false};
};

class Abilities {
public:
    /**
     * @param data The ability data (game data). Null or not loaded: each press is refused.
     * @param combat The combat tables: the damage rows of the abilities.
     */
    Abilities(World& world, const gamedata::AbilityData* data, const gamedata::CombatTables* combat);

    [[nodiscard]] bool has_data() const {
        return m_data != nullptr && m_data->loaded() && m_combat != nullptr && m_combat->loaded();
    }

    /** A session gives the press of a key of the bar of its character. */
    CastVerdict press(SessionId session, const AbilityPress& press);
    /**
     * A session gives the confirm input of its client (stage 3.3b-S6; live check: a click, `attack_primary`, or the self cast input,
     * `self_target`): the aim of an ability of its character (Divine Embrace, Sacred Ground) starts its cast now, with all the checks of
     * a press. An aim ends at a press of a key, a change of the weapon, its confirm, and after ability_rules::kAimKeep.
     * @param yaw The look yaw of the confirm (the direction of the cast).
     * @return True when an aim of the session started its cast.
     */
    bool confirm(SessionId session, std::uint16_t input, bool self, float yaw, bool locked = false);
    /**
     * Ends the aim of a character (a change of its bar or of its nodes, a travel; code review of stage 3.3b-S6, S6-R2 and S6-R8), and the shots
     * of the volley of its cast that did not fly (code review of stage B1, finding B1-R1: a shot item after a travel shot from the new place).
     * A projectile that flies already flies on.
     */
    void cancel_aim(const std::string& characterId);
    /**
     * A character died (Health::apply, the one path of a death; code review of stage B1, finding B1-R3): its aim ends (a shot item after a
     * revival confirmed the old aim of Penetrating Shot), and the shots of the volley of its cast that did not fly end.
     */
    void died(const Entity& character);
    /**
     * Development aid (tools/world cast): the press of a key of the bar for a character with no client (a remote test
     * character). The same checks and the same cast as a press of a session; the server throws a projectile itself.
     */
    CastVerdict press_character(const std::string& characterId, const AbilityPress& press);
    /**
     * A spell entity of a cast runs one of its phases at its time (world/spells.h): the hits of the phase at the place of the
     * spell (the vortex: its effects; the burst: its damage). The caster must be in the world; a dead caster's spell hits
     * while the spell lives (a spell row with no EndOnCasterDeath: game data).
     */
    void spell_phase(std::uint64_t cast, std::uint32_t phase, const state::WorldPosition& center);
    /** The spell entity of a cast went away (its end or a cancel): the cast waits for it no more. */
    void spell_ended(std::uint64_t cast, EntityId spell);
    /** A session gives a shot of its client: the projectile of a cast of its character. */
    void shot(SessionId session, const AbilityShot& shot);
    /**
     * @return True when the cast of the character of a session waits for a shot item with this attack (the CRC-32 of the lower-case
     *         ranged attack name): Gravity Well ("gravitywell"), the trap ("traps"), the Beacon ("heavy"; stage 3.3b-S2b). The
     *         session then gives the item to `shot`, not to the projectile system of the basic attacks. Stage B1: also a shot of the
     *         volley of a cast of a ranged ability (the attack and the row of a clip of its fragment, at its time, a new input), and the
     *         shot item that confirms the aim of Penetrating Shot. @param row The value "row" of the item. @param input Its input number.
     */
    [[nodiscard]] bool takes_shot(SessionId session, std::uint32_t attack, std::uint32_t row = 0, std::uint16_t input = 0);

    /**
     * @return The contact phase of the cast of a character that a contact of this input number
     *         fits (Combat::claim), for this target. No value: no cast with such a phase, a contact before the first
     *         time of the phase on the world clock, or a contact of a newer press (findings F1 and F2 of the review
     *         of stage 3.3a).
     * @param attack The input number of the attack input of the session that the contact fits (AttackTrack::press_for),
     *        when it has one: an attack after the press of the cast owns the contact.
     */
    [[nodiscard]] std::optional<MeleePhase> contact_phase(const Entity& attacker, SessionId session, std::uint16_t input,
                                                          std::uint32_t sequence, EntityId target,
                                                          std::optional<std::uint16_t> attack = std::nullopt);
    /** @return The ability of a cast (a cast that the table has; Wrecking Ball for one that it does not have). */
    [[nodiscard]] AbilityKind kind_of(std::uint64_t cast) const;
    /** @return The id text of the ability of a cast ("Ability_WarHammer_WreckingBall"), or an empty text. */
    [[nodiscard]] std::string ability_of(std::uint64_t cast) const;
    /** A contact phase hurt a creature (Combat::claim): the phase does not hurt it again, and the creature gets the effects. */
    void contact_hit(std::uint64_t cast, std::uint32_t phase, Entity& attacker, Entity& target);

    /**
     * Gives a creature the effect of a status row of the data (an ability, or a development
     * command) through the creature system (Creatures::apply_effect).
     * @param seconds The time of a command, in the place of the time of the row.
     * @return False: the data has no such row, or the creature took no effect.
     */
    bool apply_status(Entity& creature, std::string_view status, EntityId source, std::optional<float> seconds = std::nullopt);

    /** The world made the entity of a character: it gets the cooldowns of the character. */
    void entered(Entity& character);

    /**
     * @return Whether a contact of a client can be a strike of the fragment of a cast (stage 3.3b-M2: a cast of the sequence, with its
     *         weapon): the session and the input sequence of the cast, and an input before the close of the cast (a later press of
     *         the session, accepted or refused: the rule of the contact phases, code review of stage 3.3b-M2, finding M2-R6).
     */
    [[nodiscard]] AbilityStrike strike_of(std::uint64_t cast, SessionId session, std::uint32_t sequence, std::uint16_t input) const;
    /**
     * A contact of the client fits a strike of the fragment of a cast (Combat::claim_strike, stage 3.3b-M2): the hit with the weapon, the
     * gem, and the attack of the cast (Combat::strike), the damage row of the strike, the status effect of the row, and the effects of the
     * plan. @return The result of the hit.
     */
    HitResult strike_hit(std::uint64_t cast, const gamedata::AttackFragment& fragment, std::uint32_t strike, std::uint32_t rowIndex,
                         Entity& attacker, Entity& target);
    /**
     * The sequence of a character passed a strike, a spell cast, or a mark of the fragment of a cast (stage 3.3b-M2): a strike with no
     * damage moves each creature that the cast hit before (the pull of Reap: the client reports no contact for it); a spell cast makes
     * its spell (the ground of Mighty Gavel); a mark makes the spells of the action with that mark.
     */
    void fragment_event(Entity& character, const SequenceEvent& event);
    /**
     * A passive rule of a character casts a spell (the cell CastSpell of a rule: Power Cleaner of Clear Out, stage 3.3b-M2): the spell
     * entity of the spell at the character (a friend area gives its statuses to the friends in its radius).
     */
    void rule_spell(Entity& character, std::string_view spell, std::string_view rule);
    /**
     * The status effect of a damage row (the column StatusEffect of the combat tables, format 3; stage 3.3b-M2): the root of Gravity on a
     * pull row. With EffectOnlyOnDamage only after damage above 0. A status with OnEndStatusEffect gives its end status at its end (the
     * root after its hold), and ApplicationCooldown permits one application for each creature in that time.
     */
    void row_effects(Entity& creature, const gamedata::DamageRow& row, float dealt, EntityId source);
    /**
     * The hits of a cast for the client (stage 3.3b-M2; CombatState::abilityHits): 0 at the press (`reset`), else one more, for an
     * ability whose row has TrackHitCount. The owner gets the ability state again.
     */
    void count_hit(Entity& character, std::uint32_t ability, bool reset);

    /**
     * @return The mana cost of a cast (stage 1.3b, I14; protocol notes 4.23): the ManaCost of the first spell of the plan
     *         whose cost id the mana table of the players has (a cost id with no row costs nothing: client code at image
     *         RVA 0x6801430 pays nothing for it), times 1 + the ManaCostMult of each learned node of the character whose
     *         ManaCostList has the id or "All" (client code at image RVA 0x67EAED0), not less than 0.
     */
    [[nodiscard]] float mana_cost(const Entity& character, const CastPlan& plan) const;
    /**
     * @return The mana cost of a cost id of the mana table of the players for a character (the rule of `mana_cost`): its
     *         ManaCost times the node multipliers. 0: no such id (stage 3.3b-S3: the cost of a magic shot).
     */
    [[nodiscard]] float mana_cost_of(const Entity& character, std::string_view costId) const;
    /**
     * The one path of a change of the mana of a character (stage 1.3b, I14): a cost (a negative amount) and a gain (a
     * potion). The mana stays between 0 and its maximum. A spend starts the regeneration after the delay of the data
     * (ManaRegenDelay, or ManaRegenDelayOnEmpty at 0). The vitals state and the mana state of the owner change.
     */
    void change_mana(Entity& character, float amount, const char* what);
    /**
     * The maximum or the regeneration factor of the mana of a character changed (stage 3.5b.2, the progress system: the MaxMana and
     * ManaRate parts): the regeneration goes on with the new rate (the base rate of the data times Vitals::manaRateFactor), and the mana
     * state of the owner changes.
     */
    void mana_changed(Entity& character);
    /**
     * A shorter cooldown of each ability of the bar of the weapon in the hand (stage 1.3b: Refreshing Move, CooldownTimer
     * with CDRImmediatelyOptions ActiveWeapon): `part` of the cooldown of the data, not below the minimum end and not
     * below now. The end of the client cooldown changes the same.
     */
    void reduce_weapon_cooldowns(Entity& character, float part);
    /**
     * The same for some abilities of the bar (stage 3.3b-S5, CDRImmediatelyOptions of a passive rule): `only` an ability whose id
     * or cooldown id (CooldownId of its row) is this text (AbilitySpecific), or each ability but such an ability (`except`:
     * AllExcept). An empty text: no filter.
     */
    void reduce_weapon_cooldowns(Entity& character, float part, std::string_view only, std::string_view except = {});
    /**
     * @return The envelope of the movement of the cast of a character at a time (stage 3.3b-S4): the distance and the speed
     *         that the position check accepts in addition for its dash (World::report_position). Zero: no such cast, or its
     *         envelope ended (its length and one window of the check after it).
     */
    [[nodiscard]] MotionAllowance motion_allowance(const Entity& character, WorldClock::time_point now) const;
    /**
     * The world accepted a position of the client of a character (World::report_position): in the movement of a cast its
     * observers get the movement state `motion` (at most each ability_rules::kMotionMark). The owner gets nothing.
     */
    void caster_moved(Entity& character);
    /**
     * A creature hit a character (Health::creature_hit, after the damage): in the window of a counter of its cast (Riposte,
     * stage 3.3b-S4) the counter is accepted one time: the window ends, the server starts the counter action, and the
     * counter phase hits the attacker at its time when it is still in `reach` of the character. No window: nothing.
     * @return True when the hit started a counter.
     */
    bool countered(Entity& character, Entity& attacker, float reach);
    /**
     * The weapon in the hand of a character changed (Combat::select_weapon, Inventory::finish): the window of a counter of its cast ends when the
     * weapon of the cast is not in the hand (code review of stage 3.3b-S4, finding F2), and the shots of the volley of the cast that did not
     * fly end (code review of stage B1, finding B1-R1).
     */
    void weapon_changed(Entity& character);
    /** The world calls it in each tick: the mana regeneration of the characters whose mana is not full, and the dashes of the server. */
    void tick();
    /**
     * The burst of a perk around a character (stage 1.3b, gear_rules::kPerkBursts): each creature in the shape of the
     * spell (its Radius, kAreaHeight, and the sight from the character; kAreaMost at most) takes a hit of the damage row
     * of the spell (DamageTableRow) with the weapon in the hand, through Combat::strike.
     */
    void perk_burst(Entity& caster, std::string_view spellId);
    /**
     * Ends each cooldown of a character (a command). The client keeps its own cooldown (live check I8-05 of stage 1.3b):
     * a press before its end stays refused, with `client` false.
     * @param client True: the cooldown of the client ends too (a test client that has no cooldown of its own).
     * @return False: the store has no such character in the world and no value.
     */
    bool reset_cooldowns(const std::string& characterId, bool client = false);
    /** @return The cooldowns of a character, with the ability id as the key. */
    [[nodiscard]] const std::map<std::uint32_t, AbilityCooldown>* cooldowns_of(const std::string& characterId) const;

    /**
     * @return The plan of a cast of an ability for the nodes of a character (the rules of this
     *         project, with the ids of the game data). No value: the ability has no rule here.
     */
    [[nodiscard]] static std::optional<CastPlan> plan(std::uint32_t ability, const SkillSet& skills,
                                                      const gamedata::AbilityData* data = nullptr);
    /**
     * @return True when the plan of this backend takes the spell of an action from the spell selections of the action data
     *         (stage 3.3b-S5: Shockwave, Path of Destiny, the projectile of Gravity Well, the Beacon).
     */
    [[nodiscard]] static bool action_selected(std::string_view action);
    /** @return True when the plan of an ability of this backend (stage 3.3a) selects a row or an effect by this node. */
    [[nodiscard]] static bool plan_node(std::string_view node);
    /** @return True when this backend has a plan for the ability with this id text. */
    [[nodiscard]] static bool has_plan(std::string_view ability);

    [[nodiscard]] const AbilityCounts& counts() const {
        return m_counts;
    }

private:
    /** A cast that plays, or whose area phases are not done. */
    struct Cast {
        std::uint64_t number{0};
        CastPlan plan;
        std::uint32_t ability{0};
        std::uint32_t table{0};
        EntityId caster{0};
        std::string characterId;
        SessionId session{0};
        std::uint16_t input{0};
        std::uint32_t sequence{0};
        WorldClock::time_point started{};
        WorldClock::time_point ends{};
        state::WorldPosition place;
        float yaw{0.0F};
        std::string itemId;
        /** The paperdoll slot of the weapon of the cast (finding F1 of the review of stage 1.3b: the gem of that instance). */
        std::uint32_t slot{0};
        std::uint32_t gearScore{0};
        /**
         * The weapon in the hand at the press: its slot and its instance (Combat::weapon_key; code review of stage B1, finding B1-R5: a
         * different instance of the same item in the same slot is a different weapon).
         */
        std::string weaponKey;
        /**
         * The conversion of the gem of the weapon instance at the press (code review of stage 3.3b-S1, finding F3): a
         * later hit of the cast keeps it, also when a different instance of the same item is in the slot then.
         */
        std::string conversionType;
        float conversionPart{0.0F};
        /** The phases and the creatures that each one hurt. */
        std::set<std::pair<std::uint32_t, EntityId>> hits;
        /** Path of Destiny: the hits that took a part of the cooldown away. */
        unsigned cooldownHits{0};
        /** Gravity Well: true after the projectile (a shot of the client, or the throw of the server). */
        bool thrown{false};
        /** The area phases (and the throw) that are not done, and the spell entity of the cast that is in the world (0: none). */
        unsigned pending{0};
        EntityId spell{0};
        /** The input number of a later press of the session (accepted or refused): the contact windows end there. */
        std::optional<std::uint16_t> closedAt;
        /**
         * The input number of a later press of the session that the server refused (code review of stage 3.3b-M2, finding M2-R6): the
         * strikes of the fragment of the cast end there. An accepted press does not set it: the sequence ends the visit of the cast at
         * the start of the next fragment (a buffered press lets the fragment play on to the end of its window).
         */
        std::optional<std::uint16_t> refusedAt;
        /** A counter (stage 3.3b-S4): true after the hit that the window countered (one counter for each cast). */
        bool countered{false};
        /** Stage 3.3b-M2: the creatures that a strike of the cast hit (the pull of Reap tests them on the line to them: follow_strike). */
        std::set<EntityId> struck;
        /** Stage 3.3b-S6: the self cast confirm of an aimed heal (the caster is the target). */
        bool self{false};
        /**
         * Stage B1: the count of the shots of the volley that flew, the shots of the plan that flew (an index of VolleyPlan::shots; each one
         * one time), and the input numbers of the shot items that the volley took (code review of stage B1, finding B1-R2: a shot that did
         * not come does not block a later one).
         */
        std::size_t shotsTaken{0};
        std::vector<bool> shotsDone;
        std::vector<std::uint16_t> shotInputs;
        /**
         * Stage B1: true after the end of the action of the cast before its last shot (a change of the weapon, a travel, a change of the bar
         * or of the nodes, the death of the caster; code review of stage B1, finding B1-R1): the shots that did not fly do not fly.
         */
        bool shotsClosed{false};
    };
    /** The aim of an aimed ability (stage 3.3b-S6): the press that started it. */
    struct Aim {
        AbilityPress press;
        SessionId session{0};
        std::uint32_t ability{0};
        WorldClock::time_point started{};
        /** Stage B1 (Penetrating Shot): the shot item with this attack (the CRC-32 of its lower-case name) confirms the aim, not a click. */
        bool byShot{false};
        std::uint32_t shotAttack{0};
        std::uint32_t shotRow{0};
    };
    /** The values of one character: its cooldowns, its cast, and its aim. */
    struct CharacterState {
        std::map<std::uint32_t, AbilityCooldown> cooldowns;
        std::optional<std::uint64_t> cast;
        std::optional<Aim> aim;
    };

    CastVerdict refuse(CastVerdict verdict);
    /**
     * @return True when the weapon of the cast (its slot and its instance: Cast::weaponKey) is the weapon in the hand of the character (code
     *         review of stage B1, finding B1-R5).
     */
    [[nodiscard]] bool holds_cast_weapon(const Cast& cast, const Entity& character) const;
    /** Ends the aim of a character and the shots of the volley of its cast that did not fly (cancel_aim, died). */
    void end_aim(const std::string& characterId, const char* why);
    /** Stage B1: the shots of the volley of a cast that did not fly end (code review of stage B1, finding B1-R1). */
    void close_volley(Cast& cast, const char* why);
    /** @return The id text of an ability (the node of the progression tables), or an empty text. */
    [[nodiscard]] std::string ability_text(std::uint32_t ability) const;
    /** Puts the cooldowns of a character into its entity and marks the cooldown state. */
    void publish(const std::string& characterId);
    /**
     * Runs an area phase of a cast at its time: a timer of the world, or a phase of the spell entity of the cast
     * (`fromSpell`: the spell counts its phases, and its hits follow the spell, also after the death of the caster).
     */
    void run_area(std::uint64_t cast, std::uint32_t phase, state::WorldPosition center, bool fromSpell = false);
    /** The press for a character in the world, from a session (0: a command). */
    CastVerdict press_entity(Entity& character, SessionId session, const AbilityPress& press);
    /** Gravity Well: the projectile of a cast lands. */
    void throw_projectile(Cast& cast, const state::WorldPosition& origin, const std::array<float, 3>& direction);
    /** The trap, the Beacon (stage 3.3b-S2b): the projectile of the spell of a cast flies (Projectiles::launch_spell). */
    void throw_spell(Cast& cast, const state::WorldPosition& origin, const std::array<float, 3>& direction);
    /** The projectile of the spell of a cast landed: the spell entity (the trap on the ground, the Beacon on the entity that it hit). */
    void spell_landed(std::uint64_t cast, const SpellLanding& landing);
    /** A summon (the Pylon), or an area on the ground at its caster (Sacred Ground, stage 3.3b-S6): its spawn at the time of the plan, with its payment. */
    void spawn_summon(std::uint64_t cast);
    /** The direct heal of a cast at its time (stage 3.3b-S6, Divine Embrace): the target, the payment, the statuses. */
    void cast_heal(std::uint64_t cast);
    /**
     * Stage B1: the shots of a plan with a volley from the ability action of the projectile data (the clips and the casts of the fragment
     * for the nodes of the character, a node override of a row, the explosion row of the slice), and the length of the cast (the last
     * transition to an ability at once). @return False with the missing part in `missing`.
     */
    [[nodiscard]] bool resolve_volley(CastPlan& plan, const Entity& character, std::string& missing) const;
    /**
     * Stage B1: the index of the shot of a volley cast that a shot item fits now, or none (code review of stage B1, findings B1-R1 and B1-R2):
     * a shot of the plan that did not fly, with the attack and the row of the item, whose time window has the time now (from kShotEarly before
     * its time to kShotLate after the end of the action), in a volley that did not end, and an item of an input step that no shot took. Of
     * such shots the item takes the shot whose time is nearest to the input step of the item from the press.
     */
    [[nodiscard]] std::optional<std::size_t> volley_next(const Cast& cast, std::uint32_t attack, std::uint32_t row, std::uint16_t input) const;
    /** Stage B1: the cast of a session that waits for a shot of its volley with this attack and row now, with its weapon in the hand, or null. */
    [[nodiscard]] Cast* volley_cast(SessionId session, const std::string& characterId, std::uint32_t attack, std::uint32_t row, std::uint16_t input,
                                    std::size_t* index);
    /**
     * Stage B1: one shot of a volley flies (Projectiles::launch_spell): a clip, a pierce, or a projectile spell whose collision explodes. A shot
     * that flew, a volley that ended, or a weapon of the cast that is not in the hand: nothing. @param input The input number of the shot item
     * (none: a shot of the server).
     */
    void volley_shot(Cast& cast, std::size_t index, const state::WorldPosition& origin, const std::array<float, 3>& direction,
                     std::optional<std::uint16_t> input = std::nullopt);
    /** Stage B1: the projectile of a volley that explodes ended: at a collision (a creature, a wall, the ground) the explosion entity. */
    void volley_landed(std::uint64_t cast, const SpellLanding& landing);
    /** @return The source of the heals of a cast (stage 3.3b-S6): the caster, its heal base with the weapon of the cast, the row of the spell. */
    [[nodiscard]] HealSource heal_source(const Cast& cast, const Entity& caster, std::string_view spell) const;
    /**
     * @return The target of a direct heal (stage 3.3b-S6): the nearest friend of the caster (World::friendly; not the caster) that lives,
     *         is in the range of the spell (CastDistance) and in sight (Navigation::sees), and that the ability row permits
     *         (TargetStatusEffect with NotEqual: not a target with that status), and that gains health from the heal of `source` (its
     *         health below its maximum, and the heal base, the outgoing factor, and the received factor above 0: code review of stage
     *         3.3b-M1, finding M1-R7); else the caster when the row permits it; else null.
     */
    [[nodiscard]] Entity* heal_target(Entity& caster, const gamedata::SpellRow& spell, std::string_view ability, bool self, const HealSource& source);
    /** @return The weapon of a cast for a hit (its item, its slot, its row, its gear score, the ability). */
    [[nodiscard]] HitWeapon weapon_of(const Cast& cast) const;
    /**
     * The held cooldown of an ability ends (stage 3.3b-S2b, the Pylon). `countdown` true: the summon ended, and the cooldown of
     * the data starts now. False: the spawn failed (no summon, no payment), and the ability is ready at once (research
     * abilities-magic.md: a failed placement starts no summon and no fixed countdown).
     */
    void end_held_cooldown(const std::string& characterId, std::uint32_t ability, bool countdown);
    /**
     * One hit of an area phase, with its damage and its effects. @param center The center of the area: the source of the
     * impact of the hit (stage 3.3b-S4). @return True when the creature took the hit.
     */
    bool area_hit(Cast& cast, std::uint32_t phase, Entity& caster, Entity& target, float factor, const state::WorldPosition& center,
                  std::uint32_t areaCount = 0);
    /** Gives the effects of a phase to a creature that a hit of the phase hurt. */
    void give_effects(Cast& cast, const PhasePlan& phase, Entity& target);
    /** The movement of the caster of an accepted cast starts (stage 3.3b-S4): the actions for the observers, and the dash of the server. */
    void start_motion(Cast& cast, Entity& character);
    /** One step of each dash of the server (a character with no client). */
    void step_dashes();
    /** Marks the state `motion` of a character for its observers. */
    void mark_motion(Entity& character);
    /** The hit of the counter phase of a cast on the attacker of the countered hit (stage 3.3b-S4). */
    void counter_hit(std::uint64_t cast, std::uint32_t phase, EntityId attacker, float reach);

    World& m_world;
    const gamedata::AbilityData* m_data;
    const gamedata::CombatTables* m_combat;
    std::unordered_map<std::string, CharacterState> m_characters;
    std::map<std::uint64_t, Cast> m_casts;
    std::uint64_t m_nextCast{1};
    AbilityCounts m_counts;
    /** The characters whose mana comes back, with the time of the last step of the regeneration. */
    std::map<std::string, WorldClock::time_point> m_regenerating;
    /** A dash of the server: the cast, the character, the start, the direction (east, north), its distance and its time. */
    struct Dash {
        EntityId caster{0};
        std::string characterId;
        state::WorldPosition start;
        std::array<float, 2> direction{};
        float distance{0.0F};
        WorldClock::time_point started{};
        std::chrono::milliseconds time{0};
        float travelled{0.0F};
    };
    std::map<std::uint64_t, Dash> m_dashes;
    /** The characters in the movement of a cast, with the time of the last mark of the state `motion`. */
    std::map<std::string, WorldClock::time_point> m_motionMarks;
    /** The end of the ApplicationCooldown of a status of a row on a creature (stage 3.3b-M2: Gravity, 1 s for each target). */
    std::map<std::pair<EntityId, std::string>, WorldClock::time_point> m_rowCooldowns;
    /** The spell entity of a spell of the data at a character (an action, a fragment, a passive rule; stage 3.3b-M2). */
    void cast_spell_at(Entity& character, std::uint64_t cast, std::string_view spell);
    /** A strike with no damage of a cast at its time (fragment_event): each creature that the cast hit before. */
    void follow_strike(Cast& cast, Entity& caster, const gamedata::AttackStrike& strike);
    /**
     * @return The first part of the fragments that a cast can play that the data does not have (a row of a strike, a status of such a
     *         row, a spell of a cast of the fragment; code review of stage 3.3b-M2, finding M2-R8), or nothing.
     */
    [[nodiscard]] std::optional<std::string> missing_fragment_part(const gamedata::WeaponActionSet& graph, std::uint32_t first,
                                                                   const SkillSet& skills) const;
};

} // namespace soulwarden::backend::world

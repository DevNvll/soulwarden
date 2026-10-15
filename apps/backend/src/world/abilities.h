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
};
inline constexpr std::size_t kCastVerdictCount = 12;

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

/** The plan of a cast: the phases for the nodes of the character. */
struct CastPlan {
    AbilityKind kind{AbilityKind::charge};
    /** The time in which no other cast of the character starts. */
    std::chrono::milliseconds length{0};
    std::vector<PhasePlan> phases;
    /** The movement of the caster (stage 3.3b-S4). No value: the cast does not move its caster. */
    std::optional<CasterMotion> motion;
    /**
     * A counter (stage 3.3b-S4, Riposte): the status row that the cast puts on its caster and that is the window of the
     * counter (Rapier_Riposte_Timer: 1 s, its absorption takes the countered hit). Empty: the cast has no counter.
     */
    std::string window;
    /** The spell entity of the cast (stage 3.3b-S2b). No value: none (or the vortex of Gravity Well, which its phases make). */
    std::optional<SpellPlan> spell;
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
     *         session then gives the item to `shot`, not to the projectile system of the basic attacks.
     */
    [[nodiscard]] bool takes_shot(SessionId session, std::uint32_t attack) const;

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
     * The weapon in the hand of a character changed (Combat::select_weapon): the window of a counter of its cast ends when the
     * weapon of the cast is not in the hand (code review of stage 3.3b-S4, finding F2).
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
        /** A counter (stage 3.3b-S4): true after the hit that the window countered (one counter for each cast). */
        bool countered{false};
    };
    /** The values of one character: its cooldowns and its cast. */
    struct CharacterState {
        std::map<std::uint32_t, AbilityCooldown> cooldowns;
        std::optional<std::uint64_t> cast;
    };

    CastVerdict refuse(CastVerdict verdict);
    /** @return True when the weapon of the cast (its item in its slot) is the weapon in the hand of the character. */
    [[nodiscard]] bool holds_cast_weapon(const Cast& cast, const Entity& character) const;
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
    /** A summon (the Pylon): its spawn at the time of the plan, with its payment. */
    void spawn_summon(std::uint64_t cast);
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
};

} // namespace soulwarden::backend::world

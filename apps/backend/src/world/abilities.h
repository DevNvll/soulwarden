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
//   Effects.  A hit of a phase gives its effects to the creature through the creature system
//             (Creatures::apply_effect): a stun, a root, a slow, a weaken, a knockdown.
//
// The phases, their windows and times, their damage rows, and their effects are rules of this
// project (ability_rules below and Abilities::plan), with the values of the game data where
// the data has a value. World thread only.

namespace soulwarden::backend::world {

class World;

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
} // namespace ability_rules

/** The abilities that this backend has (the build of stage 3.3a): the great axe and the war hammer. */
enum class AbilityKind : std::uint8_t {
    charge,
    maelstrom,
    gravityWell,
    wreckingBall,
    shockwave,
    pathOfDestiny,
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
};

/** The plan of a cast: the phases for the nodes of the character. */
struct CastPlan {
    AbilityKind kind{AbilityKind::charge};
    /** The time in which no other cast of the character starts. */
    std::chrono::milliseconds length{0};
    std::vector<PhasePlan> phases;
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
    /** A session gives a shot of its client: the projectile of a cast of its character. */
    void shot(SessionId session, const AbilityShot& shot);

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
    /** The world calls it in each tick: the mana regeneration of the characters whose mana is not full. */
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
    [[nodiscard]] static std::optional<CastPlan> plan(std::uint32_t ability, const SkillSet& skills);

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
        /** The phases and the creatures that each one hurt. */
        std::set<std::pair<std::uint32_t, EntityId>> hits;
        /** Path of Destiny: the hits that took a part of the cooldown away. */
        unsigned cooldownHits{0};
        /** Gravity Well: true after the projectile (a shot of the client, or the throw of the server). */
        bool thrown{false};
        /** The area phases (and the throw) that are not done. */
        unsigned pending{0};
        /** The input number of a later press of the session (accepted or refused): the contact windows end there. */
        std::optional<std::uint16_t> closedAt;
    };
    /** The values of one character: its cooldowns and its cast. */
    struct CharacterState {
        std::map<std::uint32_t, AbilityCooldown> cooldowns;
        std::optional<std::uint64_t> cast;
    };

    CastVerdict refuse(CastVerdict verdict);
    /** @return The id text of an ability (the node of the progression tables), or an empty text. */
    [[nodiscard]] std::string ability_text(std::uint32_t ability) const;
    /** Puts the cooldowns of a character into its entity and marks the cooldown state. */
    void publish(const std::string& characterId);
    /** Runs the area phases of a cast that are due at this time (a timer of the world). */
    void run_area(std::uint64_t cast, std::uint32_t phase, state::WorldPosition center);
    /** Gravity Well: the projectile of a cast lands. */
    void throw_projectile(Cast& cast, const state::WorldPosition& origin, const std::array<float, 3>& direction);
    /** One hit of an area phase, with its damage and its effects. @return True when the creature took the hit. */
    bool area_hit(Cast& cast, std::uint32_t phase, Entity& caster, Entity& target, float factor);
    /** Gives the effects of a phase to a creature that a hit of the phase hurt. */
    void give_effects(Cast& cast, const PhasePlan& phase, Entity& target);

    World& m_world;
    const gamedata::AbilityData* m_data;
    const gamedata::CombatTables* m_combat;
    std::unordered_map<std::string, CharacterState> m_characters;
    std::map<std::uint64_t, Cast> m_casts;
    std::uint64_t m_nextCast{1};
    AbilityCounts m_counts;
    /** The characters whose mana comes back, with the time of the last step of the regeneration. */
    std::map<std::string, WorldClock::time_point> m_regenerating;
};

} // namespace soulwarden::backend::world

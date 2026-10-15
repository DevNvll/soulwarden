#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "gamedata/combat_tables.h"
#include "gamedata/creature_data.h"
#include "world/entity.h"
#include "world/ids.h"
#include "world/world_clock.h"

// The combat system: a melee attack of a player hurts a creature.
//
// The client finds a melee contact itself: its weapon touched a hit volume of an entity. With
// its setting javelin.enable-client-authoritative-melee on (protocol notes 2.3) it puts each
// contact into its input report. A contact is a claim of a client, not a fact. The session
// gives each claim to this system, which finds the attacker from the session (roadmap, rule
// MP-5) and checks the claim:
//
//   1. Target. The receiver of the contact is a creature of the world, the reference names a
//      component of its slice, the session observes the creature, and the creature lives.
//   2. Weapon. The character has a melee weapon in its hand, and the combat tables have the
//      damage row of the attack.
//   3. Attack. The input of the client has an attack input that fits the contact: the input
//      started some input steps before the contact, and it is on at the contact or went off
//      not long before it (in input steps, and on the world clock).
//   4. One time. One attack hurts one creature one time: a second contact with the same
//      creature a short time after a hit is the same attack.
//   5. Distance. The position of the character (the exact position that the world has) is
//      not farther from the creature than the reach of a melee weapon with a tolerance, and
//      the point of the contact is at the creature.
//   6. Rate. A client makes a limited number of claims in a time window of the world clock.
//
// A claim that passes lowers the health of the creature by the damage of the attack, through
// the creature system (world/creatures.h): each observer gets the health, the reaction of the
// creature, and its death. The session of the attacker gets the damage for the number at the
// creature (SessionSink::damage_dealt). A claim that the check refuses changes nothing.
//
// The check and the damage rule are designs of this project (protocol notes 4.15): the client
// has no copy of the rules of the original server. The damage rule is in one function
// (Combat::damage), with the cells of the game tables as its input.
//
// The values that describe the input of one client are in an AttackTrack, which the session
// of that client owns: its attack inputs, and the counts for the log. The two limits of the
// check (the claims of one time window, and the last hits of each creature) are values of the
// character, not of a session: the combat system keeps them with the character id as their
// key (HitLimits). A new session of a character, and a character that leaves the world and
// enters it again, do not get new limits. World thread only.

namespace soulwarden::backend::world {

class World;

enum class AttackKind : std::uint8_t {
    light,
    /** The attack of an attack input that stays on. */
    heavy,
};

/**
 * The limits of the hit check. Each one is a rule of this project (protocol notes 4.15). The
 * client starts each attack itself, with the action lists of its weapon: the backend has no
 * copy of them. The live data (stage 3.2a, a sword, a great axe, a hatchet, a war hammer, and
 * a spear on a Grey Wolf; an input step is 1/30 s):
 *
 *   A click (the input is on for 1 to 7 steps): one light attack. Its contact comes 5 to 12
 *   steps after the input went off (7 to 19 steps after its start).
 *   A hold (the input is on for 8 steps or more): heavy attacks. The first contact comes 15 to
 *   35 steps after the start. While the input stays on, the client attacks again: contacts 24,
 *   64, and 103 steps after the start (sword), 33 and 81 steps (war hammer). One more attack
 *   follows the end of a long hold: its contact comes 9 to 12 steps after the input went off.
 *   Fast clicks: the contact of a click can come in the step of the next click (hatchet:
 *   contacts 11 to 13 steps apart).
 */
namespace combat_limits {
/** Input steps of the client in one second. */
inline constexpr unsigned kStepsInSecond = 30;
/**
 * An attack is a heavy attack when its attack input stays on for this number of input steps
 * or more. Live data of a sword (12 attacks): the client makes a light attack when the input
 * goes off 8 steps or less after its start (it is on for 7 steps), and a heavy attack when it
 * goes off 9 steps or more after its start (it is on for 8 steps).
 */
inline constexpr unsigned kHeavyHoldSteps = 8;
/** A contact comes this number of input steps after the start of its attack input, or later. */
inline constexpr unsigned kContactStepsMin = 3;
/**
 * A contact comes while its attack input is on, or this number of input steps after the
 * input went off, or sooner. Live data: 17 steps at most (the heavy attack of a spear).
 */
inline constexpr unsigned kReleaseStepsMax = 30;
/**
 * The end of the attack input of a contact is not older than the time of `kReleaseStepsMax`
 * steps plus this value, on the world clock: an input number is a value of the client.
 */
inline constexpr std::chrono::milliseconds kReleaseSlack{500};
/**
 * One attack hurts one creature one time: a contact with a creature that comes less than this
 * number of input steps, or less than this time of the world clock, after a hit of the same
 * creature is the same attack. Live data: two attacks of the fastest chain (hatchet) have 11
 * steps between their contacts.
 */
inline constexpr unsigned kSameAttackSteps = 8;
inline constexpr std::chrono::milliseconds kSameAttackTime{250};
/**
 * The reach of a melee weapon from the position of the character (meters), and the tolerance
 * for the size of the creature and for the age of the position. The distance of the
 * character to the creature is not more than the sum of the two values. The attack shapes of
 * the game are in its animation files, and the tables have no size of a creature. Live data:
 * the largest distance of a contact was 3.9 m (the heavy attack of a spear on a wolf; 2.3 m
 * for a great axe, 2.2 m for a sword).
 */
inline constexpr float kMeleeReach = 3.5F;
inline constexpr float kReachTolerance = 1.5F;
/** The point of a contact is not farther than this from the position of the creature (meters). */
inline constexpr float kPointRange = 4.0F;
/** A client makes this number of claims at most in one time window of the world clock. */
inline constexpr unsigned kClaimsInWindow = 12;
inline constexpr std::chrono::milliseconds kClaimWindow{1000};
/** A report is newer when its input number is 1 to this value above the last one (16 bits, with wrap). */
inline constexpr std::uint16_t kNewerRange = 0x7FFF;
/**
 * When no input of this time had a newer input number, the input numbers of the client
 * started again: the track takes the new sequence, with no attack input.
 */
inline constexpr std::chrono::milliseconds kSequenceRestart{2000};
} // namespace combat_limits

/** One melee contact that a client claims: one item of the third list of an input entry. */
struct HitClaim {
    /** Input number of the entry of the contact. */
    std::uint16_t input{0};
    /** The receiver of the contact: the actor id and the entity group id of the target, and the facet target of its hit volume. */
    ActorId target;
    EntityId targetId{0};
    std::uint64_t facet{0};
    /** The entity group of the receiver. It must be the group of the target. */
    ActorId group;
    EntityId groupId{0};
    /** The point of the contact in the world. */
    state::WorldPosition point;
    /** True when the interest table of the session has the target: the client of the session has the entity. */
    bool observed{false};
};

enum class HitVerdict : std::uint8_t {
    /** The creature took the damage. */
    accepted,
    /** The session is the owner of no entity. */
    noEntity,
    /** The backend has no combat tables (the file of tools/gamedata/combat.sh). */
    noTables,
    /** The client made more claims in the time window than the limit. */
    tooMany,
    /** The target is not a creature of the world: an id that the world does not have, or a player. */
    notCreature,
    /** The reference of the contact does not name a component of the slice of the creature. */
    badReference,
    /** The session does not observe the creature. */
    notObserved,
    /** The creature is dead. */
    dead,
    /** The character has no melee weapon in its hand, or the tables have no damage row for its attack. */
    noWeapon,
    /** The input of the client has no attack input that fits the contact. */
    noAttack,
    /** The attack of the contact did hurt this creature: a second contact of one attack. */
    sameAttack,
    /** The character is too far from the creature. */
    tooFar,
    /** The point of the contact is not at the creature, or it is not a position. */
    badPoint,
    /** The character of the session is dead: it does no damage. */
    attackerDead,
    /** The creature is on its way back (the reset): it takes no damage (world/behavior.h). */
    immune,
};
inline constexpr std::size_t kHitVerdictCount = 15;

/** @return A short text for a log line, for example "too far". */
[[nodiscard]] const char* hit_verdict_text(HitVerdict verdict);

/** The damage of one attack, with the values of each step of the rule. */
struct Damage {
    /** Number of the damage type on the wire. */
    std::uint8_t type{0};
    /** The base damage of the weapon at its gear score. */
    float weapon{0.0F};
    /** The weapon damage with the attributes and the level of the attacker. */
    float core{0.0F};
    /** The damage of the attack before the defense of the target (with the part of the gear). */
    float raw{0.0F};
    /** The damage that the target takes: the two parts together. */
    float dealt{0.0F};
    /** The part of `dealt` that a gem of the weapon converted, and its damage type on the wire. 0: no conversion. */
    float converted{0.0F};
    std::uint8_t convertedType{0};
};

/** What the gear of the attacker and the effects on the target change in one hit (stage 1.3b, world/gear.h). */
struct HitModifiers {
    /** The part of the damage that the gear of the attacker adds (Gear::hit_damage). */
    float part{0.0F};
    /** The DMG<type> parts of the attacker by the name of the damage type. Null: none. */
    const std::unordered_map<std::string, float>* typeParts{nullptr};
    /** The name of the damage type of a gem of the weapon, and the part of the damage that it converts. 0: none. */
    std::string conversionType;
    float conversionPart{0.0F};
    /** The part of the armor of the target that its effects add (a rend: -0.05). */
    float armor{0.0F};
};

/** What the session of an attacker gets for one hit of its character: the number at the target. */
struct DamageDealt {
    EntityId target{0};
    ActorId targetActor;
    /** Number of the damage type on the wire, and the damage that the target took. */
    std::uint8_t type{0};
    float amount{0.0F};
    /** The converted part of the damage (a gem of the weapon), and its damage type: a second entry of the message. 0: none. */
    float secondAmount{0.0F};
    std::uint8_t secondType{0};
    /** True when the hit was the death of the target. */
    bool killed{false};
};

/** The damage of one contact of an attack of a creature on a character, with the values of each step of the rule. */
struct CreatureDamage {
    /** The damage of the creature: the base damage of its level times the damage factor of its kind. */
    float base{0.0F};
    /** The damage of the attack before the level factor and the armor. */
    float raw{0.0F};
    /** The factor of the level difference of the creature and the character. */
    float levelFactor{1.0F};
    /** The armor of the character against the category of the damage type, and the part of the damage that it stops. */
    float armor{0.0F};
    float mitigation{0.0F};
    /** The damage that the character takes. */
    float dealt{0.0F};
};

struct HitResult {
    HitVerdict verdict{HitVerdict::noEntity};
    /** The values of an accepted claim. */
    AttackKind kind{AttackKind::light};
    Damage damage;
    /** The health of the creature before and after the hit. */
    float healthBefore{0.0F};
    float healthAfter{0.0F};
    bool killed{false};
};

/**
 * The values of one client for the check of its hit claims: its attack inputs, its last hits,
 * and the counts. A session owns one. It gives each new input step to the track, and the track
 * to the combat system with each claim.
 */
class AttackTrack {
public:
    /** One attack input of the client: from the step at which the input went on to the step at which it went off. */
    struct Press {
        /** Count of the attack inputs of the session, from 1. */
        std::uint32_t number{0};
        /** Input number at which the attack input went on, and the time at which the session got it. */
        std::uint16_t input{0};
        WorldClock::time_point started{};
        /** Number of input steps for which the attack input stayed on after its start. */
        unsigned holdSteps{0};
        /** True while the attack input is on. */
        bool on{true};
        /** Input number at which the attack input went off, and the time at which the session got it. */
        std::uint16_t endInput{0};
        WorldClock::time_point ended{};
        /**
         * The first input step with the release bit of the attack input ("attack_primary_release") after the start (stage
         * 3.3b-S3): a ranged weapon keeps no hold bit (the client sends "aim_shoot_hold"), thus `on` and `holdSteps` do not
         * measure its hold; the release does.
         */
        bool released{false};
        std::uint16_t releaseInput{0};
    };

    /** @return True when this input number is newer than the last one of the track (or the track has none). */
    [[nodiscard]] bool newer(std::uint16_t input) const;
    /**
     * @return True when the input numbers of the client started again: this number is not
     *         newer, and no newer one came for some time (combat_limits::kSequenceRestart).
     *         Ask this only for the newest entry of a report: an older entry of a report is
     *         an input that the track got before, also after a pause of the reports.
     */
    [[nodiscard]] bool restarted(std::uint16_t input, WorldClock::time_point now) const;

    /**
     * One input step of the client, in the sequence of the input numbers.
     * @param press The attack input came with this step (the pointer button went down).
     * @param hold The attack input stays on. @param release The attack input went off.
     */
    void step(std::uint16_t input, bool press, bool hold, bool release, WorldClock::time_point now);

    /**
     * @return The attack input that a contact of this input number can come from: the newest
     *         one that started `kContactStepsMin` steps or more before it. With fast clicks
     *         that is the input before the newest one. Null: no such input.
     */
    [[nodiscard]] const Press* press_for(std::uint16_t input) const;
    /**
     * @return The attack input of a shot of this input number (stage 3.3b-S3): the newest one that started at this step or
     *         before it. Null: no such input.
     */
    [[nodiscard]] const Press* press_at(std::uint16_t input) const;
    /** @return The newest attack input, or null before the first one. */
    [[nodiscard]] const Press* newest() const {
        return m_current ? &*m_current : nullptr;
    }
    /**
     * @return The number of the input sequence of the client: it is one more each time the
     *         input numbers of the client start again. Two input numbers of one sequence can be
     *         compared.
     */
    [[nodiscard]] std::uint32_t sequence() const {
        return m_sequence;
    }
    /** @return The number of claims with this verdict. */
    [[nodiscard]] std::uint64_t count(HitVerdict verdict) const {
        return m_counts[static_cast<std::size_t>(verdict)];
    }
    /** @return The number of claims that the check refused. */
    [[nodiscard]] std::uint64_t refused() const;
    /** @return The number of attack inputs of the session. */
    [[nodiscard]] std::uint32_t presses() const {
        return m_presses;
    }

private:
    friend class Combat;

    HitVerdict counted(HitVerdict verdict) {
        ++m_counts[static_cast<std::size_t>(verdict)];
        return verdict;
    }

    bool m_hasInput{false};
    std::uint32_t m_sequence{0};
    std::uint16_t m_lastInput{0};
    WorldClock::time_point m_lastStep{};
    /** True while the attack input of the last step had its start mark: an attack input starts at the first such step. */
    bool m_pressed{false};
    /** The newest attack input, and the one before it. */
    std::optional<Press> m_current;
    std::optional<Press> m_previous;
    std::uint32_t m_presses{0};
    std::array<std::uint64_t, kHitVerdictCount> m_counts{};
};

/**
 * The two limits of the hit check for one character: the claims of the time window, and the
 * hits of the last second. They are values of the character (the combat system keeps them
 * with the character id as their key), not of a session: a client that opens a new session
 * for its character gets the limits that the character has.
 */
struct HitLimits {
    /** One hit: the creature, the time of its contact, and the input number of the contact with its session and input sequence. */
    struct Hit {
        EntityId target{0};
        WorldClock::time_point time{};
        std::uint16_t input{0};
        SessionId session{0};
        std::uint32_t sequence{0};
    };

    /** The times of the claims of the time window, oldest first. */
    std::deque<WorldClock::time_point> claims;
    /** The hits of the last second, oldest first. */
    std::deque<Hit> hits;

    /** Counts one claim for the rate rule. @return False when the window has too many. */
    [[nodiscard]] bool admit(WorldClock::time_point now);
    /**
     * @return True when a hit of this creature is so near in time, or in input steps of the
     *         same session and input sequence, that a contact is the same attack.
     */
    [[nodiscard]] bool same_attack(EntityId target, std::uint16_t input, SessionId session, std::uint32_t sequence,
                                   WorldClock::time_point now) const;
    /** Keeps one hit for the rule "one attack hurts one creature one time". */
    void note_hit(const Hit& hit);
    /** @return True when no value of the limits is in its time window: the limits are the limits of a new character. */
    [[nodiscard]] bool idle(WorldClock::time_point now) const;
};

/**
 * The weapon of one hit of a character (Combat::strike). The slot names the instance: a hit uses the gem of the item of
 * that slot, not of the first item with the same id (code review of stage 1.3b, finding F1).
 */
struct HitWeapon {
    std::string itemId;
    /** The paperdoll slot of the item (a weapon slot, or the heart rune). 0: no instance (no gem). */
    std::uint32_t slot{0};
    const gamedata::WeaponRow* row{nullptr};
    std::uint32_t gearScore{0};
    /** The id text of the ability of the cast ("Ability_WarHammer_WreckingBall"). Empty: an attack or a different hit. */
    std::string ability;
};

/**
 * The conversion of the gem of the weapon instance at the press of a cast (code review of stage 3.3b-S1, finding F3): each
 * hit of the cast has it, not the gem of the item that is in the slot at the time of the hit. A part 0: no conversion.
 */
struct CastConversion {
    std::string type;
    float part{0.0F};
};

class Combat {
public:
    /** @param tables The combat tables (game data). Null or not loaded: each claim is refused. */
    Combat(World& world, const gamedata::CombatTables* tables);

    [[nodiscard]] bool has_tables() const {
        return m_tables != nullptr && m_tables->loaded();
    }

    /**
     * A session claims a melee contact of its character. The system finds the character from
     * the session, checks the claim, and for a claim that passes lowers the health of the
     * creature and tells the session of the attacker (SessionSink::damage_dealt).
     * @param track The values of the client of the session for the check. The session owns them.
     */
    HitResult claim(SessionId session, const HitClaim& claim, AttackTrack& track);

    /**
     * One hit of a character on a creature that passed its check: a melee attack (claim) and
     * each hit of an ability (world/abilities.h) come here. The damage of the row (damage()),
     * times `factor`, goes to the creature through the creature system (Creatures::hurt with
     * the attacker: the credit and the rewards of a kill), and the session of the attacker
     * gets the damage number (SessionSink::damage_dealt).
     * @param weapon The weapon of the hit: its item id, its slot, its weapon row (not null), its gear score, and the ability of a cast.
     * @param what The kind of the hit for the log line ("light attack", "Shockwave").
     * @param source The source of the hit for its impact (stage 3.3b-S4: the center of an area or of a spell). Null: the
     *        place of the attacker (a melee hit). A hit that the creature takes and lives moves it by the impact of the
     *        row (gamedata::DamageImpact, Creatures::impact).
     * @return The verdict `accepted` with the values of the hit, or `dead` (the creature took no damage).
     */
    HitResult strike(Entity& attacker, Entity& target, const HitWeapon& weapon, std::uint32_t rowIndex, float factor, const char* what,
                     const CastConversion* cast = nullptr, const state::WorldPosition* source = nullptr);

    /**
     * One hit of a fixed amount of a damage type of a character on a creature (stage 1.3b: a tick of a damage over time
     * of a perk, a hit of a chain): the defense of the creature for the type, then the creature system and the damage
     * number, as `strike`. No perk of the attacker counts (a tick is not a hit of a weapon).
     * @param typeName The name of the damage type of the combat tables ("Corruption").
     */
    HitResult strike_amount(Entity& attacker, Entity& target, float amount, const std::string& typeName, const char* what);

    /** @return The combat tables, or null. */
    [[nodiscard]] const gamedata::CombatTables* tables() const {
        return has_tables() ? m_tables : nullptr;
    }

    /**
     * A client selects the weapon of a key (1 or 2): the weapon slot of the paperdoll with
     * that number, when it has an item. The damage of the next attack is the damage of that
     * weapon.
     */
    void select_weapon(SessionId session, unsigned key);

    /**
     * @return The weapon that a character has in its hand: the item of the weapon slot that
     *         its client selected, or of the first weapon slot with an item. Null: no weapon.
     */
    [[nodiscard]] static const ItemInstance* weapon_in_hand(const Entity& attacker);
    /** @return The paperdoll slot of the weapon in the hand (weapon_in_hand), or no value. */
    [[nodiscard]] static std::optional<std::uint32_t> weapon_slot_in_hand(const Entity& attacker);

    /**
     * The damage rule (a rule of this project; protocol notes 4.15):
     *
     *   weapon = base damage of the weapon row x gear score factor
     *   core   = weapon x (1 + sum of (attribute factor of the weapon x damage value of the
     *            attribute value) + level damage x (level - 1))
     *   raw    = core x factor of the damage row + damage that the row adds
     *   dealt  = raw x (1 - mitigation of the category of the damage type) x (1 + weakness for
     *            the type) x (1 - absorption for the type), each factor not less than 0
     *
     * The gear score factor is (1 + rate) to the power of the number of full intervals of
     * the gear score above the lowest gear score, with the smaller rate for the intervals
     * above the limit. The first three lines are the rule of the client for the damage of a
     * weapon (static proof). The last line, and the use of the mitigation value as a part of
     * the damage, are choices of this project.
     * The gear of the attacker (stage 1.3b, `modifiers`; rules of this project, protocol notes 4.23): raw times (1 +
     * the part of the gear); a gem of the weapon converts a part of raw to its damage type; each part times (1 +
     * the DMG<type> part of the attacker for its type) and the defense of the target for its type; the mitigation
     * of the target times (1 + the armor part of its effects, a rend).
     * @param defense The defense values of the target. Null: the target takes the raw damage.
     * @param modifiers The gear of the attacker and the effects on the target. Null: none.
     */
    [[nodiscard]] Damage damage(const Entity& attacker,
                                const gamedata::WeaponRow& weapon,
                                std::uint32_t gearScore,
                                const gamedata::DamageRow& row,
                                const gamedata::DefenseRow* defense,
                                const HitModifiers* modifiers = nullptr) const;

    /**
     * The damage rule for an attack of a creature on a character (a rule of this project;
     * protocol notes 4.18), in one function:
     *
     *   base   = base damage of the level of the creature x damage factor of its kind
     *   raw    = base x damage factor of the contact + damage that the contact adds
     *   level  = 1 + damage value of the level difference (the level of the creature minus the
     *            level of the character; the table of the game has a value for 6 and more)
     *   armor  = sum, for each armor item on the character: rating factor of the item x
     *            (gear score ^ rating exponent x factor of the item for the damage category),
     *            each product rounded to one decimal
     *   part   = armor / (armor + gear score of the level of the creature ^ mitigation exponent)
     *   dealt  = raw x level x (1 - part)
     *
     * The values are cells of the game tables (the creature data and the combat tables). The
     * rule, and the use of the two exponents, are choices of this project: the client has no
     * copy of the rule of the original server. A damage of the category "True" takes no armor.
     * With no combat tables, or with tables that have no armor values, the armor is 0 and the
     * level factor is 1.
     */
    [[nodiscard]] CreatureDamage creature_damage(const Entity& creature,
                                                 const gamedata::CreatureContact& contact,
                                                 const Entity& target) const;

    /**
     * @return The armor of a character against a damage category: the sum of the armor of the
     *         items in its five armor slots. 0 with no combat tables.
     */
    [[nodiscard]] float armor_of(const Entity& character, gamedata::DamageCategory category) const;

    /**
     * @return The limits of the hit check of a character. They stay when its session ends and
     *         when its entity leaves the world.
     */
    [[nodiscard]] HitLimits& limits_of(const std::string& characterId);

private:
    /** @return The part of a damage of a type that a target with these defense values takes (mitigation, weakness, absorption). */
    [[nodiscard]] float defense_factor(std::uint32_t type, const gamedata::DefenseRow* defense, float armor) const;
    /** Gives the damage of a hit to a creature through the creature system, with the log line and the damage number. */
    HitResult apply_hit(Entity& attacker, Entity& target, const Damage& damage, const std::string& itemId, const std::string& row,
                        const char* what);

    World& m_world;
    const gamedata::CombatTables* m_tables;
    /** Character id to the limits of the character. An entry with no value in its time window goes away. */
    std::unordered_map<std::string, HitLimits> m_limits;
};

} // namespace soulwarden::backend::world

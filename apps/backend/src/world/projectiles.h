#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "gamedata/combat_tables.h"
#include "gamedata/projectile_data.h"
#include "world/combat.h"
#include "world/entity.h"
#include "world/ids.h"
#include "world/world_clock.h"

// The projectiles of the players (stage 3.3b-S3, protocol notes 4.27): the shot of a ranged or magic basic attack (the
// bow, the musket, the fire staff, the ice gauntlet, the life staff, the void gauntlet), its flight, and its hit.
//
// The client starts a shot itself: at the release of a bow, or at the cast marker of a staff, it puts a shot item into the
// second list of its input entry (192 bytes: the origin, the direction, the charge, the ammunition key, the CRC-32 of the
// lower-case ranged attack name, and the CRC-32 of the lower-case damage row; 0 for a magic shot), and it plays its own
// projectile. It reports no later hit: at a hit on a creature, on a wall, or at a miss the client sends nothing (live
// checks L2 and L5 of stage 3.3b-S3: 46 shots, no list item and no other message in the 2 s after each one; the bounded
// client trace of the research, local projectile tick 0x6896620). The one hit that the client reports is the hit near the
// muzzle: a creature in the first meters of the line (m_projectileHitScanRange of the profile, 2 m for the bow) is the
// receiver of the shot item itself (live check L2: the wolf, its facet target, the contact point). Thus the server:
//
//   Launch.  The session gives each shot item of a new input step to this system. The system finds the character from the
//            session, the weapon in its hand, the profile of the weapon (the projectile data), the attack of the item in
//            that profile, and the shot rule of the data that has the attack, the row, and the charge of the item. It
//            checks the press of the attack input of the item (one shot for each press: the ledger), the charge (a heavy
//            shot needs the hold of the data), the ammunition key, the origin (near the character, no wall), and the
//            direction. A refused shot makes no projectile, no cost, and no hit, and it gets no message.
//   Ledger.  Each accepted launch has its entry in the world with the character, the session, its input sequence, and the
//            number of its press as the key: a second shot item of one press (a repeated datagram, many local allocations
//            of the client) is refused. The entry stays some time after the end of its projectile.
//   Flight.  The world follows each projectile on the world clock with the values of the data: the speed and the
//            acceleration of its slice (the acceleration starts after m_projectileAccelDelayDist), the range of the
//            profile, and the life time of the slice. Each step is a segment of 1/30 s at most. No session has a flight
//            timer.
//   Hit.     Each segment looks at the creatures near it (the position index) and at the cells of the navigation data on
//            it. The first creature whose capsule (the capsule of its character, game data) the segment touches, with the
//            radius of the projectile, takes one hit through Combat::strike (the row of the shot, the weapon of the launch,
//            the damage modifier of the ammunition), and the projectile ends. A wall or the ground first: the projectile
//            ends there with no hit. The range or the life time: the projectile ends with no hit (a miss).
//   Near.    A shot item with a receiver: the receiver must be a creature of the world that the session observes, with a
//            reference of its slice, in the near range of the profile from the origin, with no wall between. The hit is at
//            the launch, and no flight follows.
//
// World thread only.

namespace soulwarden::backend::world {

class World;
class AttackTrack;

/** The constants of the projectiles that the game data does not give (rules of this project, each with its source). */
namespace projectile_rules {
/**
 * A heavy shot of a weapon with ammunition (the charged arrow of the bow) needs this many input steps from the press of its
 * attack input to its release (or to the shot): the marker SetChargedAim of the fragment Bow_Charge_UpperBody at 0.5 s (15
 * steps, game data), less one step for the steps of the client (a decision of an agent). Live check L2: a hold of 14 steps
 * gave a light shot, 16 steps a charged one.
 */
inline constexpr unsigned kChargedSteps = 14;
/**
 * A heavy shot of a magic weapon needs this many input steps from the press: the condition Attack_Heavy_NotHeld of the
 * fragment Attack_Start_Charge ends at 0.1667 s (5 steps; game data: player_magicstaff_fire.adb), less one step.
 */
inline constexpr unsigned kMagicHeavySteps = 4;
/** A shot comes this many input steps after the release of its attack input at most (the hip shot of the bow: 10 steps). */
inline constexpr unsigned kAfterRelease = 30;
/** The origin of a shot is this far from the character at most (meters), as for the shot of Gravity Well. */
inline constexpr float kOriginReach = 5.0F;
/** The launch place of the server for its line test of the origin: this high above the feet of the character (meters). */
inline constexpr float kLaunchHeight = 1.5F;
/** The direction of a shot is a unit vector: its length differs from 1 by this at most. */
inline constexpr float kUnitTolerance = 0.01F;
/**
 * The tolerance of a hit for the network delay (meters): the capsule of a creature is this much larger for a projectile. A
 * decision for the user: no source gives the tolerance of the original server (research abilities-ranged.md, D2a).
 */
inline constexpr float kHitTolerance = 0.25F;
/** The point of a hit near the muzzle is this far from the creature at most (meters): the same value as for a melee contact. */
inline constexpr float kNearPointRange = 4.0F;
/** One step of a flight: 1/30 s at most (the input step of the client). */
inline constexpr std::chrono::microseconds kStep{33333};
/** A ledger entry stays this long after the end of its projectile: a repeated shot item of its press is refused in this time. */
inline constexpr std::chrono::seconds kLedgerTime{10};
/**
 * The hold of a heavy shot on the world clock (code review of stage 3.3b-S3, finding F5): the time from the step of the press to
 * the step of the release (or of the shot), as the session got them, is not shorter than the steps of the hold (kStep each) less
 * this time. The input numbers of a client alone do not give the hold. A decision of an agent: the reports of a client can come
 * with a different delay (5 entries in each report, the network).
 */
inline constexpr std::chrono::milliseconds kHoldSlack{200};
/** A character has this many projectiles in the air at most (a limit of the cost, a decision of an agent): one more is refused. */
inline constexpr std::size_t kCharacterMost = 16;
/**
 * The capsule of a creature whose slice has none (2 of 503 creature slices): the size of the agent of the navigation data
 * (radius 0.35 m, height 1.8 m; a project value of stage 3.4d).
 */
inline constexpr float kDefaultRadius = 0.35F;
inline constexpr float kDefaultHeight = 1.1F;
/** The ammunition of an empty slot (client code: the initializers at image RVA 0x221E50 and 0x221E70). */
inline constexpr const char* kDefaultArrow = "DefaultArrow";
inline constexpr const char* kDefaultShot = "DefaultShot";
} // namespace projectile_rules

/** What the system did with a shot. */
enum class ShotVerdict : std::uint8_t {
    accepted,
    /** The session is the owner of no entity. */
    noEntity,
    /** The backend has no projectile data or no combat tables. */
    noData,
    /** The character is dead. */
    dead,
    /** The client shows the loading screen of a travel. */
    locked,
    /** The weapon in the hand has no ranged attack profile. */
    noWeapon,
    /** The profile of the weapon has no attack with the name of the item. */
    unknownAttack,
    /** No shot rule of the data has the attack, the row, and the charge of the item. */
    badRow,
    /** A heavy shot with a hold that is shorter than the hold of the data. */
    badCharge,
    /** The ammunition key is not the ammunition of the weapon. */
    badAmmo,
    /** The item fits no press of the attack input. */
    noAttack,
    /** The press of the item has a projectile (the ledger). */
    repeated,
    /** The origin is not near the character, or a wall is between them. */
    badOrigin,
    /** The direction is not a unit vector. */
    badDirection,
    /** The receiver of a hit near the muzzle is not a creature with this reference, or the profile has no near range. */
    badReference,
    /** The session does not observe the receiver of the hit near the muzzle. */
    notObserved,
    /** The receiver of the hit near the muzzle is not in the near range of the origin, or a wall is between them. */
    nearTooFar,
    /** The character has less mana than the cost of the shot. */
    noMana,
    /** The character has kCharacterMost projectiles in the air. */
    busy,
    /** The attack has no flight (a hit scan) or a grouped spread: the backend has no rule for it (the blunderbuss). */
    unsupported,
};
inline constexpr std::size_t kShotVerdictCount = 20;

/** @return A short text for a log line, for example "the press has a projectile". */
[[nodiscard]] const char* shot_verdict_text(ShotVerdict verdict);

/** One shot item of a client (an item of the second list of its input entry, 192 bytes; protocol notes 4.7 and 4.27). */
struct ShotClaim {
    /** The input number of the entry, the input sequence of the session (AttackTrack::sequence), and the place of the item in its list. */
    std::uint16_t input{0};
    std::uint32_t sequence{0};
    std::uint32_t ordinal{0};
    /** Wire 0 and 12: the origin, and the direction (a unit vector). */
    state::WorldPosition origin;
    std::array<float, 3> direction{};
    /** Wire 172: ChargeValue. Wire 176: the ammunition key. Wire 180: the attack. Wire 184: the damage row (0: a magic shot). */
    float charge{0.0F};
    std::uint32_t ammo{0};
    std::uint32_t attack{0};
    std::uint32_t row{0};
    /** The hit near the muzzle: wire 92 has a receiver (its actor, its entity, and the facet target of its hit volume), and wire 36 its point. */
    bool hasReceiver{false};
    ActorId nearActor;
    EntityId nearId{0};
    std::uint64_t nearFacet{0};
    state::WorldPosition nearPoint;
    /** True when the interest table of the session has the receiver. */
    bool observed{false};
    /** True: the client of the session shows the loading screen of a travel that it did not confirm. */
    bool locked{false};
};

/** A shot that the server makes for a character with no client (development: tools/world shoot). */
struct ServerShot {
    /** The direction: yaw from north, positive to the east (degrees), and pitch, positive up (degrees). */
    float yaw{0.0F};
    float pitch{0.0F};
    /** True: the heavy shot of the weapon (the charged arrow, the charged fire ball). */
    bool heavy{false};
};

/** How a projectile ended. */
enum class ProjectileEnd : std::uint8_t {
    /** It hit a creature in its flight. */
    hit,
    /** It hit a creature near the muzzle at its launch. */
    nearHit,
    /** A wall or the ground stopped it. */
    wall,
    /** It flew its range, or its life time ended (a miss). */
    range,
    lifetime,
    /** Its caster left the world or died: it ended with no hit. */
    caster,
    /** Stage 3.3b-M2: a shield of an enemy of its shooter took it (the projectile shield of Maelstrom with Absorb). */
    absorbed,
};
inline constexpr std::size_t kProjectileEndCount = 7;

/** @return A short text for a log line, for example "a wall stopped it". */
[[nodiscard]] const char* projectile_end_text(ProjectileEnd end);

struct ShotResult {
    ShotVerdict verdict{ShotVerdict::noEntity};
    /** The number of the projectile of an accepted shot (its ledger entry). */
    std::uint64_t projectile{0};
    /** The hit near the muzzle of an accepted shot, with its values. */
    bool nearHit{false};
    HitResult hit;
};

/** The counts of the projectile system, for the log, the development routes, and the tests. */
struct ProjectileCounts {
    std::array<std::uint64_t, kShotVerdictCount> shots{};
    std::array<std::uint64_t, kProjectileEndCount> ends{};
    /** Hits that did damage (a creature can die or be immune between the segment and the hit). */
    std::uint64_t damage{0};
    /** Flight steps, the creatures that the steps looked at, and the largest number of projectiles in the air. */
    std::uint64_t steps{0};
    std::uint64_t looked{0};
    std::size_t largest{0};
    /** Stage B1: the hits of a pierce (Penetrating Shot), and the hits that a pierce did not do again on a creature that it hit before. */
    std::uint64_t pierceHits{0};
    std::uint64_t pierceRepeats{0};
};

/** One projectile in the air, for a development list. */
struct ProjectileView {
    std::uint64_t number{0};
    std::string characterId;
    std::string attack;
    std::string row;
    state::WorldPosition position;
    float travelled{0.0F};
    std::chrono::milliseconds age{0};
};

/** How a projectile of a spell ended (stage 3.3b-S2b): its end, its point, and the entity that it hit (0: none). */
struct SpellLanding {
    ProjectileEnd end{ProjectileEnd::range};
    state::WorldPosition point;
    EntityId struck{0};
};

/**
 * A projectile of a spell that the server launches (stage 3.3b-S2b): the shot of a summon (the Pylon), the projectile of an
 * ability whose client sent the shot item (the Beacon, the trap of the musket).
 */
struct SpellShot {
    /** The character that casts it: the attacker of each hit (the credit, the damage number). */
    EntityId caster{0};
    /** The spell entity that shoots it (the Pylon): it is not a target. 0: the caster shoots it. */
    EntityId source{0};
    /** The projectile spell of an ability (gamedata::ShotRule::ability): the profile, the attack, the row, and the slice. */
    const gamedata::ShotRule* rule{nullptr};
    state::WorldPosition origin;
    /** A unit vector. */
    std::array<float, 3> direction{};
    /** The weapon of the cast and the conversion of its gem at the press. */
    HitWeapon weapon;
    CastConversion conversion;
    /** True: the projectile also hits a character (TargetTypes All of the spell row: the Beacon). Not its caster, not a dead one. */
    bool characters{false};
    /** A creature that a hit hurts takes this entity as its target (the Pylon: OverrideTargetForAiThreat). 0: the caster. */
    EntityId threat{0};
    /** Called at the end of the flight (a hit, a wall, the range, the life time), not at the leave of the caster. */
    std::function<void(const SpellLanding&)> landed;
    /**
     * Stage B1: the projectile passes through creatures (Penetrating Shot): it hits each creature on its line one time, in the order
     * of the line, and ends at a wall, at the range of its profile, or at the life time of its slice. False: it ends at its first hit.
     */
    bool pierce{false};
    /** Stage B1: the damage row of the hits in the place of the row of the rule (a node override: Blood Soaked Arrow). Empty: the row of the rule. */
    std::string row;
};

/**
 * A shot of a creature (stage 3.4f-A, research creature-ranged.md Q2 and Q5): the release of a ranged attack of its slice. The
 * behavior gives it at the time of the release (world/behavior.h); the direction is the aim of the release (the lead, the angle limits,
 * the fall of the slice). The projectile system flies it on the world clock and makes its entity: each session near it gets one record.
 */
struct CreatureShot {
    /** The creature that shoots: the source of each hit, the owner field of the Projectile state. */
    EntityId creature{0};
    /** The release of the data: the profile values, the row, the projectile slice, the keys of the state. */
    const gamedata::RangedRelease* release{nullptr};
    state::WorldPosition origin;
    /** A unit vector. */
    std::array<float, 3> direction{};
    /** The character that the creature aimed at (a log line only: the shot hits the first character on its line). */
    EntityId aimedAt{0};
};

/** How a flight of a creature shot ended (stage 3.4f-A). */
enum class CreatureShotEnd : std::uint8_t {
    /** It hit a character: one damage result of the creature rule. */
    hit,
    /** A wall or the ground stopped it: no damage. */
    wall,
    /** It flew its range or its life time with no contact: no damage (a miss). */
    range,
    lifetime,
    /** Its creature died or left the world in its flight: it ends there, with no damage. */
    source,
    /** A shield of an enemy of the creature took it (the projectile shield of Maelstrom): no damage. */
    absorbed,
};
inline constexpr std::size_t kCreatureShotEndCount = 6;

/** @return A short text for a log line, for example "a wall stopped it". */
[[nodiscard]] const char* creature_shot_end_text(CreatureShotEnd end);

/** The counts of the creature shots (stage 3.4f-A), for the log, the development routes, and the tests. */
struct CreatureShotCounts {
    std::uint64_t launches{0};
    /** Shots that the system refused: no data of the shot, a direction that is not a unit vector, an origin that is not of the world. */
    std::uint64_t refused{0};
    std::array<std::uint64_t, kCreatureShotEndCount> ends{};
    /** Hits that changed the health of a character (a hit on a dead character, or a refused one, does not count). */
    std::uint64_t damage{0};
    /** Contacts with a character in the immune window of its dodge (stage 3.4f-B): no damage, and the shot flies on. */
    std::uint64_t evaded{0};
    /** Flight steps, the characters that the steps looked at, the wall tests of the segments, and the most flights at one time. */
    std::uint64_t steps{0};
    std::uint64_t looked{0};
    std::uint64_t wallTests{0};
    std::size_t largest{0};
};

/** The constants of the creature shots that the game data does not give (rules of this project, each with its source). */
namespace creature_shot_rules {
/**
 * The record of a shot that hit a character stays this long after its collision state, then the stop: the collision state goes to each
 * observer in a bundle before the stop (a session sends the stops of a tick before its records). A decision of an agent: live check L2c
 * shows the arrow stopped at the contact anchor.
 */
inline constexpr std::chrono::milliseconds kHitHold{250};
/**
 * A shot that a wall or the ground stopped stays at its contact for the stuck life time of its slice (m_stuckLifetime, game data: 8 s
 * for basic_ai_arrow), at most this long.
 */
inline constexpr std::chrono::milliseconds kLongestStuck{10000};
/** A creature has this many shots in the air at most (a limit of the cost, a decision of an agent): one more is refused. */
inline constexpr std::size_t kCreatureMost = 8;
} // namespace creature_shot_rules

/** The capsule of a creature in the world (stage 3.3b-S3): the two ends of its segment and its radius. */
struct WorldCapsule {
    state::WorldPosition a;
    state::WorldPosition b;
    float radius{0.0F};
};

/**
 * @return The capsule of a creature at its place: the capsule of its slice (gamedata::CreatureCapsule) turned by the
 *         direction of the creature, with its lowest point at the feet of the creature (a decision of this project), or the
 *         default capsule (projectile_rules) for a slice with none. A character: the default capsule, or `player` when given (stage
 *         3.4f-A: the capsule of the player slice, game data), upright, with its lowest point at its feet.
 */
[[nodiscard]] WorldCapsule capsule_of(const Entity& creature, const gamedata::CreatureCapsule* player = nullptr);

/**
 * @return The head volume of a creature at its place (stage 3.3b-S5): the head capsule of its slice (gamedata::CreatureHead: the
 *         HitVolume with m_isHeadshot on its bone in the bind pose of the skeleton, game data) turned by the direction of the
 *         creature, from its feet. No value: the slice has no head volume (or a creature data file of an older tool).
 */
[[nodiscard]] std::optional<WorldCapsule> head_of(const Entity& creature);

/**
 * @return True when a projectile that touches a creature at `point` with this direction touches its head volume (stage
 *         3.3b-S5, a decision of this project: the server has no animated bones, thus the line of the projectile near the point,
 *         with the radius of the projectile, against the head volume in the bind pose). Only the part of the line to one head
 *         radius after the point counts: the projectile stops at the body (code review of stage 3.3b-S5, finding F7).
 */
[[nodiscard]] bool head_contact(const Entity& creature, const state::WorldPosition& point, const std::array<float, 3>& direction, float radius);

/** @return The distance from a point to the surface of a capsule (0 for a point in it; stage B1: the area of an explosion). */
[[nodiscard]] float capsule_distance(const WorldCapsule& capsule, const state::WorldPosition& point);

/**
 * @return The part of the segment `from` - `to` (0 to 1) at which a sphere of `radius` that moves on it first touches the
 *         capsule. 0 when it touches at `from`. No value: it does not touch.
 */
[[nodiscard]] std::optional<float> sweep(const state::WorldPosition& from, const state::WorldPosition& to, const WorldCapsule& capsule,
                                         float radius);

class Projectiles {
public:
    /**
     * @param data The projectile data (game data). Null or not loaded: each shot is refused.
     * @param combat The combat tables: the weapon and the damage rows.
     */
    Projectiles(World& world, const gamedata::ProjectileData* data, const gamedata::CombatTables* combat);

    [[nodiscard]] bool has_data() const {
        return m_data != nullptr && m_data->loaded() && m_combat != nullptr && m_combat->loaded();
    }

    /**
     * A session gives a shot item of a new input step of its client.
     * @param track The attack inputs of the session (the press of the shot).
     */
    ShotResult launch(SessionId session, const ShotClaim& claim, AttackTrack& track);
    /**
     * Development aid (tools/world shoot): a shot of the server for a character with no client (a remote test character),
     * with the weapon in its hand, from 1.5 m above its feet. The same rules, cost, flight, and hit as the shot of a client.
     */
    ShotResult launch_character(const std::string& characterId, const ServerShot& shot);
    /**
     * A projectile of a spell (stage 3.3b-S2b): the server launches it with the rule of the spell (the speed, the acceleration,
     * the range, the life time, the row), from the origin in the direction. A hit on a creature goes through Combat::strike
     * (a row with the factor 0 does no damage: the toss of the trap); a hit on a character does no damage. The landing gets the
     * point and the entity. The limit kCharacterMost of the caster applies. No ledger entry: the cast of the abilities system
     * has one projectile. From stage B1 also a ranged clip of an ability fragment (the rule has no spell: Penetrating Shot, Rapid
     * Shot): the slice and the damage modifier of the ammunition of an empty slot of the weapon (DefaultArrow), and a pierce.
     */
    ShotResult launch_spell(const SpellShot& shot);
    /** The world calls it in each tick: each projectile in the air flies to the time of the world clock. */
    void tick();
    /** The entity of a character left the world: its projectiles end with no hit. */
    void caster_left(EntityId caster);

    /**
     * A shot of a creature (stage 3.4f-A): the release of a ranged attack. The system makes the projectile entity (each session near
     * it gets its record: the metadata state of the slice and the Projectile state) and flies it on the world clock with the values of
     * the data: the speed and the acceleration of the slice (the acceleration after the delay distance of the attack), the range of the
     * attack, and the life time of the slice. Each segment looks at the characters near it (the position index) and at the cells of
     * the navigation data on it. The first character that lives and that the creatures do not ignore, whose capsule (the capsule of the
     * player slice) the segment touches with the radius of the attack, takes one hit through the health system (Health::creature_hit
     * with the row of the release), and the flight ends with the collision state. A wall or the ground first: the collision state and no
     * hit. The range or the life time: the stop, no hit. The death or the leave of the creature ends its flights with no hit.
     * @return The number of the flight. No value: refused (no data, a direction that is not a unit vector, a place that is not of the
     *         world, kCreatureMost shots of the creature in the air).
     */
    std::optional<std::uint64_t> launch_creature(const CreatureShot& shot);
    /** The creature died or left the world (stage 3.4f-A, a rule of this project): its flights end with no hit, and their records stop. */
    void source_left(EntityId creature);
    [[nodiscard]] const CreatureShotCounts& creature_counts() const {
        return m_creatureCounts;
    }
    /** @return The number of creature shots in the air, and of ended ones whose record waits for its stop. */
    [[nodiscard]] std::size_t creature_flights() const {
        return m_flights.size();
    }
    [[nodiscard]] std::size_t creature_holds() const {
        return m_holds.size();
    }
    /** One creature shot in the air, for a development list. */
    struct FlightView {
        std::uint64_t number{0};
        EntityId creature{0};
        EntityId entity{0};
        std::string attack;
        state::WorldPosition position;
        float travelled{0.0F};
        std::chrono::milliseconds age{0};
    };
    [[nodiscard]] std::vector<FlightView> creature_active() const;

    [[nodiscard]] const ProjectileCounts& counts() const {
        return m_counts;
    }
    /** @return The largest reach of a creature capsule from the place of its creature (meters; stage B1: the query of an area of bodies). */
    [[nodiscard]] float capsule_reach() {
        return reach();
    }
    /** @return The projectiles in the air, in the sequence of their numbers. */
    [[nodiscard]] std::vector<ProjectileView> active() const;
    [[nodiscard]] std::size_t ledger_size() const {
        return m_ledger.size();
    }

private:
    /** The values of one launch that the system checked. */
    struct Launch {
        SessionId session{0};
        std::uint32_t sequence{0};
        std::uint32_t press{0};
        std::uint16_t input{0};
        state::WorldPosition origin;
        std::array<float, 3> direction{};
        const gamedata::RangedAttack* attack{nullptr};
        const gamedata::ShotRule* rule{nullptr};
        const gamedata::ProjectileSlice* slice{nullptr};
        float factor{1.0F};
        /** The receiver of a hit near the muzzle. 0: none. */
        EntityId nearTarget{0};
    };
    /** One projectile in the air. */
    struct Projectile {
        std::uint64_t number{0};
        std::string characterId;
        EntityId caster{0};
        const gamedata::RangedAttack* attack{nullptr};
        const gamedata::ShotRule* rule{nullptr};
        const gamedata::ProjectileSlice* slice{nullptr};
        std::uint32_t rowIndex{0};
        HitWeapon weapon;
        CastConversion conversion;
        float factor{1.0F};
        state::WorldPosition start;
        state::WorldPosition position;
        std::array<float, 3> velocity{};
        float travelled{0.0F};
        WorldClock::time_point launched{};
        WorldClock::time_point at{};
        std::tuple<std::string, SessionId, std::uint32_t, std::uint32_t> key;
        /** A projectile of a spell (stage 3.3b-S2b): its shooter, the threat of its hits, its targets, and its landing. */
        EntityId source{0};
        EntityId threat{0};
        bool characters{false};
        std::function<void(const SpellLanding&)> landed;
        /** Stage B1: a pierce (Penetrating Shot), and the creatures that it hit, in the order of the hits. */
        bool pierce{false};
        std::vector<EntityId> struck;
    };

    ShotVerdict counted(ShotVerdict verdict) {
        ++m_counts.shots[static_cast<std::size_t>(verdict)];
        return verdict;
    }
    /** The checks of the weapon, the attack, the rule, the ammunition, and the slice. */
    ShotVerdict rule_of(const Entity& character, std::uint32_t attackId, std::uint32_t row, float charge, std::uint32_t ammo, Launch& launch) const;
    /** The common end of a launch: the limit, the cost, the ledger, and the hit near the muzzle or the flight. */
    ShotResult fire(Entity& character, const Launch& launch);
    /** Flies one projectile to `now`. @return The end, or no value while it is in the air. */
    std::optional<ProjectileEnd> advance(Projectile& projectile, WorldClock::time_point now);
    /** One hit of a projectile on a creature through Combat::strike. @return The values of the hit. */
    HitResult hit(Projectile& projectile, Entity& target, const state::WorldPosition& point, bool head = false, float part = 0.0F);
    /**
     * The hits of a pierce on one segment (stage B1): each creature whose capsule the segment touches before `limit` (a part of the
     * segment: the wall, or 1), in the order of the line, one time for each projectile, with the hit rate part of its row.
     */
    void pierce_hits(Projectile& projectile, const state::WorldPosition& end, const std::vector<EntityId>& candidates, float limit);
    void finish(const Projectile& projectile, ProjectileEnd end, const state::WorldPosition& at, EntityId struck = 0);

    /** One creature shot in the air (stage 3.4f-A). */
    struct CreatureFlight {
        std::uint64_t number{0};
        EntityId creature{0};
        /** The projectile entity of the record. */
        EntityId entity{0};
        const gamedata::RangedRelease* release{nullptr};
        const gamedata::ProjectileSliceData* slice{nullptr};
        state::WorldPosition position;
        std::array<float, 3> velocity{};
        float travelled{0.0F};
        WorldClock::time_point launched{};
        WorldClock::time_point at{};
        /** The characters that the shot passed in the immune window of their dodge (stage 3.4f-B): not a target of this shot again. */
        std::vector<EntityId> passed;
    };
    /** Flies one creature shot to `now`. @return True when its flight ended. */
    bool fly(CreatureFlight& flight, WorldClock::time_point now);
    /** The end of a creature shot: the counts, the log line, the collision state (a contact), and the time of the stop of its record. */
    void land(const CreatureFlight& flight, CreatureShotEnd end, const state::WorldPosition& at, EntityId struck = 0);

    World& m_world;
    const gamedata::ProjectileData* m_data;
    const gamedata::CombatTables* m_combat;
    std::map<std::uint64_t, Projectile> m_active;
    /** The landings of the projectiles of spells of this tick: called after the flights (a landing makes a spell entity). */
    std::vector<std::pair<std::function<void(const SpellLanding&)>, SpellLanding>> m_landings;
    /** The ledger: the key of each launch (character, session, input sequence, press) to the end of its time. */
    std::map<std::tuple<std::string, SessionId, std::uint32_t, std::uint32_t>, WorldClock::time_point> m_ledger;
    std::uint64_t m_next{1};
    /** The creature shots in the air (stage 3.4f-A), by number, and the projectile entities of ended shots by the time of their stop. */
    std::map<std::uint64_t, CreatureFlight> m_flights;
    std::multimap<WorldClock::time_point, EntityId> m_holds;
    CreatureShotCounts m_creatureCounts;
    /** @return The largest reach of a creature capsule from the place of its creature (meters): a part of the range of a query. */
    float reach();
    float m_reach{0.0F};
    std::size_t m_reachSlices{0};
    ProjectileCounts m_counts;
};

} // namespace soulwarden::backend::world

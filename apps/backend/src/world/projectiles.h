#pragma once

#include <array>
#include <chrono>
#include <cstdint>
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
};
inline constexpr std::size_t kProjectileEndCount = 6;

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

/** The capsule of a creature in the world (stage 3.3b-S3): the two ends of its segment and its radius. */
struct WorldCapsule {
    state::WorldPosition a;
    state::WorldPosition b;
    float radius{0.0F};
};

/**
 * @return The capsule of a creature at its place: the capsule of its slice (gamedata::CreatureCapsule) turned by the
 *         direction of the creature, with its lowest point at the feet of the creature (a decision of this project), or the
 *         default capsule (projectile_rules) for a slice with none.
 */
[[nodiscard]] WorldCapsule capsule_of(const Entity& creature);

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
    ShotResult launch(SessionId session, const ShotClaim& claim, const AttackTrack& track);
    /**
     * Development aid (tools/world shoot): a shot of the server for a character with no client (a remote test character),
     * with the weapon in its hand, from 1.5 m above its feet. The same rules, cost, flight, and hit as the shot of a client.
     */
    ShotResult launch_character(const std::string& characterId, const ServerShot& shot);
    /** The world calls it in each tick: each projectile in the air flies to the time of the world clock. */
    void tick();
    /** The entity of a character left the world: its projectiles end with no hit. */
    void caster_left(EntityId caster);

    [[nodiscard]] const ProjectileCounts& counts() const {
        return m_counts;
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
    HitResult hit(Projectile& projectile, Entity& target, const state::WorldPosition& point);
    void finish(const Projectile& projectile, ProjectileEnd end, const state::WorldPosition& at);

    World& m_world;
    const gamedata::ProjectileData* m_data;
    const gamedata::CombatTables* m_combat;
    std::map<std::uint64_t, Projectile> m_active;
    /** The ledger: the key of each launch (character, session, input sequence, press) to the end of its time. */
    std::map<std::tuple<std::string, SessionId, std::uint32_t, std::uint32_t>, WorldClock::time_point> m_ledger;
    std::uint64_t m_next{1};
    /** @return The largest reach of a creature capsule from the place of its creature (meters): a part of the range of a query. */
    float reach();
    float m_reach{0.0F};
    std::size_t m_reachSlices{0};
    ProjectileCounts m_counts;
};

} // namespace soulwarden::backend::world

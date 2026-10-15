#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// The projectile data (stage 3.3b-S3, protocol notes 4.27): what the server needs to accept the shot of a player and to
// follow its projectile. The ranged attack profiles of the game (the range, the radius of the projectile, the range of the
// hit near the muzzle, the pierce, bounce, and fall values), the flight values of each projectile slice, the ammunition,
// the profile of each weapon, and the shots that the basic attacks of a weapon make (the joins of the attack, the damage
// row, the charge, the projectile slice, the spell, and the mana cost).
//
// The data has the values of the game files and no rule: the rules of a projectile are in the world (world/projectiles.h).
//
// The data is game data: the tool tools/gamedata/projectiles.sh makes the file from the game copy of the user, and the
// file stays outside the repository. Without the file the backend starts and works: each shot of a ranged or magic basic
// attack is refused (no projectile, no damage).

namespace soulwarden::backend::gamedata {

/** File name of the projectile data in the game data directory of the backend. */
inline constexpr const char* kProjectileDataFile = "projectile-data.json";

/** One attack of a profile of the ranged attack data of the game (rangedattackdatabase.radb). */
struct RangedAttack {
    std::string profile;
    std::string name;
    /** m_isHitScan: the attack has no flight (the client reports its hits in the first list of its input). */
    bool hitScan{false};
    /** m_maxRange, m_projectileRadius (meters). */
    float maxRange{0.0F};
    float radius{0.0F};
    /** m_projectileHitScanRange: the range of the client query near the muzzle at the launch (the hit near the muzzle). */
    float nearRange{0.0F};
    /** m_projectilePenetrationDepth (meters; the use is not known). */
    float penetration{0.0F};
    /** m_projectileAccelDelayDist: the projectile has no acceleration in its first meters (client code at image RVA 0x695CCC0). */
    float accelDelay{0.0F};
    /** m_projectileDamageFalloffDist, m_distForProjectileDamageFalloffMax, m_projectileDamageMaxFalloff (no formula is known). */
    float falloffStart{0.0F};
    float falloffEnd{0.0F};
    float falloffMost{0.0F};
    /** m_bounceCountMax. */
    std::uint32_t bounces{0};
    /** m_destroyProjectileOnBlock, m_destroyProjectileOnUnsuccesfulHit. */
    bool destroyOnBlock{true};
    bool destroyOnMiss{true};
    /** m_predictedProjectileAccel: the acceleration of the visual that the client predicts (not the server projectile). */
    std::array<float, 3> predictedAccel{};
    /** The directions of the grouped spread of the attack (the shotgun of the blunderbuss). 0: no spread. */
    std::uint32_t spreadDirections{0};
};

/** The flight values of the projectile component of a slice. */
struct ProjectileSlice {
    std::string path;
    /** m_baseSpeed, m_extraSpeed (m/s), m_timeForMaxSpeed (s). The use of the last two is not known. */
    float speed{0.0F};
    float extraSpeed{0.0F};
    float timeForMaxSpeed{0.0F};
    /** m_accel (m/s/s, x east, y north, z up). */
    std::array<float, 3> accel{};
    /** m_flyingLifetime, m_stuckLifetime (s). */
    float flyingLifetime{0.0F};
    float stuckLifetime{0.0F};
};

/** One ammunition row (AmmoItemDefinitions). */
struct AmmoRow {
    std::string id;
    /** AmmoType: "Arrow" or "Shot". */
    std::string type;
    float damageModifier{1.0F};
    /** The slice of AmmoPrefabPath (an index into ProjectileTables::slices). -1: none. */
    std::int32_t slice{-1};
};

/** The profile of a weapon (WeaponItemDefinitions). */
struct RangedWeapon {
    std::string weapon;
    std::string profile;
    /** AmmoType of the weapon. Empty: the weapon takes no ammunition (a magic weapon). */
    std::string ammoType;
};

/** One shot of a basic attack: a CAGE-RangedAttack clip of an animation database, or a projectile spell of a magic weapon. */
struct ShotRule {
    std::string profile;
    std::string attack;
    std::string row;
    /** ChargeValue of the clip (0 for a spell). */
    float charge{0.0F};
    /** The projectile slice of the shot (an index into ProjectileTables::slices). -1: the slice of the ammunition of the shot. */
    std::int32_t slice{-1};
    /** The spell of a magic shot ("Elemental_Fire_Projectile"), and its ManaCost id. Empty: a clip. */
    std::string spell;
    std::string manaCost;
    /** The damage row has the attack type Heavy. */
    bool heavy{false};
    /** The ammunition slot of the clip (12 arrows, 13 cartridges; 0: no ammunition), and ConsumeAmmo of the clip. */
    std::uint32_t ammoSlot{0};
    bool consume{false};
};

/** The data as plain values: the content of the file, and what a test sets. */
struct ProjectileTables {
    std::vector<RangedAttack> attacks;
    std::vector<ProjectileSlice> slices;
    std::vector<AmmoRow> ammo;
    std::vector<RangedWeapon> weapons;
    std::vector<ShotRule> shots;
};

class ProjectileData {
public:
    /**
     * Reads the file of the tool. A file that is not complete is not used, as a whole: a member of a wrong form, a number
     * that is not finite or not a size, a slice index outside its list, a shot of a profile or an attack that the attacks
     * do not have, two attacks with one name in one profile, and a member "counts" with a number that is not the size of
     * its list.
     * @return False when the file is not there or is not complete. `error()` then has the cause.
     */
    [[nodiscard]] bool load(const std::string& path);
    /** Sets the data with no file (tests), with the same check. @return False, with no data, when it is not correct. */
    [[nodiscard]] bool set(ProjectileTables tables);

    [[nodiscard]] bool loaded() const {
        return !m_tables.attacks.empty();
    }
    [[nodiscard]] const std::string& error() const {
        return m_error;
    }
    [[nodiscard]] const ProjectileTables& tables() const {
        return m_tables;
    }
    /** @return The profile of a weapon (WeaponID; the compare ignores the case of the letters A to Z), or null. */
    [[nodiscard]] const RangedWeapon* weapon(std::string_view weaponId) const;
    /** @return The attack of a profile with the CRC-32 of the lower-case attack name (the shot item of the client), or null. */
    [[nodiscard]] const RangedAttack* attack(std::string_view profile, std::uint32_t attackId) const;
    /** @return The shots of a profile and an attack (the CRC-32 of the lower-case attack name). */
    [[nodiscard]] std::vector<const ShotRule*> shots(std::string_view profile, std::uint32_t attackId) const;
    /** @return The ammunition row with the CRC-32 of its lower-case id (the shot item of the client), or null. */
    [[nodiscard]] const AmmoRow* ammo(std::uint32_t ammoId) const;
    /** @return A slice, or null for -1 or an index outside the list. */
    [[nodiscard]] const ProjectileSlice* slice(std::int32_t index) const;

private:
    ProjectileTables m_tables;
    std::unordered_map<std::string, std::size_t> m_weapons;
    /** Lower-case profile and the attack id to the index of the attack. */
    std::unordered_map<std::string, std::size_t> m_attacks;
    std::unordered_map<std::uint32_t, std::size_t> m_ammo;
    std::string m_error;
};

} // namespace soulwarden::backend::gamedata

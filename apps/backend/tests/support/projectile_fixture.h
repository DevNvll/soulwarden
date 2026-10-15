#pragma once

// The world of the cases of stage 3.3b-S3 (the projectiles of the players) and of the cases that use the same data (the
// corrections of the review of stage 3.3b-S3, the ranged spells of stage 3.3b-S2b): the shop with a bow, a fire staff, their
// rows, and made-up projectile data. All ids, names, and values are made up.

#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include "support/shop_fixture.h"
#include "world/projectiles.h"

namespace soulwarden::backend::tests::projectile {

using namespace shop;

constexpr unsigned kRelease = 567;
constexpr unsigned kShootHold = 315;
constexpr unsigned kAim = 126;
/** The capsule of the wolf of the cases: radius 0.5 m, a segment of 1.0 m on the axis of its body (the form of the game data). */
constexpr float kWolfRadius = 0.5F;
constexpr float kWolfLength = 1.0F;

inline std::uint32_t crc(const char* text) {
    return gamedata::progression_id(text);
}

/** The projectile data of the cases. */
inline gamedata::ProjectileTables projectile_tables() {
    gamedata::ProjectileTables tables;
    const auto attack = [](const char* profile, const char* name, float range, float radius, float nearRange, float accelDelay) {
        gamedata::RangedAttack row;
        row.profile = profile;
        row.name = name;
        row.maxRange = range;
        row.radius = radius;
        row.nearRange = nearRange;
        row.accelDelay = accelDelay;
        row.predictedAccel = {0.0F, 0.0F, -9.8F};
        return row;
    };
    tables.attacks = {attack("TestBowProfile", "Basic", 100.0F, 0.15F, 2.0F, 5.0F), attack("TestBowProfile", "Basic_Heavy", 100.0F, 0.275F, 2.0F, 5.0F),
                      attack("TestBowProfile", "Spread", 14.0F, 0.0F, 0.0F, 0.0F), attack("TestBowProfile", "Scan", 20.0F, 0.0F, 0.0F, 0.0F),
                      attack("TestFireProfile", "Basic", 50.0F, 0.15F, 0.0F, 2.0F), attack("TestFireProfile", "Charged", 50.0F, 0.33F, 0.0F, 2.0F)};
    tables.attacks[2].spreadDirections = 6;
    tables.attacks[3].hitScan = true;
    tables.slices = {{"slices/test/arrow.dynamicslice", 100.0F, 25.0F, 1.0F, {0.0F, 0.0F, -9.8F}, 10.0F, 8.0F},
                     {"slices/test/fire.dynamicslice", 100.0F, 25.0F, 1.0F, {0.0F, 0.0F, 0.0F}, 0.5F, 0.0F}};
    tables.ammo = {{"DefaultArrow", "Arrow", 1.2F, 0}, {"DefaultShot", "Shot", 1.2F, -1}};
    tables.weapons = {{"TestBowRow", "TestBowProfile", "Arrow"}, {"TestStaffRow", "TestFireProfile", ""}};
    tables.shots = {{"TestBowProfile", "Basic", "TestBowLight", 0.0F, -1, "", "", false, 12, true},
                    {"TestBowProfile", "Basic", "TestBowHip", 0.0F, -1, "", "", false, 12, true},
                    {"TestBowProfile", "Basic_Heavy", "TestBowHeavy", 100.0F, -1, "", "", true, 12, true},
                    {"TestBowProfile", "Spread", "TestBowLight", 0.0F, -1, "", "", false, 12, true},
                    {"TestBowProfile", "Scan", "TestBowLight", 0.0F, -1, "", "", false, 12, true},
                    {"TestFireProfile", "Basic", "TestFireLight", 0.0F, 1, "Test_Fire_Projectile", "", false, 0, false},
                    {"TestFireProfile", "Charged", "TestFireHeavy", 0.0F, 1, "Test_Fire_Charged", "Test_Fire_Cost", true, 0, false}};
    return tables;
}

/** The wolf of the cases with the capsule of its character. */
inline gamedata::CreatureSlice capsule_wolf() {
    gamedata::CreatureSlice slice = wolf_slice();
    slice.capsule = gamedata::CreatureCapsule{kWolfRadius, kWolfLength, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 0.0F}};
    return slice;
}

/** The world of a projectile case: the shop with a bow, a fire staff, their rows, and the projectile data. */
struct ProjectileShop : Shop {
    explicit ProjectileShop(bool observe = false) : Shop(false, false, observe) {
        gamedata::CombatData combat = shop_combat();
        const auto row = [&](const char* id, const char* kind, std::uint32_t type, float factor) {
            combat.damageRows.push_back(gamedata::DamageRow{id, kind, type, factor, 0.0F, 0.0F, 0.0F});
            return static_cast<std::uint32_t>(combat.damageRows.size() - 1);
        };
        const std::uint32_t light = row("TestBowLight", "Light", 1, 1.0F);
        const std::uint32_t heavy = row("TestBowHeavy", "Heavy", 1, 1.5F);
        row("TestBowHip", "Light", 1, 0.6F);
        const std::uint32_t fire = row("TestFireLight", "Light", 4, 1.0F);
        const std::uint32_t fireHeavy = row("TestFireHeavy", "Heavy", 4, 1.4F);
        gamedata::WeaponRow bow = combat.weapons.front();
        bow.id = "TestBowRow";
        bow.melee = false;
        bow.lightRow = light;
        bow.heavyRow = heavy;
        bow.mastery = "BowAbilityTable";
        gamedata::WeaponRow staff = bow;
        staff.id = "TestStaffRow";
        staff.lightRow = fire;
        staff.heavyRow = fireHeavy;
        staff.mastery = "FireMagicAbilityTable";
        combat.weapons.push_back(bow);
        combat.items["testbow"] = static_cast<std::uint32_t>(combat.weapons.size() - 1);
        combat.weapons.push_back(staff);
        combat.items["teststaff"] = static_cast<std::uint32_t>(combat.weapons.size() - 1);
        if (!host.combat.set(combat)) {
            throw std::runtime_error("projectile test: the combat tables are not valid: " + host.combat.error());
        }
        std::vector<gamedata::ItemDefinition> items = shop_catalog();
        items.push_back(definition("TestBow", "Weapon", "Weapon+EquippableTwoHand+Bow+Ranged", 5, 100, 600));
        items.push_back(definition("TestStaff", "Weapon", "Weapon+EquippableTwoHand+MagicStaff", 5, 100, 600));
        host.catalog.set(items);
        gamedata::AbilityTables abilities = shop_abilities();
        abilities.manaCosts.push_back({"Test_Fire_Cost", 10.0F});
        if (!host.abilities.set(abilities)) {
            throw std::runtime_error("projectile test: the ability data is not valid: " + host.abilities.error());
        }
        if (!host.projectiles.set(projectile_tables())) {
            throw std::runtime_error("projectile test: the projectile data is not valid: " + host.projectiles.error());
        }
        if (!host.creatures.set_full({capsule_wolf()}, {wolf_kind()}, {}, {{1, kWolfHealth, 100.0F, 50.0F}})) {
            throw std::runtime_error("projectile test: the creature data is not valid");
        }
    }
    /** Puts an item into the first weapon slot of A and selects it. */
    void hold(const char* item) {
        if (!wear(item, world::paperdoll::kMainHand1).ok()) {
            throw std::runtime_error(std::string("projectile test: the item ") + item);
        }
        character()->combat.selectedWeapon = world::paperdoll::kMainHand1;
    }
    /** A wolf `east` m east of A (it looks west), observed by the client of A. */
    world::Entity* wolf_east(float east, float north = 0.0F) {
        world::Entity* entity = host.world.creatures().spawn("Test_Wolf", at(kPlace.x + east, kPlace.y + north), 90.0F).entity;
        walker.idle(20);
        return host.world.creatures().find(entity->id);
    }
    world::Entity* creature(world::EntityId id) {
        return host.world.creatures().find(id);
    }
    /** A shot item of A: from 0.6 m above its feet, to the east (or a direction), with an attack, a row, a charge, and an ammunition key. */
    static InputShotItem item(const char* attack, const char* row, float charge = 0.0F, const char* ammo = "DefaultArrow",
                              std::array<float, 3> direction = {1.0F, 0.0F, 0.0F}) {
        InputShotItem shot;
        shot.origin = {kPlace.x + 0.5F, kPlace.y, kHeight + 0.6F};
        shot.direction = direction;
        shot.attack = crc(attack);
        shot.row = row != nullptr ? crc(row) : 0;
        shot.charge = charge;
        shot.ammo = ammo != nullptr ? crc(ammo) : 0;
        return shot;
    }
    /** A press of the attack input, `held` steps with the hold bits, then the release with the shot item. @return The verdict counts after it. */
    void shoot(const InputShotItem& shot, unsigned held = 2) {
        walker.bits({kActionAttack, kAim});
        for (unsigned step = 1; step < held; ++step) {
            walker.bits({kShootHold, kAim});
        }
        InputEntry entry = walker.entry({kRelease, kAim});
        entry.shotItems = {shot};
        walker.send(entry);
    }
    const world::ProjectileCounts& counts() {
        return host.world.projectiles().counts();
    }
    std::uint64_t verdicts(world::ShotVerdict verdict) {
        return counts().shots[static_cast<std::size_t>(verdict)];
    }
    std::uint64_t ends(world::ProjectileEnd end) {
        return counts().ends[static_cast<std::size_t>(end)];
    }
};

} // namespace soulwarden::backend::tests::projectile

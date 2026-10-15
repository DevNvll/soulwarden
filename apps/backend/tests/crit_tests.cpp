// The critical hits of stage 3.3b-S5 (world/combat.h, Combat::critical; the decisions of the user of 2026-10-15 on the inputs of
// the game data; live check L6 of the damage number).
//
//   CH1  the critical values of the combat data: the weapons and the cells of the rows; a file of an older tool has none (no
//        critical hit and no roll); the refusals of the loader
//   CH2  the roll: one independent number of the generator of the world for each hit below the chance (the weapon, the statuses);
//        a fixed generator gives the expected critical hits, damage, and flags of the damage number (Crit and CanCrit)
//   CH3  the bonus: one additive pool on the multiplier of the weapon (Keen Edge, a status CritDamageModifier), and DmgCoefCrit of
//        a row in the place of the multiplier of the weapon
//   CH4  a hit from behind: the angles of the slice of the target (120 and 135 degrees), the critical hit from behind with no
//        roll, NoBackstab, the source of an area
//   CH5  a headshot: the head volume of the slice in the frame of the creature, the line of a projectile, a critical headshot with
//        the flags Ranged, CanCrit, Crit, Headshot; NoHeadshot; a projectile of the bow on the head and on the body
//   CH6  an arrow of the bow in its flight: on the head of a wolf a critical headshot, on its body a ranged hit
//   CH7  the hits of an area roll; a tick of a damage over time does not
//   CH8  the byte form of the damage number with each flag (the low byte first)
//   CH9  the creature tool: the angles and the head volume of a slice from its skeleton (check_hit_facts_tool.py)
//   CH10 two sessions (MP-10): the facts of a hit are facts of each hit (the place of its attacker), and each session gets only
//        the damage number of its own hit with its own flags

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "support/cases.h"
#include "support/check.h"
#include "support/passive_fixture.h"
#include "support/projectile_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace passive;

/** @return The number that World::random_unit gives first after set_random_seed(seed) (splitmix64: the rule of the world). */
float first_roll(std::uint64_t seed) {
    std::uint64_t value = seed + 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
    value ^= value >> 31;
    return static_cast<float>(value >> 40) * (1.0F / 16777216.0F);
}

/** @return The flags (low byte first) of each damage number that a client got from the message `from` on. */
std::vector<std::uint16_t> flags_of(const FakeClient& client, std::size_t from = 0) {
    std::vector<std::uint16_t> flags;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != server::rep::kTypeDamageDealt) {
            continue;
        }
        server::hub::Reader reader(client.got[index].body);
        reader.u64();
        reader.u64();
        reader.raw(16);
        reader.u64();
        reader.u32();
        reader.u32();
        const std::uint8_t low = reader.u8();
        const std::uint8_t high = reader.u8();
        flags.push_back(static_cast<std::uint16_t>(low | (high << 8)));
    }
    return flags;
}

/** CH1: the critical values of the combat data. */
void crit_data(const Options& options) {
    nlohmann::json document{{"format", 1},
                            {"constants", {{"levelDamage", 0.025}, {"gearScoreRate", 0.0112}, {"gearScoreInterval", 5}, {"gearScoreMinimum", 100},
                                           {"gearScoreLimit", 500}, {"gearScoreSmallerRate", 0.6667}}},
                            {"damageTypes", {{"Slash", 4, "Physical"}}},
                            {"attributes", {{"minimum", 5}, {"strength", {0.0, 1.25}}, {"dexterity", {0.0, 1.25}}, {"intelligence", {0.0, 1.25}},
                                            {"focus", {0.0, 1.25}}}},
                            {"damageRows", {{"Light", "Sword", "Light", "Slash", 1.0, 0.0, 0.0, 0.0}, {"Heavy", "Sword", "Heavy", "Slash", 1.5, 0.0, 0.0, 0.0}}},
                            {"weapons", {{"SwordT1", "Combat Melee", "Sword", 64.0, 0.0, 0.0, 0.0, 0.0, "Light", "Heavy", 0.0, "SwordAbilityTable"}}},
                            {"items", {{"1hswordt1", 0}}},
                            {"defenses", {{0.2, 0.1, nlohmann::json::object(), nlohmann::json::object()}}},
                            {"vitals", {{"wolf", 0}}}};
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "crit-combat.json";
    const auto load = [&](const nlohmann::json& content, gamedata::CombatTables& tables) {
        std::ofstream(path) << content.dump();
        const bool loaded = tables.load(path.string());
        std::filesystem::remove(path);
        return loaded;
    };
    gamedata::CombatTables old;
    check(load(document, old) && !old.weapon_of("1hSwordT1")->critical,
          "a file of the combat tool from before the stage (no criticals and no hit rules) loads; the weapon has no critical values");
    nlohmann::json full = document;
    full["criticals"] = {{"SwordT1", 0.07, 1.3}};
    full["hitRules"] = {{"Light", false, false, true, 1.3, 0.0}, {"Heavy", true, true, false, 1.3, 1.25}};
    gamedata::CombatTables tables;
    check(load(full, tables), "a file with the criticals and the hit rules loads");
    const gamedata::WeaponRow* sword = tables.weapon_of("1hSwordT1");
    const gamedata::DamageRow& heavy = tables.damage_row(*tables.row_index("Heavy"));
    check(sword != nullptr && sword->critical && near(sword->critChance, 0.07F) && near(sword->critMultiplier, 1.3F),
          "the weapon has CritChance 0.07 and CritDamageMultiplier 1.3 (SwordT1 of the data)");
    check(heavy.canCrit && heavy.noHeadshot && !heavy.noBackstab && near(heavy.headFactor, 1.3F) && near(heavy.critFactor, 1.25F)
              && tables.damage_row(*tables.row_index("Light")).noBackstab,
          "each damage row has CanCrit, NoHeadshot, NoBackstab, DmgCoefHead, and DmgCoefCrit");
    const auto refused = [&](const char* what, const std::function<void(nlohmann::json&)>& change) {
        nlohmann::json copy = full;
        change(copy);
        gamedata::CombatTables refusedTables;
        check(!load(copy, refusedTables), std::string("the loader refuses ") + what);
    };
    refused("a critical chance above 1", [](nlohmann::json& copy) { copy["criticals"][0][1] = 1.5; });
    refused("a critical entry of a weapon that the file does not have", [](nlohmann::json& copy) { copy["criticals"][0][0] = "NoWeapon"; });
    refused("a damage row with no hit rule", [](nlohmann::json& copy) { copy["hitRules"].erase(1); });
    refused("only one of the two lists", [](nlohmann::json& copy) { copy.erase("hitRules"); });
    refused("a negative DmgCoefCrit", [](nlohmann::json& copy) { copy["hitRules"][1][5] = -1.0; });
    // With the old file, no critical hit and no number of the generator.
    PassiveShop shop;
    gamedata::CombatData data = crit_combat();
    for (gamedata::WeaponRow& weapon : data.weapons) {
        weapon.critical = false;
    }
    check(shop.host.combat.set(data), "setup: combat tables with no critical values");
    shop.hold("TestAxe");
    world::Entity* wolf = shop.wolf(2);
    shop.give("TSE_CritChance_Inc_10PCT", 60.0F, 10);
    const world::HitResult result = shop.hit(*wolf, "Test_Light", "TestAxe");
    check(result.verdict == world::HitVerdict::accepted && !result.critical.critical && result.critical.roll == 1.0F,
          "a weapon with no critical values: no critical hit and no roll, also with a critical chance of the statuses");
}

/** CH2: the roll with a fixed generator. */
void crit_roll(const Options&) {
    PassiveShop shop;
    shop.hold("TestAxe");
    world::Entity* wolf = shop.wolf(2);
    // A plain hit with the chance 0 (the statuses remove the chance of the weapon: TSE_CritChance_Dec_10PCT is not in the data;
    // the generator gives a number above the chance 0.07 for the seed of the plain hit).
    std::uint64_t plainSeed = 1;
    while (first_roll(plainSeed) < 0.07F) {
        ++plainSeed;
    }
    shop.world().set_random_seed(plainSeed);
    const world::HitResult plain = shop.hit(*wolf, "Test_Light", "TestAxe");
    check(plain.verdict == world::HitVerdict::accepted && !plain.critical.critical && near(plain.critical.chance, 0.07F)
              && near(plain.critical.roll, first_roll(plainSeed), 1e-6F),
          "a hit takes one number of the generator of the world: above the chance 0.07 of the axe (CritChance of 2HAxeT2): no critical hit");
    // Five stacks of TSE_CritChance_Inc_10PCT (CritChanceModifier 0.1 of the data): the chance 0.57.
    check(shop.give("TSE_CritChance_Inc_10PCT", 60.0F, 5) == world::StatusVerdict::renewed && near(shop.character()->gear.critChance, 0.5F),
          "setup: five stacks of a status with CritChanceModifier 0.1 give the critical chance 0.5 of the statuses");
    shop.walker.idle(2);
    const std::size_t from = shop.a.got.size();
    unsigned criticals = 0;
    unsigned expected = 0;
    bool each = true;
    for (std::uint64_t seed = 100; seed < 140; ++seed) {
        shop.world().set_random_seed(seed);
        const float roll = first_roll(seed);
        const world::HitResult result = shop.hit(*wolf, "Test_Light", "TestAxe");
        const bool critical = roll < 0.57F;
        expected += critical ? 1 : 0;
        criticals += result.critical.critical ? 1 : 0;
        const float wanted = plain.damage.dealt * (critical ? 1.35F : 1.0F);
        each = each && result.critical.critical == critical && near(result.critical.roll, roll, 1e-6F) && near(result.critical.chance, 0.57F)
               && near(result.damage.dealt, wanted, 0.05F) && (!critical || std::string(result.critical.cause) == "roll");
    }
    check(each && criticals == expected && expected > 5 && expected < 35,
          "40 hits with fixed seeds: a hit is critical when the number of the generator is below 0.57, and its damage is 1.35 times "
          "the damage (CritDamageMultiplier of 2HAxeT2)");
    shop.walker.idle(2);
    const std::vector<std::uint16_t> flags = flags_of(shop.a, from);
    std::size_t crit = 0;
    std::size_t normal = 0;
    for (const std::uint16_t value : flags) {
        crit += value == (world::damage_flags::kCrit | world::damage_flags::kCanCrit) ? 1 : 0;
        normal += value == 0 ? 1 : 0;
    }
    note("damage numbers " + std::to_string(flags.size()) + ", critical " + std::to_string(crit) + ", normal " + std::to_string(normal)
         + ", expected critical " + std::to_string(expected));
    check(flags.size() == 40 && crit == expected && normal == 40 - expected,
          "the damage number of a critical hit has the flags Crit and CanCrit (60 00), of a normal hit none (00 00)");
    // The chance is not above 1, and with the chance 1 each hit is critical.
    shop.give("TSE_CritChance_Inc_10PCT", 60.0F, 10);
    shop.world().set_random_seed(7);
    const world::HitResult sure = shop.hit(*wolf, "Test_Light", "TestAxe");
    check(sure.critical.critical && near(sure.critical.chance, 1.0F), "fifteen stacks: the chance is 1 (not above), and the hit is critical");
}

/** CH3: the bonus pool. */
void crit_bonus(const Options&) {
    PassiveShop shop;
    shop.hold("TestAxe");
    world::Entity* wolf = shop.wolf(2);
    shop.give("TSE_CritChance_Inc_10PCT", 60.0F, 10);
    const world::HitResult base = shop.hit(*wolf, "Test_Light", "TestAxe");
    check(base.critical.critical && near(base.critical.multiplier, 1.35F), "a critical hit of the axe: the multiplier 1.35 of the weapon");
    shop.learn("Passive_Greataxe_Reaper_Crit");
    const world::HitResult keen = shop.hit(*wolf, "Test_Light", "TestAxe");
    check(keen.critical.critical && near(keen.critical.multiplier, 1.45F) && near(keen.damage.dealt, base.damage.dealt * 1.45F / 1.35F, 0.05F),
          "Keen Edge (Passive_Greataxe_Reaper_Crit, CritDamage 0.1, a fixed stat): the multiplier 1.35 + 0.1 = 1.45");
    // A status with CritDamageModifier of the data (Status_SkillPerk_LifeStaff_Beacon_CritDamage has 0.06): a made-up row here.
    gamedata::AbilityTables tables = passive_tables();
    tables.statusEffects.push_back(status("Test_CritDamage", 10.0F, cells({{"CritDamageModifier", 0.06F}})));
    tables.statusEffects = numbered(tables.statusEffects);
    check(shop.host.abilities.set(tables), "setup: a status with CritDamageModifier 0.06");
    shop.give("Test_CritDamage", 10.0F);
    const world::HitResult pooled = shop.hit(*wolf, "Test_Light", "TestAxe");
    check(near(pooled.critical.multiplier, 1.51F), "one additive pool: 1.35 + 0.1 (the node) + 0.06 (the status) = 1.51, not 1.35 x 1.1 x 1.06");
    const world::HitResult row = shop.hit(*wolf, "Test_CritRow", "TestAxe");
    check(row.critical.critical && near(row.critical.multiplier, 1.5F + 0.1F + 0.06F),
          "a row with DmgCoefCrit 1.5: its coefficient in the place of the multiplier of the weapon, with the same pool");
    // The node counts only for a hit of the tree of the node (the great axe), not for the hammer.
    shop.hold("TestHammer");
    const world::HitResult hammer = shop.hit(*wolf, "Test_Light", "TestHammer");
    check(near(hammer.critical.multiplier, 1.2F + 0.06F), "a critical hit of the hammer: 1.2 + 0.06, no part of the node of the great axe");
}

/** CH4: a hit from behind. */
void crit_behind(const Options&) {
    PassiveShop shop;
    shop.hold("TestAxe");
    // A wolf 2 m east of A that looks west (yaw 90 degrees): A is in front of it. The chance 0 (no roll) for these hits.
    world::Entity* wolf = shop.wolf(2);
    gamedata::CombatData data = crit_combat();
    data.weapons[0].critChance = 0.0F;
    check(shop.host.combat.set(data), "setup: the axe with the chance 0");
    const auto source_at = [&](float degrees) {
        // A source 1.5 m from the wolf at this angle from its front (the front is west).
        const float radians = degrees * 3.14159265F / 180.0F;
        return state::WorldPosition{wolf->position.x - 1.5F * std::cos(radians), wolf->position.y + 1.5F * std::sin(radians), wolf->position.z};
    };
    struct Expect {
        float degrees;
        bool behind;
        bool critical;
    };
    for (const Expect& expect : {Expect{0.0F, false, false}, Expect{119.0F, false, false}, Expect{121.0F, true, false},
                                 Expect{134.0F, true, false}, Expect{136.0F, true, true}, Expect{180.0F, true, true}}) {
        const state::WorldPosition source = source_at(expect.degrees);
        const world::HitResult result = shop.hit(*wolf, "Test_Light", "TestAxe", world::HitKind::contact, 0, "", false, &source);
        check(result.behind == expect.behind && result.critical.critical == expect.critical && result.critical.roll == 1.0F
                  && (!expect.critical || std::string(result.critical.cause) == "behind"),
              "a source at " + std::to_string(static_cast<int>(expect.degrees)) + " degrees from the front: " + (expect.behind ? "from behind" : "not from behind")
                  + (expect.critical ? ", a critical hit (m_isHitByCritFromBehindMinAngle 2.3562 of the slice)" : ", no critical hit")
                  + " (m_isHitFromBehindMinAngle 2.0944)");
    }
    const state::WorldPosition back = source_at(180.0F);
    const world::HitResult guarded = shop.hit(*wolf, "Test_NoBackstab", "TestAxe", world::HitKind::contact, 0, "", false, &back);
    check(guarded.behind && !guarded.critical.critical, "a row with NoBackstab: from behind, and no critical hit");
    // The source of a hit at the creature (an area whose center is the creature): no direction, no hit from behind.
    const state::WorldPosition center = wolf->position;
    const world::HitResult area = shop.hit(*wolf, "Test_Light", "TestAxe", world::HitKind::area, 0, "", false, &center);
    check(!area.behind && !area.critical.critical, "the source at the creature: no hit from behind");
    // The angle helper: the front of a creature with the yaw 90 is west.
    check(near(world::Combat::source_angle(*wolf, at(wolf->position.x - 3.0F, wolf->position.y)), 0.0F, 1e-3F)
              && near(world::Combat::source_angle(*wolf, at(wolf->position.x + 3.0F, wolf->position.y)), 3.14159F, 1e-3F),
          "the yaw 90 degrees looks west: a source to the west is at 0, to the east at pi");
}

/** CH5: a headshot. */
void crit_headshot(const Options&) {
    PassiveShop shop;
    shop.hold("TestAxe");
    world::Entity* wolf = shop.wolf(3);
    gamedata::CombatData data = crit_combat();
    data.weapons[0].critChance = 0.0F;
    check(shop.host.combat.set(data), "setup: the axe with the chance 0");
    // The head volume at the place of the wolf (yaw 90: its front is west, -x): from 0.59 m to 0.91 m in front, 1.02 m to 1.27 m up.
    const std::optional<world::WorldCapsule> head = world::head_of(*wolf);
    check(head && near(head->a.x, wolf->position.x - 0.5915F, 1e-3F) && near(head->b.x, wolf->position.x - 0.9056F, 1e-3F)
              && near(head->a.z, wolf->position.z + 1.0205F, 1e-3F) && near(head->radius, 0.2F),
          "the head volume of the slice (the bind pose of bind_neck_2_jnt) turned to the front of the wolf");
    // A projectile from the west at the height of the head, and one at the height of the legs.
    const state::WorldPosition headPoint{wolf->position.x - 1.2F, wolf->position.y, wolf->position.z + 1.15F};
    const state::WorldPosition legPoint{wolf->position.x - 0.7F, wolf->position.y, wolf->position.z + 0.3F};
    check(world::head_contact(*wolf, headPoint, {1.0F, 0.0F, 0.0F}, 0.05F) && !world::head_contact(*wolf, legPoint, {1.0F, 0.0F, 0.0F}, 0.05F),
          "the line of a projectile at the height of the head touches the head volume; at the height of the legs it does not");
    const std::size_t from = shop.a.got.size();
    const world::HitResult shot = shop.hit(*wolf, "Test_Light", "TestAxe", world::HitKind::projectile, 0, "", true);
    check(shot.head && shot.critical.critical && std::string(shot.critical.cause) == "headshot" && near(shot.critical.multiplier, 1.35F),
          "a projectile on the head volume: a critical hit (the client shows a ranged headshot as a critical number: 0x686CC50, L6)");
    const world::HitResult melee = shop.hit(*wolf, "Test_Light", "TestAxe", world::HitKind::contact, 0, "", true);
    check(!melee.head && !melee.critical.critical, "a melee contact has no headshot (the contact of the client has no bone)");
    const world::HitResult noHead = shop.hit(*wolf, "Test_NoBackstab", "TestAxe", world::HitKind::projectile, 0, "", true);
    check(!noHead.head && !noHead.critical.critical, "a row with NoHeadshot: no headshot");
    const world::HitResult body = shop.hit(*wolf, "Test_Light", "TestAxe", world::HitKind::projectile, 0, "", false);
    check(!body.head && !body.critical.critical, "a projectile on the body: no headshot");
    shop.walker.idle(2);
    const std::vector<std::uint16_t> flags = flags_of(shop.a, from);
    using namespace world::damage_flags;
    check(flags.size() == 4 && flags[0] == (kRanged | kCanCrit | kCrit | kHeadshot) && flags[1] == 0 && flags[2] == kRanged && flags[3] == kRanged,
          "the flags: a critical headshot 61 01 (Ranged, CanCrit, Crit, Headshot), a melee hit 00 00, a projectile on the body 01 00");
}

/** CH5 (flight): a projectile of the bow on the head of a wolf and on its body. */
void crit_headshot_flight(const Options&) {
    using namespace projectile;
    ProjectileShop shop;
    gamedata::CreatureSlice slice = capsule_wolf();
    slice.capsule = gamedata::CreatureCapsule{0.7F, 0.9F, {0.0F, 1.0F, 0.0F}, {0.0F, 0.1F, 0.0F}};
    slice.behind = gamedata::CreatureBehind{2.0944F, 2.3562F};
    slice.head = gamedata::CreatureHead{{0.0F, 0.5915F, 1.0205F}, {0.0F, 0.9056F, 1.2682F}, 0.2F};
    check(shop.host.creatures.set_full({slice}, {wolf_kind()}, {}, {{1, kWolfHealth, 100.0F, 50.0F}}), "setup: the wolf with its head volume");
    gamedata::CombatData data = shop.host.combat.data();
    for (gamedata::WeaponRow& weapon : data.weapons) {
        if (weapon.id == "TestBowRow") {
            weapon.critical = true; // BowT1: 0.08 and 1.3; the chance 0 here (the head only).
            weapon.critChance = 0.0F;
            weapon.critMultiplier = 1.3F;
        }
    }
    check(shop.host.combat.set(data), "setup: the bow with its critical values");
    shop.hold("TestBow");
    world::Entity* wolf = shop.wolf_east(8.0F);
    // The origin of the shot: 0.5 m east of A, 0.6 m up. The head is 0.75 m in front of the wolf (west), 1.15 m up.
    const state::WorldPosition origin{kPlace.x + 0.5F, kPlace.y, kHeight + 0.6F};
    const state::WorldPosition head{wolf->position.x - 0.75F, wolf->position.y, wolf->position.z + 1.15F};
    const float dx = head.x - origin.x;
    const float dz = head.z - origin.z;
    const float size = std::hypot(dx, dz);
    const std::size_t from = shop.a.got.size();
    shop.shoot(ProjectileShop::item("basic", "TestBowLight", 0.0F, "DefaultArrow", {dx / size, 0.0F, dz / size}));
    shop.walker.idle(20);
    const float afterHead = shop.creature(wolf->id)->vitals.health;
    shop.shoot(ProjectileShop::item("basic", "TestBowLight", 0.0F, "DefaultArrow", {1.0F, 0.0F, 0.0F}));
    shop.walker.idle(20);
    const std::vector<std::uint16_t> flags = flags_of(shop.a, from);
    using namespace world::damage_flags;
    check(flags.size() == 2 && flags[0] == (kRanged | kCanCrit | kCrit | kHeadshot) && flags[1] == kRanged,
          "a flight of an arrow to the head of the wolf: a critical headshot (61 01); a flight at 0.6 m: a ranged hit (01 00)");
    const float headDamage = kWolfHealth - afterHead;
    const float bodyDamage = afterHead - shop.creature(wolf->id)->vitals.health;
    check(near(headDamage, bodyDamage * 1.3F, 0.5F), "the damage of the headshot is 1.3 times the damage of the body hit (CritDamageMultiplier of BowT1)");
}

/** CH6: the hits of an ability, of an area, and of a tick. */
void crit_kinds(const Options&) {
    PassiveShop shop;
    shop.hold("TestHammer");
    world::Entity* wolf = shop.wolf(2);
    shop.give("TSE_CritChance_Inc_10PCT", 60.0F, 10);
    const world::HitResult area = shop.hit(*wolf, "Warhammer_Damage_Shockwave", "TestHammer", world::HitKind::area,
                                           world::passive_rules::kAttackCast | 1, "Ability_WarHammer_Shockwave");
    check(area.critical.critical && near(area.critical.multiplier, 1.2F), "a hit of an area of an ability: critical with the chance 1 (multiplier 1.2 of the hammer)");
    // A tick of a damage over time (Combat::strike_amount) is not a hit of a weapon: no roll, no critical hit.
    const float before = shop.world().random_unit();
    (void)before;
    shop.world().set_random_seed(55);
    const world::HitResult tick = shop.world().combat().strike_amount(*shop.character(), *wolf, 50.0F, "Slash", "a test tick");
    check(tick.verdict == world::HitVerdict::accepted && !tick.critical.critical && near(shop.world().random_unit(), first_roll(55), 1e-6F),
          "a tick (strike_amount): no critical hit, and it takes no number of the generator");
}

/** CH7: the byte form of the damage number with each flag. */
void crit_bytes(const Options&) {
    world::DamageDealt dealt;
    dealt.target = 0x40;
    dealt.type = 4;
    dealt.amount = 100.0F;
    dealt.flags = world::damage_flags::kRanged | world::damage_flags::kCanCrit | world::damage_flags::kCrit | world::damage_flags::kHeadshot;
    const server::hub::Bytes body = server::rep::damage_dealt_body(0x2, dealt);
    check(body.size() > 50 && body[48] == 0x61 && body[49] == 0x01, "the flags of a critical ranged headshot: 61 01 (low byte first)");
    dealt.flags = world::damage_flags::kCrit | world::damage_flags::kCanCrit;
    dealt.killed = true;
    const server::hub::Bytes deadly = server::rep::damage_dealt_body(0x2, dealt);
    check(deadly[48] == 0x68 && deadly[49] == 0x00, "a critical hit that was the death: 68 00 (Crit, CanCrit, DamageCausedDeath)");
}

/** CH9: the hit facts of the creature tool (tools/gamedata/check_hit_facts_tool.py): the angles and the head volume of a slice. */
void hit_facts_tool(const Options& options) {
    const std::filesystem::path script = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools/gamedata/check_hit_facts_tool.py";
    std::filesystem::create_directories(options.workDirectory);
    const auto output = options.workDirectory / "check_hit_facts_tool.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + script.lexically_normal().string() + "\" > \"" + output.string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream log(output);
    std::vector<std::string> lines;
    for (std::string line; std::getline(log, line);) {
        note(line.substr(0, 160));
        lines.push_back(line);
    }
    std::filesystem::remove(output);
    const auto passed = [&](const std::string& name) {
        return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) { return line.rfind("ok    " + name + ":", 0) == 0; });
    };
    check(status == 0 && lines.size() == 9, "the check of the hit facts has status 0 and 9 cases (the case cut-angle: code review of stage 3.3b-S5, finding F12)");
    check(passed("valid") && passed("skeleton-as-cdf"), "the angles of the DamageReceiver and the head segment in the frame of the creature");
    check(passed("no-skeleton") && passed("missing-bone") && passed("other-shape"), "no head with no skeleton, no bone, or a box: each one counted");
    check(passed("bad-angle") && passed("bad-chunk") && passed("nonfinite-bone"), "an angle above pi, a bone chunk that is not whole, a NaN: the tool stops");
}

/** CH10: two sessions (MP-10). */
void crit_two_sessions(const Options&) {
    PassiveShop shop(true);
    shop.enter_b();
    world::Entity& a = *shop.character();
    world::Entity* b = shop.world().find_character(kCharacterB);
    check(b != nullptr, "setup: B is in the world");
    shop.hold("TestAxe");
    world::GiveOptions options;
    (void)shop.world().inventory().give(kCharacterB, "TestAxe", 1, options);
    (void)shop.world().inventory().equip(kCharacterB, 0, world::paperdoll::kMainHand1);
    b->combat.selectedWeapon = world::paperdoll::kMainHand1;
    shop.world().gear().refresh(*b);
    // The chance 0: only the place of the attacker decides. The wolf is 2 m east of A and looks west: A is in front of it, and B
    // stands 1.5 m east of the wolf, behind it.
    gamedata::CombatData data = crit_combat();
    data.weapons[0].critChance = 0.0F;
    check(shop.host.combat.set(data), "setup: the axe with the chance 0");
    world::Entity* wolf = shop.wolf(2);
    b->position = state::WorldPosition{wolf->position.x + 1.5F, wolf->position.y, wolf->position.z};
    shop.walker.idle(3);
    shop.b.exchange(shop.host);
    const std::size_t fromA = shop.a.got.size();
    const std::size_t fromB = shop.b.got.size();
    const float before = wolf->vitals.health;
    // One hit of each character in the same tick.
    const world::HitResult hitA = shop.hit(*wolf, "Test_Light", "TestAxe");
    const world::HitResult hitB = shop.hit(*wolf, "Test_Light", "TestAxe", world::HitKind::contact, 0, "", false, nullptr, b);
    check(hitA.verdict == world::HitVerdict::accepted && !hitA.behind && !hitA.critical.critical,
          "the hit of A from the front of the wolf: not from behind, no critical hit");
    check(hitB.verdict == world::HitVerdict::accepted && hitB.behind && hitB.critical.critical && std::string(hitB.critical.cause) == "behind"
              && near(hitB.critical.multiplier, 1.35F),
          "the hit of B from the back of the wolf in the same tick: a critical hit from behind (the place of B, not of A)");
    check(near(wolf->vitals.health, before - hitA.damage.dealt - hitB.damage.dealt, 0.05F),
          "the health of the wolf goes down by the two hits");
    shop.walker.idle(3);
    shop.b.exchange(shop.host);
    const std::vector<std::uint16_t> flagsA = flags_of(shop.a, fromA);
    const std::vector<std::uint16_t> flagsB = flags_of(shop.b, fromB);
    note("damage numbers of A " + std::to_string(flagsA.size()) + ", of B " + std::to_string(flagsB.size()));
    check(flagsA.size() == 1 && flagsA[0] == 0 && flagsB.size() == 1 && flagsB[0] == (world::damage_flags::kCrit | world::damage_flags::kCanCrit),
          "each session gets one damage number, of the hit of its own character: A the normal number (00 00), B the critical number "
          "(60 00)");
    // B in front of the wolf: the same hit of B is not critical, and the critical hit was 1.35 times it.
    b->position = state::WorldPosition{wolf->position.x - 1.5F, wolf->position.y + 0.5F, wolf->position.z};
    const world::HitResult frontB = shop.hit(*wolf, "Test_Light", "TestAxe", world::HitKind::contact, 0, "", false, nullptr, b);
    check(!frontB.behind && !frontB.critical.critical && near(hitB.damage.dealt, frontB.damage.dealt * 1.35F, 0.05F),
          "B from the front: no critical hit; the hit from behind did 1.35 times its damage (CritDamageMultiplier of 2HAxeT2)");
    (void)a;
}

} // namespace

std::vector<Case> crit_cases() {
    return {{"CH1", "critical hits: the critical values of the combat data; an older file has none", crit_data},
            {"CH2", "critical hits: the roll of the generator of the world, the damage, and the flags (fixed seeds)", crit_roll},
            {"CH3", "critical hits: one additive pool of the bonuses on the multiplier of the weapon; DmgCoefCrit of a row", crit_bonus},
            {"CH4", "critical hits: a hit from behind at the angles of the slice; NoBackstab", crit_behind},
            {"CH5", "critical hits: a headshot on the head volume of the slice; NoHeadshot; the flags", crit_headshot},
            {"CH6", "critical hits: an arrow of the bow on the head and on the body (the flight)", crit_headshot_flight},
            {"CH7", "critical hits: the hit of an area rolls, a tick does not", crit_kinds},
            {"CH8", "critical hits: the byte form of the flags of the damage number", crit_bytes},
            {"CH9", "critical hits: the creature tool reads the angles and the head volume of a slice (check_hit_facts_tool.py)", hit_facts_tool},
            {"CH10", "critical hits: two sessions: the facts and the damage number of each hit (MP-10)", crit_two_sessions}};
}

} // namespace soulwarden::backend::tests

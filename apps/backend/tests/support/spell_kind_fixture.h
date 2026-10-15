#pragma once

// The world of the cases of stage 3.3b-S2b (the spell entities of three kinds: the Pylon of the ice gauntlet, the trap of the
// musket, the Beacon of the life staff) and of the cases of the corrections of its code review (stage 1.1b). The slices have the
// structure of the slices of the game (the slots, the delayed events, the AutoSpell, the detection volume, the offense, and the
// timelines of ice_pylon, trap_bear, and spell_life_staff_beacon) with made-up asset ids, rows, and values. All ids, names, and
// values are made up.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "support/projectile_fixture.h"
#include "support/shop_fixture.h"
#include "support/test_navigation.h"
#include "world/projectiles.h"
#include "world/spells.h"

namespace soulwarden::backend::tests::spell_kind {

using namespace shop;
using namespace projectile;
using std::chrono::milliseconds;
using gamedata::spell_event::kActivate;
using gamedata::spell_event::kTrigger;

inline constexpr const char* kIceMagic = "IceMagicAbilityTable";
inline constexpr const char* kMusket = "MusketAbilityTable";
inline constexpr const char* kLifeMagic = "LifeMagicAbilityTable";
inline constexpr const char* kPylonNode = "IceMagic_IcePylon";
inline constexpr const char* kTrapsNode = "Ability_Musket_Traps";
inline constexpr const char* kBeaconNode = "Ability_Lifestaff_Beacon";
inline constexpr const char* kPylon = "IceMagic_Spell_Pylon";
inline constexpr const char* kTrap = "Musket_Trap_Set";
inline constexpr const char* kBeacon = "Spell_Ability_Lifestaff_Beacon_AOE";
inline constexpr const char* kBeaconStatus = "Status_Ability_Lifestaff_Beacon";
inline constexpr const char* kBeaconPotency = "Status_Ability_Lifestaff_Beacon_SG_Potency";
inline constexpr const char* kTrapRoot = "Musket_Trap_Root";
inline constexpr std::uint32_t kPylonSpellSlot = 4;
inline constexpr std::uint32_t kSpellVitalsSlot = 1;
inline constexpr std::uint32_t kTrapPlaceSlot = 4;
inline constexpr std::uint32_t kBeaconSpellSlot = 1;
/** The made-up cooldowns (seconds), the costs, and the vitals rows of the cases. */
inline constexpr float kPylonCooldown = 10.0F;
inline constexpr float kPylonCost = 15.0F;
inline constexpr float kBeaconCost = 16.0F;
inline constexpr float kPylonOwnerPart = 0.2F;
inline constexpr float kTrapHealth = 17.0F;

inline std::array<std::uint8_t, 16> made_up_asset(std::uint8_t mark) {
    std::array<std::uint8_t, 16> asset{};
    asset.fill(mark);
    asset[15] = 1;
    return asset;
}

/** The Pylon slice: the structure of ice_pylon (its life 15 s from its AutoSpell, the shots 1 s and each 2 s, the death chain 1.3 s). */
inline gamedata::SpellSlice pylon_slice() {
    gamedata::SpellSlice slice;
    slice.path = "slices/test/pylon.dynamicslice";
    slice.asset = made_up_asset(0x61);
    slice.root = 0x0000030000000061ULL;
    slice.interestRadius = 50.0F;
    slice.spellSlot = kPylonSpellSlot;
    slice.slots = {{1, "Vitals", 15},        {2, "TemporaryAffiliation", 3563}, {4, "Spell", 2912},         {5, "Ownership", 3217},
                   {9, "DelayedEvent", 3139}, {10, "DelayedEvent", 3139},        {11, "DelayedEvent", 3139}, {12, "DelayedEvent", 3139}};
    slice.events = {{9, 45.0F, kTrigger, "DestroyTrigger", "", false, "DestroyTrigger", true, false},
                    {10, 1.3F, kTrigger, "Destroy", "", true, "DestroyTrigger", false, true},
                    {11, 0.15F, kTrigger, "VFX_Death", "", false, "DestroyTrigger", false, true},
                    {12, 0.05F, kActivate, "VFX_ChargeSrt", "", false, "FireTrigger", false, true}};
    slice.vitals = gamedata::SpellVitals{kSpellVitalsSlot, "TestPylonVitals", "DestroyTrigger"};
    slice.autoCast = gamedata::SpellAutoCast{"IceMagic_Pylon_Projectile", 15.0F, 0.0F, 1.0F, 2.0F, "DestroyTrigger", "FireTrigger", {}};
    slice.volumes = {{"Ice_Pylon", 20.0F, 0.0F}, {"FrostVolume", 1.0F, 0.0F}};
    return slice;
}

/** The trap slice: the structure of trap_bear (a PositionInTheWorld slot, the arm time 0.1 s, the life 30 s, the timeline 0.508 s). */
inline gamedata::SpellSlice trap_slice() {
    gamedata::SpellSlice slice;
    slice.path = "slices/test/trap.dynamicslice";
    slice.asset = made_up_asset(0x62);
    slice.root = 0x0000030000000062ULL;
    slice.interestRadius = 125.0F;
    slice.spellSlot = 0;
    slice.positionSlot = kTrapPlaceSlot;
    slice.slots = {{1, "Vitals", 15},         {3, "Ownership", 3217},    {4, "PositionInTheWorld", 13}, {7, "TemporaryAffiliation", 3563},
                   {8, "DelayedEvent", 3139}, {10, "DelayedEvent", 3139}, {11, "DelayedEvent", 3139},  {12, "Timeline", 1647},
                   {13, "Timeline", 1647}};
    slice.events = {{8, 0.1F, kActivate, "Armed", "", false, "Complete", true, false},
                    {10, 30.0F, kTrigger, "Dissolve", "", false, "Lifetime", true, false},
                    {11, 30.0F, kTrigger, "Trigger", "", false, "Lifetime", true, false}};
    slice.vitals = gamedata::SpellVitals{kSpellVitalsSlot, "TestTrapVitals", "Trigger"};
    slice.volumes = {{"DetectionVolume", 0.5F, 0.1F}};
    slice.offenses = {{"TrapActivate", "Musket_Trap", 1.1F}};
    slice.detectionEvents = {{"Trigger", kTrigger}, {"Dissolve", kTrigger}};
    slice.timelines = {{"Open", -1.0F}, {"Trigger", 0.508F}};
    return slice;
}

/** The Beacon slice: the structure of spell_life_staff_beacon (a Spell slot, activations at the init, no end event of its own). */
inline gamedata::SpellSlice beacon_slice() {
    gamedata::SpellSlice slice;
    slice.path = "slices/test/beacon.dynamicslice";
    slice.asset = made_up_asset(0x63);
    slice.root = 0x0000030000000063ULL;
    slice.interestRadius = 50.0F;
    slice.spellSlot = kBeaconSpellSlot;
    slice.slots = {{1, "Spell", 2912},        {2, "Ownership", 3217},        {3, "TemporaryAffiliation", 3563},
                   {4, "DelayedEvent", 3139}, {5, "DelayedEvent", 3139}, {6, "DelayedEvent", 3139}, {7, "DelayedEvent", 3139}};
    slice.events = {{4, 3.0F, kActivate, "Destroy", "", true, "DelayEvents", true, true},
                    {5, 0.05F, kActivate, "AreaTrigger", "", false, "DelayEvents", true, true},
                    {6, 0.05F, kActivate, "DetectionVolume", "", false, "DelayEvents", true, true},
                    {7, 0.05F, kActivate, "VFX", "", false, "DelayEvents", true, true}};
    slice.volumes = {{"DetectionVolume", 0.0F, 0.05F}};
    return slice;
}

inline gamedata::SpellTables kind_tables() {
    gamedata::SpellTables tables;
    tables.slices = {pylon_slice(), trap_slice(), beacon_slice()};
    tables.spells = {{kPylon, 0}, {kTrap, 1}, {"Musket_Trap_Set_DMG", 1}, {kBeacon, 2}, {"Spell_Ability_Lifestaff_Beacon_AOE_Upgrade", 2}};
    tables.vitals = {{"TestPylonVitals", 59.0F, kPylonOwnerPart, 496.0F, 496.0F, 295.0F}, {"TestTrapVitals", kTrapHealth, 0.0F, 136.0F, 136.0F, 100.0F}};
    return tables;
}

inline gamedata::RangedAttack ranged(const char* profile, const char* name, float range, float radius, float nearRange, float accelDelay) {
    gamedata::RangedAttack row;
    row.profile = profile;
    row.name = name;
    row.maxRange = range;
    row.radius = radius;
    row.nearRange = nearRange;
    row.accelDelay = accelDelay;
    row.predictedAccel = {0.0F, 0.0F, -9.8F};
    return row;
}

/** The projectile data of the cases: the tables of stage 3.3b-S3 and the three ability shots (the 11th value of the tool). */
inline gamedata::ProjectileTables kind_projectiles() {
    gamedata::ProjectileTables tables = projectile_tables();
    tables.attacks.push_back(ranged("TestIceProfile", "Pylon", 50.0F, 0.15F, 0.0F, 0.0F));
    tables.attacks.push_back(ranged("TestMusketProfile", "Traps", 100.0F, 0.33F, 2.0F, 0.0F));
    tables.attacks.push_back(ranged("TestLifeProfile", "Heavy", 50.0F, 0.33F, 2.0F, 2.0F));
    const auto slice = [&](const char* path, float speed, std::array<float, 3> accel, float life) {
        tables.slices.push_back({path, speed, 0.0F, 0.0F, accel, life, 0.0F});
        return static_cast<std::int32_t>(tables.slices.size() - 1);
    };
    const std::int32_t pylonShot = slice("slices/test/pylon_proj.dynamicslice", 100.0F, {0.0F, 0.0F, 0.0F}, 0.5F);
    const std::int32_t toss = slice("slices/test/trap_proj.dynamicslice", 20.0F, {0.0F, 0.0F, -20.0F}, 4.0F);
    const std::int32_t beaconShot = slice("slices/test/beacon_proj.dynamicslice", 100.0F, {0.0F, 0.0F, 0.0F}, 0.5F);
    tables.weapons.push_back({"TestGauntletRow", "TestIceProfile", ""});
    tables.weapons.push_back({"TestMusketRow", "TestMusketProfile", "Shot"});
    tables.weapons.push_back({"TestLifeStaffRow", "TestLifeProfile", ""});
    tables.shots.push_back({"TestIceProfile", "Pylon", "IceMagic_Pylon", 0.0F, pylonShot, "IceMagic_Pylon_Projectile", "", false, 0, false, true});
    tables.shots.push_back({"TestMusketProfile", "Traps", "Musket_Trap_Toss", 0.0F, toss, "Musket_Trap_Toss", "", false, 0, false, true});
    tables.shots.push_back({"TestLifeProfile", "Heavy", "LifeStaff_Beacon", 0.0F, beaconShot, "Spell_Ability_Lifestaff_Beacon_Projectile",
                            "LifeStaff_Beacon", false, 0, false, true});
    tables.shots.push_back({"TestLifeProfile", "Heavy", "LifeStaff_Beacon", 0.0F, beaconShot, "Spell_Ability_Lifestaff_Beacon_Projectile_Upgrade",
                            "LifeStaff_Beacon", false, 0, false, true});
    return tables;
}

/** The ability data of the cases: the shop, the cooldowns, the costs, the statuses, and the spell rows of the three abilities. */
inline gamedata::AbilityTables kind_abilities() {
    gamedata::AbilityTables tables = shop_abilities();
    tables.cooldowns.push_back({kPylonNode, kPylonCooldown, 0.0F});
    tables.cooldowns.push_back({kTrapsNode, 20.0F, 0.0F});
    tables.cooldowns.push_back({kBeaconNode, 35.0F, 0.0F});
    std::vector<gamedata::StatusRow> rows = tables.statusEffects;
    gamedata::StatusRow root = status(kTrapRoot, 3.0F, {}, true);
    root.root = true;
    rows.push_back(root);
    rows.push_back(status(kBeaconStatus, 10.0F, cells({{"TickRate", 1.0F}, {"HealthModifierDamageBased", 0.16F}, {"ShowInUITray", 1.0F}},
                                                      {{"EffectCategories", "Heal+LifestaffBuff"}})));
    rows.push_back(status(kBeaconPotency, 10.0F, cells({{"TickRate", 1.0F}}, {{"EffectPotencyMods", "SacredGroundCat=-0.1"}})));
    tables.statusEffects = numbered(std::move(rows));
    const std::string statuses = std::string(kBeaconStatus) + "," + kBeaconPotency;
    tables.spells.push_back({kPylon, 1.0F, 0.0F, 0.0F, 0, 0.0F, 15000.0F, cells({{"CharRelPosOffsetY", 2.0F}})});
    tables.spells.push_back({"Musket_Trap_Toss", 0.0F, 0.0F, 0.0F, 0, 0.0F, 4000.0F, {}});
    tables.spells.push_back({kTrap, 0.5F, 0.0F, 0.0F, 0, 0.0F, 30000.0F, {}});
    tables.spells.push_back({"Spell_Ability_Lifestaff_Beacon_Projectile", 0.5F, 0.0F, 10.0F, 0, 0.0F, 5000.0F,
                             cells({}, {{"ManaCost", "LifeStaff_Beacon"}, {"SecondarySpellID", kBeacon}, {"TargetTypes", "All"}})});
    tables.spells.push_back({"Spell_Ability_Lifestaff_Beacon_Projectile_Upgrade", 0.5F, 0.0F, 10.0F, 0, 0.0F, 5000.0F,
                             cells({}, {{"ManaCost", "LifeStaff_Beacon"}, {"SecondarySpellID", "Spell_Ability_Lifestaff_Beacon_AOE_Upgrade"},
                                        {"TargetTypes", "All"}})});
    tables.spells.push_back({kBeacon, 3.0F, 0.0F, 0.0F, 0, 0.0F, 10000.0F, cells({}, {{"StatusEffects", statuses}})});
    tables.spells.push_back({"Spell_Ability_Lifestaff_Beacon_AOE_Upgrade", 4.0F, 0.0F, 0.0F, 0, 0.0F, 10000.0F, cells({}, {{"StatusEffects", statuses}})});
    tables.manaCosts.push_back({"IceMagic_IcePylon", kPylonCost});
    tables.manaCosts.push_back({"LifeStaff_Beacon", kBeaconCost});
    return tables;
}

/** The progression data of the cases: the shop and the three weapon trees with the nodes of the three abilities. */
inline gamedata::ProgressionData kind_progression() {
    gamedata::ProgressionData data = shop_progression();
    gamedata::RankTable ranks;
    for (std::uint32_t rank = 0; rank < 20; ++rank) {
        ranks.experience.push_back(10);
        ranks.resetCost.push_back(0);
    }
    const auto tree = [&](const char* table, std::vector<std::pair<const char*, bool>> nodes) {
        data.progressions.push_back({table, 0, gamedata::ProgressionKind::weapon, 19, ranks});
        std::uint32_t column = 0;
        for (const auto& [id, active] : nodes) {
            gamedata::AbilityNode node;
            node.id = id;
            node.table = gamedata::progression_id(table);
            node.column = column++;
            node.active = active;
            data.nodes.push_back(node);
        }
    };
    tree(kIceMagic, {{kPylonNode, true}});
    tree(kMusket, {{kTrapsNode, true}, {"Upgrade_Musket_TrapRend", false}});
    tree(kLifeMagic, {{kBeaconNode, true}, {"Upgrade_Lifestaff_Protection_AoeInc", false}, {"Upgrade_Lifestaff_Protection_DurOnLow", false},
                      {"Upgrade_Lifestaff_Protection_AddHaste", false}});
    return data;
}

/** The world of a case: the projectile shop with an ice gauntlet, a musket, a life staff, their rows, and the data of the three kinds. */
struct KindShop : ProjectileShop {
    explicit KindShop(bool observe = false, bool spellData = true, bool projectileData = true) : ProjectileShop(observe) {
        gamedata::CombatData combat = shop_combat();
        const auto row = [&](const char* id, std::uint32_t type, float factor) {
            combat.damageRows.push_back(gamedata::DamageRow{id, "Ability", type, factor, 0.0F, 0.0F, 0.0F});
        };
        row("IceMagic_Pylon", 4, 0.5F);
        row("Musket_Trap_Toss", 2, 0.0F);
        row("Musket_Trap", 2, 1.0F);
        row("LifeStaff_Beacon", 4, 1.46F);
        const auto weapon = [&](const char* id, const char* item, const char* mastery) {
            gamedata::WeaponRow ranged = combat.weapons.front();
            ranged.id = id;
            ranged.melee = false;
            ranged.mastery = mastery;
            combat.weapons.push_back(ranged);
            combat.items[item] = static_cast<std::uint32_t>(combat.weapons.size() - 1);
        };
        weapon("TestGauntletRow", "testgauntlet", kIceMagic);
        weapon("TestMusketRow", "testmusket", kMusket);
        weapon("TestLifeStaffRow", "testlifestaff", kLifeMagic);
        if (!host.combat.set(combat)) {
            throw std::runtime_error("spell kind test: the combat tables are not valid: " + host.combat.error());
        }
        std::vector<gamedata::ItemDefinition> items = shop_catalog();
        items.push_back(definition("TestGauntlet", "Weapon", "Weapon+EquippableTwoHand+MagicGauntlet", 5, 100, 600));
        items.push_back(definition("TestMusket", "Weapon", "Weapon+EquippableTwoHand+Musket+Ranged", 5, 100, 600));
        items.push_back(definition("TestLifeStaff", "Weapon", "Weapon+EquippableTwoHand+MagicStaff", 5, 100, 600));
        host.catalog.set(items);
        host.progression.set(kind_progression());
        if (!host.abilities.set(kind_abilities())) {
            throw std::runtime_error("spell kind test: the ability data is not valid: " + host.abilities.error());
        }
        if (projectileData && !host.projectiles.set(kind_projectiles())) {
            throw std::runtime_error("spell kind test: the projectile data is not valid: " + host.projectiles.error());
        }
        if (!projectileData) {
            (void)host.projectiles.set({});
        }
        if (spellData && !host.spells.set(kind_tables())) {
            throw std::runtime_error("spell kind test: the spell data is not valid: " + host.spells.error());
        }
    }
    /** The weapon of an ability in the hand of a character, the node (and more nodes of the tree), and the node on place Q. */
    void learn(world::Entity& character, const char* item, const char* table, const char* node, std::vector<const char*> more = {}) {
        world::GiveOptions options;
        options.gearScore = 100;
        const world::ItemResult given = host.world.inventory().give(character.characterId, item, 1, options);
        if (!given.ok() || given.slots.empty()
            || !host.world.inventory().equip(character.characterId, given.slots.front(), world::paperdoll::kMainHand1).ok()) {
            throw std::runtime_error(std::string("spell kind test: the weapon ") + item);
        }
        world::Entity& again = *host.world.find_character(character.characterId);
        again.combat.selectedWeapon = world::paperdoll::kMainHand1;
        const std::uint32_t tree = gamedata::progression_id(table);
        again.skills.pools[tree][gamedata::progression_id(node)].level = 1;
        for (const char* extra : more) {
            again.skills.pools[tree][gamedata::progression_id(extra)].level = 1;
        }
        again.skills.bars[tree] = {gamedata::progression_id(node), 0, 0};
    }
    /** The spell entities of the world. */
    std::vector<world::Entity*> spells() {
        std::vector<world::Entity*> found;
        for (const auto& [id, entity] : host.world.entities()) {
            if (entity.kind == world::EntityKind::spell) {
                found.push_back(host.world.find(id));
            }
        }
        return found;
    }
    world::Entity* spell() {
        const std::vector<world::Entity*> found = spells();
        return found.size() == 1 ? found.front() : nullptr;
    }
    /** Waits (at most `steps` ticks) until a spell entity is in the world. */
    world::Entity* wait_spell(int steps) {
        for (int step = 0; step < steps && spells().empty(); ++step) {
            walker.idle(1);
        }
        return spell();
    }
    /** Presses Q and waits (at most `steps` ticks) until a spell entity is in the world. */
    world::Entity* cast(int steps = 60) {
        walker.press(0);
        return wait_spell(steps);
    }
    /** A shot item of the client of A from 1.5 m above its feet in a direction, with the attack of a ranged attack. */
    void send_shot(const char* attack, std::array<float, 3> direction) {
        InputShotItem shot;
        shot.origin = {kPlace.x - 0.3F, kPlace.y, kHeight + 1.5F};
        const float size = std::hypot(direction[0], direction[1], direction[2]);
        shot.direction = {direction[0] / size, direction[1] / size, direction[2] / size};
        shot.attack = crc(attack);
        InputEntry entry = walker.entry();
        entry.shotItems = {shot};
        walker.send(entry);
    }
    const world::SpellCounts& spell_counts() {
        return host.world.spells().counts();
    }
    std::uint64_t ended(world::SpellEnd cause) {
        return spell_counts().ended[static_cast<std::size_t>(cause)];
    }
    std::uint64_t presses(world::CastVerdict verdict) {
        return host.world.abilities().counts().presses[static_cast<std::size_t>(verdict)];
    }
    const world::AbilityCooldown* cooldown(const char* node) {
        const auto* timers = host.world.abilities().cooldowns_of(kCharacterA);
        if (timers == nullptr) {
            return nullptr;
        }
        const auto found = timers->find(gamedata::progression_id(node));
        return found == timers->end() ? nullptr : &found->second;
    }
    /** B enters near A with a client, and both clients exchange for some ticks. */
    void enter_b_near(const state::WorldPosition& place) {
        beta = add_player(host.store, "76561202255233019", "Tester B", kCharacterB, "Beta", place);
        host.enter(b, beta.registration());
        b_steps(20, place);
    }
    std::uint16_t bInput{100};
    void b_steps(int count, const state::WorldPosition& place) {
        for (int index = 0; index < count; ++index) {
            host.clock.advance(kTick);
            host.datagram(b.peer, b.report_at(bInput++, place.x, place.y, place.z));
            host.tick();
            b.exchange(host);
            a.exchange(host);
        }
    }
    /** The flat ground of the navigation data (a projectile that falls stops on it). */
    void flat_ground(const std::filesystem::path& folder) {
        TestNavRegion region;
        region.write(folder);
        if (!host.navigation.set_directory(folder.string())) {
            throw std::runtime_error("spell kind test: the navigation data");
        }
        walker.idle(10);
    }
};

inline bool has_status(const world::Entity& character, const char* status) {
    return std::any_of(character.statuses.begin(), character.statuses.end(), [&](const world::ActiveStatus& entry) { return entry.status == status; });
}

} // namespace soulwarden::backend::tests::spell_kind

// Cases of stage 3.3b-S2b: the spell entities of three kinds (world/spells.h): the Pylon of the ice gauntlet (a summon), the trap of
// the musket, and the Beacon of the life staff (an aura). The slices of the cases have the structure of the slices of the game (the
// slots, the delayed events, the AutoSpell, the detection volume, the offense, and the timelines of ice_pylon, trap_bear, and
// spell_life_staff_beacon) with made-up asset ids, rows, and values. All ids, names, and values are made up.
//
//   SB1   the spell data of format 2 and the ability shots of the projectile data: the values, the lookups, the refusals, a file
//   SB2   the Pylon: the cast, the spawn at its time in front of the caster, the payment at the spawn, the held cooldown, the health
//         of a part of the caster, the record (Spell state, Vitals state, the delayed events of the init)
//   SB3   the shots of the Pylon (the AutoSpell times, the projectile path), the threat (the creature takes the Pylon as its target
//         and hurts it), the death chain at the health 0, the removal after the chain, the cooldown of the data from the end
//   SB4   the life of the Pylon (the expire chain), the cancel at the leave of the caster, a press with less mana than the payment,
//         a spawn with less mana (no summon)
//   SB5   the trap: the toss, the landing, the arm time, one trigger (the hit and the root), the removal after the trigger
//         timeline; no creature: the end of its life; the record (PositionInTheWorld, no Spell state)
//   SB6   the Beacon: the hit and the attachment, the area follows its target, the statuses of a friend in it (exit, end), the pulses
//         at the TickRate with no heal, the end at the leave of the target and of the caster, the spell of the nodes
//   SB7   two sessions (MP-10): one Pylon, the same Spell state and Vitals state for both; the cooldown only for the caster; the
//         Beacon gives its statuses only to a friend (the fixture pair of World::set_test_friends)
//   SB8   the load: 3,000 far spells of the three kinds and a far crowd of creatures, with a time limit; the crowd adds no look
//   SB9   the routing of a shot item: the shot of a trap goes to the cast; the same item with no cast is not a shot of a basic attack
//   SB10  no spell data, data of format 1, no projectile data: no entity and no payment, a refused press, the backend works

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "server/http/character_commands.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/projectile_fixture.h"
#include "support/shop_fixture.h"
#include "support/test_navigation.h"
#include "world/projectiles.h"
#include "world/spells.h"

namespace soulwarden::backend::tests {
namespace {

using namespace shop;
using namespace projectile;
using nlohmann::json;
using std::chrono::milliseconds;
using gamedata::spell_event::kActivate;
using gamedata::spell_event::kTrigger;

constexpr const char* kIceMagic = "IceMagicAbilityTable";
constexpr const char* kMusket = "MusketAbilityTable";
constexpr const char* kLifeMagic = "LifeMagicAbilityTable";
constexpr const char* kPylonNode = "IceMagic_IcePylon";
constexpr const char* kTrapsNode = "Ability_Musket_Traps";
constexpr const char* kBeaconNode = "Ability_Lifestaff_Beacon";
constexpr const char* kPylon = "IceMagic_Spell_Pylon";
constexpr const char* kTrap = "Musket_Trap_Set";
constexpr const char* kBeacon = "Spell_Ability_Lifestaff_Beacon_AOE";
constexpr const char* kBeaconStatus = "Status_Ability_Lifestaff_Beacon";
constexpr const char* kBeaconPotency = "Status_Ability_Lifestaff_Beacon_SG_Potency";
constexpr const char* kTrapRoot = "Musket_Trap_Root";
constexpr std::uint32_t kPylonSpellSlot = 4;
constexpr std::uint32_t kSpellVitalsSlot = 1;
constexpr std::uint32_t kTrapPlaceSlot = 4;
constexpr std::uint32_t kBeaconSpellSlot = 1;
/** The made-up cooldowns (seconds), the costs, and the vitals rows of the cases. */
constexpr float kPylonCooldown = 10.0F;
constexpr float kPylonCost = 15.0F;
constexpr float kBeaconCost = 16.0F;
constexpr float kPylonOwnerPart = 0.2F;
constexpr float kTrapHealth = 17.0F;

std::array<std::uint8_t, 16> made_up_asset(std::uint8_t mark) {
    std::array<std::uint8_t, 16> asset{};
    asset.fill(mark);
    asset[15] = 1;
    return asset;
}

/** The Pylon slice: the structure of ice_pylon (its life 15 s from its AutoSpell, the shots 1 s and each 2 s, the death chain 1.3 s). */
gamedata::SpellSlice pylon_slice() {
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
gamedata::SpellSlice trap_slice() {
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
gamedata::SpellSlice beacon_slice() {
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

gamedata::SpellTables kind_tables() {
    gamedata::SpellTables tables;
    tables.slices = {pylon_slice(), trap_slice(), beacon_slice()};
    tables.spells = {{kPylon, 0}, {kTrap, 1}, {"Musket_Trap_Set_DMG", 1}, {kBeacon, 2}, {"Spell_Ability_Lifestaff_Beacon_AOE_Upgrade", 2}};
    tables.vitals = {{"TestPylonVitals", 59.0F, kPylonOwnerPart, 496.0F, 496.0F, 295.0F}, {"TestTrapVitals", kTrapHealth, 0.0F, 136.0F, 136.0F, 100.0F}};
    return tables;
}

gamedata::RangedAttack ranged(const char* profile, const char* name, float range, float radius, float nearRange, float accelDelay) {
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
gamedata::ProjectileTables kind_projectiles() {
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
gamedata::AbilityTables kind_abilities() {
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
gamedata::ProgressionData kind_progression() {
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

std::string hex(const Bytes& bytes) {
    static const char* digits = "0123456789abcdef";
    std::string text;
    for (const std::uint8_t byte : bytes) {
        text += digits[byte >> 4];
        text += digits[byte & 15];
    }
    return text;
}

/** @return The health (field 0, f32) of the vitals fragments of slot 1 of a spell that a client got from the message `from` on. */
std::vector<float> spell_healths(const FakeClient& client, std::size_t from) {
    std::vector<float> healths;
    for (const Fragment& fragment : fragments_of(client, kSpellVitalsSlot, server::rep::kTypeVitalsState, from)) {
        if (fragment.fields.contains({0, 0}) && fragment.fields.at({0, 0}).size() == 4) {
            const Bytes& bytes = fragment.fields.at({0, 0});
            const std::uint32_t bits = (static_cast<std::uint32_t>(bytes[0]) << 24) | (static_cast<std::uint32_t>(bytes[1]) << 16)
                                       | (static_cast<std::uint32_t>(bytes[2]) << 8) | bytes[3];
            healths.push_back(std::bit_cast<float>(bits));
        }
    }
    return healths;
}

/** @return True when a fragment of a delayed event slot is in the messages from `from` on, and its completed mark (field 1). */
std::pair<bool, int> delayed_mark(const FakeClient& client, std::uint32_t slot, std::size_t from) {
    const std::vector<Fragment> fragments = fragments_of(client, slot, server::rep::kTypeDelayedEventState, from);
    if (fragments.empty() || !fragments.back().fields.contains({0, 1})) {
        return {false, -1};
    }
    return {true, fragments.back().fields.at({0, 1})[0]};
}

bool has_status(const world::Entity& character, const char* status) {
    return std::any_of(character.statuses.begin(), character.statuses.end(), [&](const world::ActiveStatus& entry) { return entry.status == status; });
}

bool creature_has(const world::Entity& creature, const char* status) {
    return std::any_of(creature.creature.effects.begin(), creature.creature.effects.end(),
                       [&](const world::CreatureEffect& effect) { return effect.status == status; });
}

/** SB1: the spell data of format 2 and the ability shots of the projectile data. */
void data_format2(const Options& options) {
    gamedata::SpellData data;
    check(data.set(kind_tables()) && data.loaded(), "the data of the three kinds is valid");
    const gamedata::SpellSlice* pylon = data.slice_of("icemagic_spell_pylon");
    const gamedata::SpellSlice* trap = data.slice_of(kTrap);
    const gamedata::SpellSlice* beacon = data.slice_of(kBeacon);
    check(pylon != nullptr && trap != nullptr && beacon != nullptr && pylon->autoCast && pylon->vitals && trap->positionSlot == kTrapPlaceSlot,
          "the lookups by the spell row (the case of the letters is ignored), with the values of format 2");
    if (pylon == nullptr || trap == nullptr || beacon == nullptr) {
        return;
    }
    check(!pylon->end_delay() && near(*pylon->destroy_after_trigger("destroytrigger"), 1.3F) && near(*trap->destroy_after_trigger("Trigger"), 0.508F)
              && !trap->destroy_after_trigger("Open") && !beacon->end_delay(),
          "the end chain: the event of the trigger owner (the Pylon: 1.3 s) or its timeline (the trap: 0.508 s); an event that starts at a "
          "trigger and an activation are not the end of the life");
    check(pylon->volume() != nullptr && pylon->volume()->radius == 20.0F && pylon->volume("frostvolume") != nullptr
              && pylon->volume("frostvolume")->radius == 1.0F && trap->offense() != nullptr && trap->offense()->row == "Musket_Trap"
              && beacon->offense() == nullptr,
          "the largest detection volume or one by its entity, and the first offense");
    const gamedata::SpellVitalsRow* row = data.vitals_row("testpylonvitals");
    check(row != nullptr && row->ownerHealthPart == kPylonOwnerPart && row->physicalArmor == 496.0F && data.vitals_row("nothing") == nullptr,
          "a row of the base vitals tables by its id");
    const auto refused = [](const char* what, const std::function<void(gamedata::SpellTables&)>& change) {
        gamedata::SpellTables tables = kind_tables();
        change(tables);
        gamedata::SpellData wrong;
        const bool set = wrong.set(tables);
        if (set) {
            note(std::string("accepted: ") + what);
        }
        return !set && !wrong.loaded() && !wrong.error().empty();
    };
    bool all = true;
    all = refused("a vitals component whose slot is not a Vitals slot", [](auto& t) { t.slices[0].vitals->slot = 2; }) && all;
    all = refused("a position slot that is not of the type 13", [](auto& t) { t.slices[1].positionSlot = 1; }) && all;
    all = refused("no Spell slot and no position slot", [](auto& t) { t.slices[1].positionSlot = 0; }) && all;
    all = refused("an AutoSpell time that is negative", [](auto& t) { t.slices[0].autoCast->cooldown = -1.0F; }) && all;
    all = refused("an AutoSpell with no spell", [](auto& t) { t.slices[0].autoCast->spell.clear(); }) && all;
    all = refused("a volume radius that is not finite", [](auto& t) { t.slices[1].volumes[0].radius = std::nanf(""); }) && all;
    all = refused("an offense radius that is negative", [](auto& t) { t.slices[1].offenses[0].radius = -1.0F; }) && all;
    all = refused("a timeline destroy time that is not a number", [](auto& t) { t.slices[1].timelines[1].destroyAfter = std::nanf(""); }) && all;
    check(all, "the data refuses a vitals slot, a position slot, an AutoSpell, a volume, an offense, or a timeline that is not correct");
    // A file of format 2 (the form of tools/gamedata/spells.py), and a file of format 1 (stage 3.3b-S2a) that still loads.
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "spell-kinds.json";
    const json slice{{"path", "slices/test/trap.dynamicslice"},
                     {"asset", "62626262-6262-6262-6262-626262626201"},
                     {"sub", 2},
                     {"root", "0000030000000062"},
                     {"interestRadius", 125.0},
                     {"spellSlot", 0},
                     {"positionSlot", 4},
                     {"slots", json::array({json::array({1, "Vitals", 15}), json::array({4, "PositionInTheWorld", 13}),
                                            json::array({11, "DelayedEvent", 3139})})},
                     {"events", json::array({json::array({11, 30.0, 0, "Trigger", "", false, "Lifetime", true, false})})},
                     {"components", json::array()},
                     {"vitals", json::array({1, "TestTrapVitals", "Trigger"})},
                     {"autoSpell", nullptr},
                     {"volumes", json::array({json::array({"DetectionVolume", 0.5, 0.1})})},
                     {"offenses", json::array({json::array({"TrapActivate", "Musket_Trap", 1.1})})},
                     {"detectionEvents", json::array({json::array({"Trigger", 0})})},
                     {"timelines", json::array({json::array({"Trigger", 0.508})})}};
    const json document{{"format", 2},
                        {"counts", {{"slices", 1}, {"spells", 1}}},
                        {"slices", {slice}},
                        {"spells", json::array({json::array({kTrap, 0})})},
                        {"vitals", json::array({json::array({"TestTrapVitals", 17.0, 0.0, 136.0, 136.0, 100.0})})}};
    const auto load = [&path](const json& content, gamedata::SpellData& out) {
        std::ofstream(path, std::ios::binary | std::ios::trunc) << content.dump();
        return out.load(path.string());
    };
    gamedata::SpellData file;
    if (!load(document, file)) {
        note("the file of format 2: " + file.error());
    }
    check(file.loaded() && file.slice_of(kTrap) != nullptr && file.slice_of(kTrap)->positionSlot == 4
              && file.slice_of(kTrap)->events[0].owner == "Lifetime" && !file.slice_of(kTrap)->events[0].onTrigger
              && file.vitals_row("TestTrapVitals") != nullptr && file.vitals_row("TestTrapVitals")->healthBaseMax == 17.0F,
          "a file of format 2 loads with the values of the slice and the vitals rows");
    json noVitals = document;
    noVitals.erase("vitals");
    json shortEvent = document;
    shortEvent["slices"][0]["events"][0] = json::array({11, 30.0, 0, "Trigger", "", false});
    gamedata::SpellData other;
    check(!load(noVitals, other) && !load(shortEvent, other), "a file of format 2 with no list vitals, or an event with 6 values, is refused");
    json old = document;
    old["format"] = 1;
    old["slices"][0]["spellSlot"] = 2;
    old["slices"][0]["slots"] = json::array({json::array({2, "Spell", 2912}), json::array({11, "DelayedEvent", 3139})});
    old["slices"][0]["events"][0] = json::array({11, 30.0, 0, "Trigger", "", true});
    gamedata::SpellData oldData;
    if (!load(old, oldData)) {
        note("the file of format 1: " + oldData.error());
    }
    check(oldData.loaded() && oldData.slice_of(kTrap)->events[0].onInit && !oldData.slice_of(kTrap)->autoCast
              && oldData.slice_of(kTrap)->volumes.empty(),
          "a file of format 1 still loads: each event starts at the init, and the slice has no values of format 2");
    // The ability shots of the projectile data (the 11th value): only the abilities system launches them.
    gamedata::ProjectileData shots;
    check(shots.set(kind_projectiles()), "the projectile data with the ability shots is valid");
    const gamedata::ShotRule* toss = shots.ability_shot("musket_trap_toss");
    check(toss != nullptr && toss->ability && toss->attack == "Traps" && shots.shots("TestMusketProfile", crc("traps")).empty()
              && shots.ability_shot("Test_Fire_Projectile") == nullptr,
          "an ability shot by its spell (the case of the letters is ignored); the shots of a basic attack do not have it");
    gamedata::ProjectileTables wrong = kind_projectiles();
    wrong.shots.back().slice = -1;
    gamedata::ProjectileTables noSpell = kind_projectiles();
    noSpell.shots.back().spell.clear();
    gamedata::ProjectileData refusedShots;
    check(!refusedShots.set(wrong) && !refusedShots.set(noSpell), "an ability shot with no slice of its own or with no spell is refused");
}

/** SB2: the Pylon: the spawn, the payment, the held cooldown, the health, the record. */
void pylon_spawn(const Options&) {
    KindShop shop;
    world::Entity& own = *shop.character();
    shop.learn(own, "TestGauntlet", kIceMagic, kPylonNode);
    const float mana = shop.character()->vitals.mana;
    const float healthMax = shop.character()->vitals.healthMax;
    const std::size_t from = shop.a.got.size();
    shop.walker.press(0);
    check(shop.presses(world::CastVerdict::accepted) == 1, "the press of Q with the Pylon on it is accepted");
    shop.walker.idle(12);
    check(shop.spells().empty() && near(shop.character()->vitals.mana, mana, 0.01F),
          "0.4 s after the press: no Pylon yet, and no mana is paid at the press");
    const world::AbilityCooldown* held = shop.cooldown(kPylonNode);
    check(held != nullptr && held->indefinite, "the cooldown holds from the press (indefinite)");
    world::Entity* pylon = shop.wait_spell(15);
    shop.walker.idle(1);
    check(pylon != nullptr && pylon->spell.role == world::SpellRole::summon && pylon->spell.spellId == kPylon,
          "the Pylon is in the world at the time of the cast fragment (0.6 s)");
    if (pylon == nullptr) {
        return;
    }
    note("the Pylon at " + std::to_string(pylon->position.x) + " " + std::to_string(pylon->position.y) + ", health "
         + std::to_string(pylon->vitals.health) + ", mana of A " + std::to_string(shop.character()->vitals.mana));
    check(near(pylon->position.x, kPlace.x - 2.0F, 0.05F) && near(pylon->position.y, kPlace.y, 0.05F) && near(pylon->position.z, kHeight, 0.05F),
          "its place: CharRelPosOffsetY (2 m) in front of the caster, on the ground");
    check(near(shop.character()->vitals.mana, mana - kPylonCost, 0.5F), "the spawn pays the cost of IceMagic_IcePylon (15) through the one mana path");
    check(near(pylon->vitals.healthMax, kPylonOwnerPart * healthMax) && near(pylon->vitals.health, pylon->vitals.healthMax)
              && pylon->spell.vitalsRow != nullptr && pylon->spell.vitalsRow->physicalArmor == 496.0F,
          "its health: the part of the vitals row (0.2) of the maximum health of the caster; the armor of the row");
    check(pylon->spell.ends - pylon->spell.started == milliseconds(16300), "its life: 15 s of the AutoSpell, then the death chain of 1.3 s");
    const std::vector<Fragment> spellStates = fragments_of(shop.a, kPylonSpellSlot, server::rep::kTypeSpellState, from);
    const std::vector<float> healths = spell_healths(shop.a, from);
    check(spellStates.size() == 1 && hex(spellStates[0].fields.at({0, 6})).substr(32) == "00000000ffffffff",
          "the record: one Spell state (slot 4 of the slice), with no attachment");
    check(!healths.empty() && near(healths.back(), pylon->vitals.health), "the record has the Vitals state (slot 1): the health of the server");
    const auto [initSeen, initDone] = delayed_mark(shop.a, 9, from);
    const auto [chainSeen, chainDone] = delayed_mark(shop.a, 10, from);
    check(initSeen && initDone == 0 && !chainSeen,
          "the delayed event of the init is in the record; the events of the death chain (they start at a trigger) are not");
    held = shop.cooldown(kPylonNode);
    check(held != nullptr && held->indefinite, "the cooldown holds while the Pylon lives");
    const std::vector<Fragment> cooldowns = fragments_of(shop.a, server::rep::kCooldownSlot, server::rep::kTypeCooldownState, from);
    const std::string id_hex = [] {
        const std::uint32_t id = gamedata::progression_id(kPylonNode);
        Bytes bytes{static_cast<std::uint8_t>(id >> 24), static_cast<std::uint8_t>(id >> 16), static_cast<std::uint8_t>(id >> 8),
                    static_cast<std::uint8_t>(id)};
        return hex(bytes);
    }();
    check(!cooldowns.empty() && hex(cooldowns.back().fields.at({0, 0})).find(id_hex + "ffffffffffffffff0000000000000000") != std::string::npos,
          "the owner gets the cooldown of the Pylon with the end all ones and the minimum end 0 (an indefinite cooldown)");
    shop.walker.press(0);
    check(shop.presses(world::CastVerdict::cooldown) == 1 && shop.spells().size() == 1, "a second press while the Pylon lives is refused (cooldown)");
    // The development view of the cooldowns names the held cooldown also after the time of the data (10 s) passed.
    while (shop.host.clock.now() - pylon->spell.started < milliseconds(11000)) {
        shop.walker.idle(1);
    }
    const json view = server::http::commands::abilities_json(shop.host.world, kCharacterA);
    const bool listed = std::any_of(view["cooldowns"].begin(), view["cooldowns"].end(), [](const json& entry) {
        return entry["ability"] == kPylonNode && entry["held"].get<bool>();
    });
    check(listed, "the cooldown view (tools/world cooldowns) names the held cooldown while the Pylon lives");
}

/** SB3: the shots of the Pylon, the threat, the damage of a creature, the death chain, the cooldown from the end. */
void pylon_fight(const Options&) {
    KindShop shop;
    shop.host.world.set_creature_behavior(true);
    shop.host.world.creatures().behavior().set_ignored(kCharacterA, true);
    world::Entity& own = *shop.character();
    shop.learn(own, "TestGauntlet", kIceMagic, kPylonNode);
    world::Entity* wolf = shop.wolf_east(-9.0F);
    const world::EntityId wolfId = wolf->id;
    const float full = wolf->vitals.health;
    world::Entity* pylon = shop.cast(30);
    check(pylon != nullptr, "setup: the Pylon");
    if (pylon == nullptr) {
        return;
    }
    const world::EntityId id = pylon->id;
    const auto started = pylon->spell.started;
    while (shop.host.clock.now() - started < milliseconds(950)) {
        shop.walker.idle(1);
    }
    check(shop.spell_counts().shots == 0, "no shot before the initial delay of the AutoSpell (1 s)");
    shop.walker.idle(4);
    check(shop.spell_counts().shots == 1, "the first shot at 1 s");
    shop.walker.idle(15);
    world::Entity* hurt = shop.creature(wolfId);
    check(hurt != nullptr && hurt->vitals.health < full, "the shot flies through the projectile path and hurts the wolf (the row IceMagic_Pylon)");
    check(hurt != nullptr && hurt->creature.mind.target == id, "the wolf takes the Pylon as its target (OverrideTargetForAiThreat)");
    while (shop.host.clock.now() - started < milliseconds(3100)) {
        shop.walker.idle(1);
    }
    check(shop.spell_counts().shots == 2, "the second shot 2 s later (m_cooldown of the AutoSpell)");
    // The wolf walks to the Pylon and bites it.
    for (int step = 0; step < 150 && shop.spell_counts().summonHits == 0; ++step) {
        shop.walker.idle(1);
    }
    pylon = shop.host.world.find(id);
    check(pylon != nullptr && shop.spell_counts().summonHits >= 1 && pylon->vitals.health < pylon->vitals.healthMax,
          "the bite of the wolf hurts the Pylon (its armor from the vitals row)");
    if (pylon == nullptr) {
        return;
    }
    check(near(shop.character()->vitals.health, shop.character()->vitals.healthMax), "the caster takes no damage (the wolf fights the Pylon)");
    const std::size_t from = shop.a.got.size();
    check(shop.host.world.spells().hurt(id, pylon->vitals.health) && pylon->spell.ending, "the health 0 starts the death chain");
    shop.walker.idle(2);
    const auto [destroySeen, destroyDone] = delayed_mark(shop.a, 10, from);
    const auto [initSeen, initDone] = delayed_mark(shop.a, 9, from);
    check(destroySeen && destroyDone == 0 && initSeen && initDone == 1,
          "each observer gets the death chain: the event of DestroyTrigger starts, and the trigger event of the init is completed");
    check(shop.host.world.find(id) != nullptr && shop.host.world.find(id)->spell.ending, "the Pylon stays for its death chain");
    const std::uint64_t shots = shop.spell_counts().shots;
    shop.pass(1.4F);
    check(shop.host.world.find(id) == nullptr && shop.ended(world::SpellEnd::destroyed) == 1 && shop.spell_counts().shots == shots,
          "the removal 1.3 s after the trigger (the event Destroy); no shot in the death chain");
    world::Entity* after = shop.creature(wolfId);
    check(after != nullptr && after->creature.mind.target != id, "the wolf loses the Pylon as its target");
    const world::AbilityCooldown* timer = shop.cooldown(kPylonNode);
    check(timer != nullptr && !timer->indefinite && near(std::chrono::duration<float>(timer->end - timer->start).count(), kPylonCooldown),
          "the cooldown of the data starts at the end of the Pylon");
}

/** SB4: the life of the Pylon, the cancel at the leave of the caster, the mana rules of the payment. */
void pylon_life(const Options&) {
    {
        KindShop shop;
        shop.learn(*shop.character(), "TestGauntlet", kIceMagic, kPylonNode);
        world::Entity* pylon = shop.cast(30);
        check(pylon != nullptr, "setup: the Pylon");
        if (pylon == nullptr) {
            return;
        }
        const world::EntityId id = pylon->id;
        const auto started = pylon->spell.started;
        while (shop.host.clock.now() - started < milliseconds(14900)) {
            shop.walker.idle(1);
        }
        check(shop.host.world.find(id) != nullptr && !shop.host.world.find(id)->spell.ending && shop.spell_counts().shots == 0,
              "the Pylon lives for 15 s; with no creature in its volume it does not shoot");
        shop.walker.idle(5);
        check(shop.host.world.find(id) != nullptr && shop.host.world.find(id)->spell.ending, "at 15 s its expire event starts the death chain");
        shop.pass(1.4F);
        check(shop.host.world.find(id) == nullptr && shop.ended(world::SpellEnd::time) == 1, "the removal 1.3 s later (its end on the world clock)");
        const world::AbilityCooldown* timer = shop.cooldown(kPylonNode);
        check(timer != nullptr && !timer->indefinite, "the cooldown of the data starts at the end");
    }
    {
        KindShop shop;
        shop.enter_b_near(at(kPlace.x - 3.0F, kPlace.y + 3.0F));
        shop.learn(*shop.character(), "TestGauntlet", kIceMagic, kPylonNode);
        world::Entity* pylon = shop.cast(30);
        check(pylon != nullptr, "setup: the Pylon");
        if (pylon == nullptr) {
            return;
        }
        const world::EntityId id = pylon->id;
        check(shop.host.world.leave(shop.character()->owner), "setup: the caster leaves the world");
        shop.b_steps(3, at(kPlace.x - 3.0F, kPlace.y + 3.0F));
        check(shop.host.world.find(id) == nullptr && shop.ended(world::SpellEnd::casterLeft) == 1,
              "the leave of the caster removes its Pylon at once (no orphan)");
    }
    {
        KindShop shop;
        shop.learn(*shop.character(), "TestGauntlet", kIceMagic, kPylonNode);
        shop.host.world.abilities().change_mana(*shop.character(), -(shop.character()->vitals.mana - kPylonCost + 1.0F), "a test");
        shop.walker.press(0);
        shop.walker.idle(30);
        check(shop.presses(world::CastVerdict::noMana) == 1 && shop.spells().empty() && shop.cooldown(kPylonNode) == nullptr,
              "a press with less mana than the payment of the spawn makes no cast (the refusal before the payment)");
    }
    {
        KindShop shop;
        shop.learn(*shop.character(), "TestGauntlet", kIceMagic, kPylonNode);
        shop.walker.press(0);
        check(shop.presses(world::CastVerdict::accepted) == 1, "setup: the press");
        shop.host.world.abilities().change_mana(*shop.character(), -shop.character()->vitals.mana, "a test");
        const float before = shop.character()->vitals.mana;
        shop.walker.idle(25);
        const world::AbilityCooldown* timer = shop.cooldown(kPylonNode);
        check(shop.spells().empty() && shop.character()->vitals.mana >= before
                  && (timer == nullptr || (!timer->indefinite && timer->end <= shop.host.clock.now())),
              "less mana at the spawn: no Pylon, no payment, and no countdown (research: a failed placement starts no fixed countdown)");
    }
}

/** SB5: the trap: the toss, the arm time, one trigger, the removal after the timeline; the end of its life with no trigger. */
void trap(const Options& options) {
    {
        KindShop shop;
        shop.flat_ground(options.workDirectory / "spell-kinds-ground");
        shop.learn(*shop.character(), "TestMusket", kMusket, kTrapsNode);
        world::Entity* first = shop.wolf_east(0.0F, 30.0F);
        world::Entity* second = shop.wolf_east(0.0F, 34.0F);
        const world::EntityId firstId = first->id;
        const world::EntityId secondId = second->id;
        const float full = first->vitals.health;
        const std::size_t from = shop.a.got.size();
        world::Entity* trap = shop.cast(60);
        check(trap != nullptr && trap->spell.role == world::SpellRole::trap && trap->spell.spellId == kTrap,
              "the server throws the toss (no shot item came), and its landing makes the trap");
        if (trap == nullptr) {
            return;
        }
        const world::EntityId id = trap->id;
        const state::WorldPosition place = trap->position;
        note("the trap at " + std::to_string(place.x - kPlace.x) + " m east of A, height " + std::to_string(place.z));
        check(place.x < kPlace.x - 3.0F && near(place.z, kHeight, 0.05F) && shop.ends(world::ProjectileEnd::wall) == 1,
              "the toss falls in front of the caster and stops on the ground; the trap is on the ground");
        check(near(trap->vitals.health, kTrapHealth) && trap->spell.ends - trap->spell.started == milliseconds(30508),
              "its health of the vitals row; its life: the trigger of its Lifetime entity at 30 s and the timeline of 0.508 s");
        shop.walker.idle(2);
        note("the record of the trap: " + std::to_string(fragments_of(shop.a, kTrapPlaceSlot, server::rep::kTypePositionInTheWorldState, from).size())
             + " PositionInTheWorld, " + std::to_string(fragments_of(shop.a, kTrapPlaceSlot, server::rep::kTypeSpellState, from).size()) + " Spell, "
             + std::to_string(spell_healths(shop.a, from).size()) + " Vitals fragments");
        check(!fragments_of(shop.a, kTrapPlaceSlot, server::rep::kTypePositionInTheWorldState, from).empty()
                  && fragments_of(shop.a, kTrapPlaceSlot, server::rep::kTypeSpellState, from).empty() && !spell_healths(shop.a, from).empty(),
              "the record: the PositionInTheWorld state (slot 4) and the Vitals state; no Spell state");
        // The two wolves walk onto the trap (the behavior is off: the test moves them).
        shop.host.world.place_creature(*shop.creature(firstId), place);
        shop.host.world.place_creature(*shop.creature(secondId), state::WorldPosition{place.x + 0.6F, place.y, place.z});
        const std::size_t before = shop.a.got.size();
        shop.walker.idle(5);
        check(shop.spell_counts().triggers == 1 && shop.spell_counts().trapHits == 2 && shop.creature(firstId)->vitals.health < full
                  && shop.creature(secondId)->vitals.health < full,
              "one trigger: each wolf in the sphere of the offense (1.1 m) takes one hit (the row Musket_Trap)");
        check(creature_has(*shop.creature(firstId), kTrapRoot) && creature_has(*shop.creature(secondId), kTrapRoot), "each one gets the root");
        const auto [triggerSeen, triggerDone] = delayed_mark(shop.a, 11, before);
        check(triggerSeen && triggerDone == 1, "the observer gets the trigger of the entity Trigger (the event completed: the activation)");
        const float once = full - shop.creature(firstId)->vitals.health;
        shop.walker.idle(5);
        check(shop.spell_counts().triggers == 1 && near(full - shop.creature(firstId)->vitals.health, once), "no second trigger, no second hit");
        shop.pass(0.6F);
        check(shop.host.world.find(id) == nullptr && shop.ended(world::SpellEnd::triggered) == 1, "the removal 0.508 s after the trigger");
    }
    {
        KindShop shop;
        shop.flat_ground(options.workDirectory / "spell-kinds-ground-2");
        shop.learn(*shop.character(), "TestMusket", kMusket, kTrapsNode);
        world::Entity* trap = shop.cast(60);
        check(trap != nullptr, "setup: the trap");
        if (trap == nullptr) {
            return;
        }
        const world::EntityId id = trap->id;
        const auto started = trap->spell.started;
        while (shop.host.clock.now() - started < milliseconds(30400)) {
            shop.walker.idle(1);
        }
        check(shop.host.world.find(id) != nullptr && shop.host.world.find(id)->spell.ending && shop.spell_counts().triggers == 0,
              "with no creature: at 30 s its Lifetime entity triggers the end chain (no hit)");
        shop.walker.idle(5);
        check(shop.host.world.find(id) == nullptr && shop.ended(world::SpellEnd::time) == 1, "the removal after the timeline (30.508 s)");
    }
}

/** SB6: the Beacon: the hit, the attachment, the area that follows, the statuses, the pulses, the ends, the spell of the nodes. */
void beacon(const Options&) {
    {
        KindShop shop;
        shop.learn(*shop.character(), "TestLifeStaff", kLifeMagic, kBeaconNode);
        world::Entity* wolf = shop.wolf_east(-2.5F);
        const world::EntityId wolfId = wolf->id;
        const float full = wolf->vitals.health;
        const float mana = shop.character()->vitals.mana;
        const std::size_t from = shop.a.got.size();
        shop.walker.press(0);
        shop.walker.idle(3);
        check(near(shop.character()->vitals.mana, mana - kBeaconCost, 0.5F), "the press pays the cost of the projectile spell (16)");
        // The client sends the shot item of the Heavy attack (the shot of the Beacon) at the wolf.
        shop.send_shot("Heavy", {-2.2F, 0.0F, -1.0F});
        for (int step = 0; step < 10 && shop.spells().empty(); ++step) {
            shop.walker.idle(1);
        }
        world::Entity* aura = shop.spell();
        check(aura != nullptr && aura->spell.role == world::SpellRole::aura && aura->spell.attached == wolfId && aura->spell.spellId == kBeacon,
              "the projectile hits the wolf and makes the area of its SecondarySpellID on it (attached)");
        if (aura == nullptr) {
            return;
        }
        const world::EntityId id = aura->id;
        check(shop.creature(wolfId)->vitals.health < full, "the hit hurts the wolf (the row LifeStaff_Beacon)");
        check(aura->spell.ends - aura->spell.started == milliseconds(10000), "its life: the Duration of the spell row (10 s)");
        shop.walker.idle(2);
        const std::vector<Fragment> states = fragments_of(shop.a, kBeaconSpellSlot, server::rep::kTypeSpellState, from);
        const world::Entity& target = *shop.creature(wolfId);
        check(!states.empty() && hex(states.back().fields.at({0, 6})).substr(0, 32) == hex(Bytes(target.actor.bytes.begin(), target.actor.bytes.end())),
              "the Spell state names the wolf as the attachment (field 6): the client moves the area with it");
        check(has_status(*shop.character(), kBeaconStatus) && has_status(*shop.character(), kBeaconPotency)
                  && shop.host.world.spells().members(id).size() == 1,
              "the caster in the radius (3 m) is a friend: it gets the statuses of the spell row");
        const auto pulses = [&] { return shop.host.world.statuses().counts().pulsesWithNoHeal; };
        const std::uint64_t first = pulses();
        const float health = shop.character()->vitals.health;
        shop.pass(2.0F);
        note("pulses of the Beacon status: " + std::to_string(first) + " at the start, " + std::to_string(pulses()) + " after 2 s");
        check(first >= 1 && pulses() - first == 2 && near(shop.character()->vitals.health, health),
              "a pulse at the TickRate of the status (1 s) with no heal (the heal base is stage 3.3b-S6)");
        // The wolf runs 10 m away: the area follows it, and the caster is out of it.
        shop.host.world.place_creature(*shop.creature(wolfId), at(kPlace.x - 12.0F, kPlace.y));
        shop.walker.idle(10);
        check(near(shop.host.world.find(id)->position.x, kPlace.x - 12.0F, 0.01F), "the area follows the wolf");
        check(!has_status(*shop.character(), kBeaconStatus) && shop.spell_counts().auraTaken == 1, "the caster out of the area loses the statuses (ClearOnExit)");
        shop.host.world.place_creature(*shop.creature(wolfId), at(kPlace.x - 2.0F, kPlace.y));
        shop.walker.idle(10);
        check(has_status(*shop.character(), kBeaconStatus) && shop.spell_counts().auraGiven == 2, "back in the area: the statuses again");
        shop.pass(8.0F);
        check(shop.host.world.find(id) == nullptr && shop.ended(world::SpellEnd::time) == 1 && !has_status(*shop.character(), kBeaconStatus)
                  && !has_status(*shop.character(), kBeaconPotency),
              "the end at 10 s takes the statuses back (ClearOnEnd)");
    }
    for (const bool caster : {false, true}) {
        KindShop shop;
        shop.enter_b_near(at(kPlace.x + 3.0F, kPlace.y + 3.0F));
        shop.learn(*shop.character(), "TestLifeStaff", kLifeMagic, kBeaconNode);
        world::Entity* wolf = shop.wolf_east(-2.5F);
        const world::EntityId wolfId = wolf->id;
        shop.walker.press(0);
        shop.walker.idle(3);
        shop.send_shot("Heavy", {-2.2F, 0.0F, -1.0F});
        shop.walker.idle(5);
        world::Entity* aura = shop.spell();
        check(aura != nullptr && aura->spell.attached == wolfId, "setup: the Beacon on the wolf");
        if (aura == nullptr) {
            return;
        }
        const world::EntityId id = aura->id;
        if (caster) {
            check(shop.host.world.leave(shop.character()->owner), "setup: the caster leaves");
            shop.b_steps(2, at(kPlace.x + 3.0F, kPlace.y + 3.0F));
            check(shop.host.world.find(id) == nullptr && shop.ended(world::SpellEnd::casterLeft) == 1, "the leave of the caster ends the Beacon");
        } else {
            check(shop.host.world.creatures().remove(wolfId).error == world::CreatureError::none, "setup: the wolf leaves the world");
            shop.walker.idle(2);
            check(shop.host.world.find(id) == nullptr && shop.ended(world::SpellEnd::targetLeft) == 1 && !has_status(*shop.character(), kBeaconStatus),
                  "the leave of its target ends the Beacon (no orphan), and the statuses go away");
        }
    }
    {
        // The spells of the nodes (a decision of an agent from the spell rows): the chain Infused Light, Radiance's Blessing, Speed of
        // Light selects _Upgrade, _Upgrade2, _Upgrade3; a node with no node before it in the chain does not count.
        world::SkillSet skills;
        const std::uint32_t tree = gamedata::progression_id(kLifeMagic);
        const std::uint32_t node = gamedata::progression_id(kBeaconNode);
        skills.pools[tree][node].level = 1;
        const auto projectile_of = [&]() {
            const std::optional<world::CastPlan> plan = world::Abilities::plan(node, skills);
            return plan && plan->spell ? plan->spell->projectile : std::string();
        };
        const std::string base = projectile_of();
        skills.pools[tree][gamedata::progression_id("Upgrade_Lifestaff_Protection_AddHaste")].level = 1;
        const std::string haste = projectile_of();
        skills.pools[tree][gamedata::progression_id("Upgrade_Lifestaff_Protection_AoeInc")].level = 1;
        const std::string wider = projectile_of();
        skills.pools[tree][gamedata::progression_id("Upgrade_Lifestaff_Protection_DurOnLow")].level = 1;
        const std::string all = projectile_of();
        check(base == "Spell_Ability_Lifestaff_Beacon_Projectile" && haste == base && wider == "Spell_Ability_Lifestaff_Beacon_Projectile_Upgrade"
                  && all == "Spell_Ability_Lifestaff_Beacon_Projectile_Upgrade3",
              "the projectile spell of the nodes: the base, _Upgrade with Infused Light, _Upgrade3 with the full chain");
    }
}

/** SB7 (MP-10): two sessions observe one Pylon; the cooldown is private; the Beacon gives its statuses only to a friend. */
void two_sessions(const Options&) {
    {
        KindShop shop(true);
        const state::WorldPosition placeB = at(kPlace.x - 1.0F, kPlace.y + 4.0F);
        shop.enter_b_near(placeB);
        shop.learn(*shop.character(), "TestGauntlet", kIceMagic, kPylonNode);
        const std::size_t fromA = shop.a.got.size();
        const std::size_t fromB = shop.b.got.size();
        world::Entity* pylon = shop.cast(30);
        check(pylon != nullptr, "setup: the Pylon");
        if (pylon == nullptr) {
            return;
        }
        const world::EntityId id = pylon->id;
        shop.b_steps(3, placeB);
        const std::vector<Fragment> stateA = fragments_of(shop.a, kPylonSpellSlot, server::rep::kTypeSpellState, fromA);
        const std::vector<Fragment> stateB = fragments_of(shop.b, kPylonSpellSlot, server::rep::kTypeSpellState, fromB);
        check(stateA.size() == 1 && stateB.size() == 1 && stateA[0].fields == stateB[0].fields,
              "the two sessions get the same Spell state of the Pylon (one encoder, the observer view)");
        const std::vector<float> healthA = spell_healths(shop.a, fromA);
        const std::vector<float> healthB = spell_healths(shop.b, fromB);
        check(!healthA.empty() && !healthB.empty() && healthA.back() == healthB.back(), "and the same Vitals state (its health)");
        const std::size_t laterA = shop.a.got.size();
        const std::size_t laterB = shop.b.got.size();
        check(shop.host.world.spells().hurt(id, 10.0F), "setup: the Pylon takes 10 damage");
        shop.b_steps(2, placeB);
        const std::vector<float> newA = spell_healths(shop.a, laterA);
        const std::vector<float> newB = spell_healths(shop.b, laterB);
        check(!newA.empty() && !newB.empty() && near(newA.back(), shop.host.world.find(id)->vitals.health) && newA.back() == newB.back(),
              "a change of its health goes to each observer (one path)");
        check(!fragments_of(shop.a, server::rep::kCooldownSlot, server::rep::kTypeCooldownState, fromA).empty()
                  && fragments_of(shop.b, server::rep::kCooldownSlot, server::rep::kTypeCooldownState, fromB).empty(),
              "the held cooldown goes only to the session of the caster (B observes A and gets no cooldown state)");
        const auto* timersB = shop.host.world.abilities().cooldowns_of(kCharacterB);
        check(timersB == nullptr || timersB->empty(), "B has no cooldown of the Pylon of A");
    }
    for (const bool friends : {false, true}) {
        KindShop shop(true);
        const state::WorldPosition placeB = at(kPlace.x - 2.5F, kPlace.y + 1.5F);
        shop.enter_b_near(placeB);
        if (friends) {
            shop.host.world.set_test_friends(kCharacterA, kCharacterB);
        }
        shop.learn(*shop.character(), "TestLifeStaff", kLifeMagic, kBeaconNode);
        shop.wolf_east(-2.5F);
        shop.walker.press(0);
        shop.walker.idle(3);
        shop.send_shot("Heavy", {-2.2F, 0.0F, -1.0F});
        shop.walker.idle(5);
        shop.b_steps(3, placeB);
        world::Entity* aura = shop.spell();
        check(aura != nullptr, "setup: the Beacon on the wolf, A and B in its radius");
        if (aura == nullptr) {
            return;
        }
        const world::Entity* other = shop.host.world.find_character(kCharacterB);
        if (friends) {
            check(other != nullptr && has_status(*other, kBeaconStatus) && shop.host.world.spells().members(aura->id).size() == 2,
                  "with the fixture pair of friends B gets the statuses too");
        } else {
            check(other != nullptr && !has_status(*other, kBeaconStatus) && shop.host.world.spells().members(aura->id).size() == 1,
                  "B is not in the group of one of A: no status for B (the friendly relation is an open decision for the user)");
        }
    }
}

/** SB8: the load of 3,000 far spells of the three kinds, with and without a far crowd of creatures. */
void load(const Options& options) {
    std::array<std::uint64_t, 2> looked{};
    std::array<double, 2> seconds{};
    std::array<std::size_t, 2> observed{};
    for (int crowd = 0; crowd < 2; ++crowd) {
        KindShop shop;
        const auto beta = add_player(shop.host.store, "76561202255233019", "Tester B", kCharacterB, "Beta", at(kPlace.x + 4000.0F, kPlace.y));
        check(shop.host.world.spawn_character(kCharacterB, at(kPlace.x + 4000.0F, kPlace.y)) == world::SpawnError::none, "setup: a far caster");
        const world::EntityId far = shop.host.world.find_character(kCharacterB)->id;
        const world::EntityId own = shop.character()->id;
        if (crowd == 1) {
            // 4,000 creatures 8 km away: no spell is near them.
            for (int index = 0; index < 4000; ++index) {
                (void)shop.host.world.creatures().spawn("Test_Wolf", at(kPlace.x + 8000.0F + static_cast<float>(index % 80) * 5.0F,
                                                                        kPlace.y + static_cast<float>(index / 80) * 5.0F),
                                                        90.0F);
            }
        }
        const auto request = [](world::EntityId caster, world::SpellRole role, const state::WorldPosition& place) {
            world::SpellRequest spell;
            spell.spellId = role == world::SpellRole::summon ? kPylon : role == world::SpellRole::trap ? kTrap : kBeacon;
            spell.caster = caster;
            spell.role = role;
            spell.position = place;
            if (role == world::SpellRole::aura) {
                spell.radius = 3.0F;
                spell.life = milliseconds(10000);
                spell.statuses = {kBeaconStatus};
            }
            return spell;
        };
        for (int index = 0; index < 1000; ++index) {
            const float x = kPlace.x + 2000.0F + static_cast<float>(index % 40) * 60.0F;
            const float y = kPlace.y + static_cast<float>(index / 40) * 60.0F;
            (void)shop.host.world.spells().make(request(far, world::SpellRole::summon, at(x, y)));
            (void)shop.host.world.spells().make(request(far, world::SpellRole::trap, at(x + 20.0F, y)));
            (void)shop.host.world.spells().make(request(far, world::SpellRole::aura, at(x + 40.0F, y)));
        }
        (void)shop.host.world.spells().make(request(own, world::SpellRole::summon, at(kPlace.x - 2.0F, kPlace.y)));
        (void)shop.host.world.spells().make(request(own, world::SpellRole::trap, at(kPlace.x - 4.0F, kPlace.y)));
        (void)shop.host.world.spells().make(request(own, world::SpellRole::aura, at(kPlace.x - 1.0F, kPlace.y)));
        const auto start = std::chrono::steady_clock::now();
        shop.pass(2.0F);
        seconds[crowd] = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        looked[crowd] = shop.spell_counts().looked;
        for (const auto& [id, known] : shop.host.server->session(1)->observer().known()) {
            const world::Entity* entity = shop.host.world.find(id);
            observed[crowd] += entity != nullptr && entity->kind == world::EntityKind::spell ? 1 : 0;
        }
        note(std::string(crowd == 1 ? "with" : "without") + " the far crowd: " + std::to_string(shop.host.world.spells().count())
             + " spells, 60 ticks of the world in " + std::to_string(seconds[crowd]) + " s, " + std::to_string(looked[crowd])
             + " entities looked at, " + std::to_string(shop.spell_counts().dueTaken) + " due entries taken, " + std::to_string(observed[crowd])
             + " spells in the interest table");
        (void)beta;
        (void)options;
    }
    check(observed[0] == 3 && observed[1] == 3, "the session observes only the 3 spells near its character");
    check(seconds[0] < 2.0 && seconds[1] < 2.0, "60 ticks of the world with 3,003 spells in less than 2 s");
    check(looked[0] == looked[1], "the far crowd of 4,000 creatures adds no look (the cost depends on what is near the spells)");
}

/** SB9: the routing of a shot item of an ability. */
void shot_routing(const Options& options) {
    KindShop shop;
    shop.flat_ground(options.workDirectory / "spell-kinds-routing");
    shop.learn(*shop.character(), "TestMusket", kMusket, kTrapsNode);
    // With no cast, a shot item of the attack Traps is a shot of a basic attack: the data has no basic shot for it.
    const std::uint64_t accepted = shop.verdicts(world::ShotVerdict::accepted);
    shop.send_shot("Traps", {-1.0F, 0.0F, 0.3F});
    shop.walker.idle(5);
    check(shop.verdicts(world::ShotVerdict::accepted) == accepted && shop.host.world.abilities().counts().shots == 0 && shop.spells().empty(),
          "with no cast the item is refused by the rules of a basic shot (an ability shot is not a basic shot): no projectile");
    shop.walker.press(0);
    shop.walker.idle(3);
    shop.send_shot("Traps", {-1.0F, 0.0F, 0.3F});
    check(shop.host.world.abilities().counts().shots == 1 && shop.verdicts(world::ShotVerdict::accepted) == accepted + 1,
          "while the cast waits the item goes to the cast: the server launches the toss of the trap in the direction of the item");
    shop.send_shot("Traps", {-1.0F, 0.0F, 0.3F});
    check(shop.host.world.abilities().counts().shots == 1 && shop.verdicts(world::ShotVerdict::accepted) == accepted + 1,
          "a second item after the throw is not a second toss");
    for (int step = 0; step < 60 && shop.spells().empty(); ++step) {
        shop.walker.idle(1);
    }
    world::Entity* trap = shop.spell();
    check(trap != nullptr && trap->position.x < kPlace.x - 5.0F, "one trap, where the toss of the item lands (farther than the throw of the server)");
}

/** SB10: no spell data, a file of format 1, no projectile data. */
void no_data(const Options&) {
    {
        KindShop shop(false, false);
        shop.learn(*shop.character(), "TestGauntlet", kIceMagic, kPylonNode);
        const float mana = shop.character()->vitals.mana;
        shop.walker.press(0);
        shop.walker.idle(30);
        const world::AbilityCooldown* timer = shop.cooldown(kPylonNode);
        check(shop.presses(world::CastVerdict::accepted) == 1 && shop.spells().empty() && near(shop.character()->vitals.mana, mana, 0.01F)
                  && (timer == nullptr || (!timer->indefinite && timer->end <= shop.host.clock.now())),
              "no spell data: the cast makes no Pylon, pays nothing, and holds no cooldown (no countdown, not held forever)");
    }
    {
        KindShop shop(false, false);
        gamedata::SpellTables tables = kind_tables();
        tables.slices[0].autoCast.reset();
        tables.slices[1].volumes.clear();
        check(shop.host.spells.set(tables), "setup: slices with no values of format 2 (a file of format 1)");
        shop.learn(*shop.character(), "TestGauntlet", kIceMagic, kPylonNode);
        shop.walker.press(0);
        shop.walker.idle(30);
        check(shop.spells().empty(), "a Pylon slice with no AutoSpell makes no entity (it would end at once)");
    }
    {
        KindShop shop(false, true, false);
        shop.learn(*shop.character(), "TestMusket", kMusket, kTrapsNode);
        shop.walker.press(0);
        check(shop.presses(world::CastVerdict::noRule) == 1 && shop.cooldown(kTrapsNode) == nullptr,
              "no projectile data: the press of the trap is refused (no rule) and changes nothing");
    }
    {
        KindShop shop(false, true, false);
        shop.learn(*shop.character(), "TestGauntlet", kIceMagic, kPylonNode);
        world::Entity* wolf = shop.wolf_east(-8.0F);
        const float full = wolf->vitals.health;
        world::Entity* pylon = shop.cast(30);
        shop.pass(3.0F);
        check(pylon != nullptr && shop.spell_counts().shots == 0 && wolf->vitals.health == full,
              "no projectile data: the Pylon lives, and it does not shoot");
    }
}

} // namespace

std::vector<Case> spell_kind_cases() {
    return {{"SB1", "spell kinds: the spell data of format 2 and the ability shots of the projectile data", data_format2},
            {"SB2", "spell kinds: the Pylon: the spawn, the payment at the spawn, the held cooldown, the health, the record", pylon_spawn},
            {"SB3", "spell kinds: the Pylon: the shots, the threat, the bite of a creature, the death chain, the cooldown from the end", pylon_fight},
            {"SB4", "spell kinds: the Pylon: its life, the leave of the caster, the mana of the press and of the spawn", pylon_life},
            {"SB5", "spell kinds: the trap: the toss, one trigger and its hits, the removal; the end of its life with no trigger", trap},
            {"SB6", "spell kinds: the Beacon: the attachment, the area that follows, the statuses, the pulses, the ends, the nodes", beacon},
            {"SB7", "spell kinds: two sessions observe one Pylon; the private cooldown; the statuses of a friend only (MP-10)", two_sessions},
            {"SB8", "spell kinds: the load of 3,003 spells and a far crowd, with a time limit", load},
            {"SB9", "spell kinds: the routing of a shot item of an ability", shot_routing},
            {"SB10", "spell kinds: no spell data, data of format 1, no projectile data", no_data}};
}

} // namespace soulwarden::backend::tests

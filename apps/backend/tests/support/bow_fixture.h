#pragma once

// The world of the cases of stage B1 (the three bow abilities of the build of the user: Penetrating Shot, Explosive Arrow, Rapid Shot).
// The data has the structure and the values of the game data of these abilities (the actions of playeractions_ability_bow, the
// fragments of player_bow.adb, the profiles of the ranged attack data, the damage rows, the node rows, the burn of Flame Arrow, the two
// explosion slices) with made-up profiles, weapons, slices, and damage types. All ids, names, and values are made up or are values of
// the game data; no id of an account.
//
// The proof that a case fails on the tree before the stage (stage B0, 4cfa9cf): that tree builds the cases with SOULWARDEN_B1_BASELINE (the
// members of the stage count as absent: no ability action, no clip shot, no hit rate, the counts of the stage are 0).

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
#include "support/spell_kind_fixture.h"
#include "support/test_navigation.h"
#include "world/abilities.h"
#include "world/projectiles.h"
#include "world/spells.h"

namespace soulwarden::backend::tests::bow {

using namespace shop;
using namespace projectile;
using std::chrono::milliseconds;

inline constexpr const char* kBowTable = "BowAbilityTable";
inline constexpr const char* kPenetrating = "Ability_Bow_PenetratingShot";
inline constexpr const char* kExplosive = "Ability_Bow_SplinterShot";
inline constexpr const char* kRapid = "Ability_Bow_RapidShot";
inline constexpr const char* kBloodSoaked = "Upgrade_Bow_PenetratingShot_DmgPerHit";
inline constexpr const char* kFlameArrow = "Upgrade_Bow_SplinterShot_CDRPerHIt";
inline constexpr const char* kContained = "Upgrade_Bow_SplinterShot_FiveA";
inline constexpr const char* kRapidAccuracy = "Upgrade_Bow_RapidAccuracy_CDROnLast";
inline constexpr const char* kFinalBlow = "Upgrade_Bow_RapidAccuracy_DmgOnLast";
inline constexpr const char* kBurn = "Status_ExplosiveShot_Burn";
inline constexpr const char* kExplosion = "Spell_Ability_Bow_ExplosiveShot_Explosion";
inline constexpr const char* kSmallExplosion = "Spell_Ability_Bow_ExplosiveShot_Explosion_SizeChange";
/** The cooldowns of the data (seconds). */
inline constexpr float kPenetratingCooldown = 18.0F;
inline constexpr float kExplosiveCooldown = 22.0F;
inline constexpr float kRapidCooldown = 14.0F;
/** The slot of the Spell state of the explosion slices (the structure of spell_aoe_explosionshot). */
inline constexpr std::uint32_t kExplosionSpellSlot = 1;

/** The damage rows of the three abilities (the factors of the game data; made-up damage types of the shop: 1 Slash, 3 Corruption). */
inline void add_bow_rows(gamedata::CombatData& combat) {
    const auto row = [&](const char* id, const char* kind, std::uint32_t type, float factor) {
        combat.damageRows.push_back(gamedata::DamageRow{id, kind, type, factor, 0.0F, 0.0F, 0.0F});
        return &combat.damageRows.back();
    };
    row("Bow_Penetrating_Attack", "Ability", 1, 1.5F);
    [[maybe_unused]] gamedata::DamageRow* a1 = row("Bow_Penetrating_AttackA1", "Ability", 1, 1.5F);
#ifndef SOULWARDEN_B1_BASELINE
    a1->hitRate = 0.1F;
    a1->hitRateMost = 0.5F;
#endif
    row("Bow_RapidAttack_Standard", "Ability", 1, 1.0F);
    row("Bow_RapidAttack_KnockBack", "Ability", 1, 1.25F);
    row("Bow_ExplosionShot_Projectile", "Ability", 1, 0.5F);
    row("Bow_ExplosionShot_Explosion", "Ability", 3, 1.35F)->noHeadshot = true;
    row("Bow_ExplosionShot_Explosion_Dmg", "Ability", 3, 1.7F)->noHeadshot = true;
    row("Common_DamageOverTime_Override", "DOT", 0, 1.0F);
}

#ifndef SOULWARDEN_B1_BASELINE
/** The ability actions of the bow (the times of player_bow.adb; the projectile data of format 2). */
inline std::vector<gamedata::AbilityActionData> bow_actions() {
    const auto windows = [](std::vector<const char*> abilities, float buffered, float instant) {
        std::vector<gamedata::ActionTransition> list;
        for (const char* ability : abilities) {
            list.push_back({ability, true, buffered});
            list.push_back({ability, false, instant});
        }
        return list;
    };
    gamedata::AbilityActionData penetrating;
    penetrating.action = "Bow_Penetrating_Shoot";
    penetrating.ability = kPenetrating;
    penetrating.fragment = "Bow_Shoot_UpperBody";
    penetrating.tags = "Bow+PenetratingAim";
    penetrating.clips = {{0.0333F, "PenetratingShot", "Bow_Penetrating_Attack", 0.0F, true}};
    penetrating.transitions = windows({kPenetrating, kExplosive, kRapid}, 0.2333F, 0.7F);
    gamedata::AbilityActionData spread;
    spread.action = "Bow_SpreadShot";
    spread.ability = kExplosive;
    spread.fragment = "Ability_Bow_ExplosiveArrow";
    spread.tags = "Bow+LongBow";
    spread.casts = {{0.4667F, "Spell_Ability_Bow_Explosive_Base", {kExplosive}, {kContained}},
                    {0.4667F, "Spell_Ability_Bow_Explosive_Upgrade", {kExplosive, kContained}, {}}};
    spread.transitions = windows({kPenetrating, kExplosive, kRapid}, 0.5F, 1.1667F);
    gamedata::AbilityActionData rapid;
    rapid.action = "Bow_RapidShot";
    rapid.ability = kRapid;
    rapid.fragment = "Ability_Bow_RapidShot";
    rapid.tags = "Bow+LongBow";
    rapid.canTransition = 2.6F;
    rapid.clips = {{0.3333F, "TripleShot", "Bow_RapidAttack_Standard", 0.0F, true},
                   {0.8667F, "TripleShot", "Bow_RapidAttack_Standard", 0.0F, false},
                   {1.7333F, "TripleShot", "Bow_RapidAttack_KnockBack", 0.0F, false}};
    rapid.transitions = windows({kPenetrating, kExplosive, kRapid}, 1.8681F, 2.6F);
    return {penetrating, spread, rapid};
}
#endif

/** The projectile data of the cases: the tables of stage 3.3b-S3, the bow attacks of the abilities, their clips, their spells. */
inline gamedata::ProjectileTables bow_projectiles() {
    gamedata::ProjectileTables tables = projectile_tables();
    tables.attacks.push_back(spell_kind::ranged("TestBowProfile", "PenetratingShot", 50.0F, 0.275F, 2.0F, 30.0F));
    tables.attacks.push_back(spell_kind::ranged("TestBowProfile", "TripleShot", 100.0F, 0.275F, 2.0F, 20.0F));
    tables.attacks.push_back(spell_kind::ranged("TestBowProfile", "ExplosionShot", 100.0F, 0.275F, 2.0F, 10.0F));
    tables.slices.push_back({"slices/test/arrow_spread.dynamicslice", 100.0F, 25.0F, 1.0F, {0.0F, 0.0F, -9.8F}, 10.0F, 8.0F});
    const auto spread = static_cast<std::int32_t>(tables.slices.size() - 1);
    tables.shots.push_back({"TestBowProfile", "PenetratingShot", "Bow_Penetrating_Attack", 0.0F, -1, "", "", false, 12, true, true});
    tables.shots.push_back({"TestBowProfile", "TripleShot", "Bow_RapidAttack_Standard", 0.0F, -1, "", "", false, 12, true, true});
    tables.shots.push_back({"TestBowProfile", "TripleShot", "Bow_RapidAttack_KnockBack", 0.0F, -1, "", "", false, 12, false, true});
    tables.shots.push_back({"TestBowProfile", "ExplosionShot", "Bow_ExplosionShot_Projectile", 0.0F, spread, "Spell_Ability_Bow_Explosive_Base", "",
                            false, 0, false, true});
    tables.shots.push_back({"TestBowProfile", "ExplosionShot", "Bow_ExplosionShot_Projectile", 0.0F, spread, "Spell_Ability_Bow_Explosive_Upgrade",
                            "", false, 0, false, true});
#ifndef SOULWARDEN_B1_BASELINE
    tables.actions = bow_actions();
#else
    // The tree before the stage refuses a shot of an ability with no spell (a clip shot).
    std::erase_if(tables.shots, [](const gamedata::ShotRule& shot) { return shot.ability && shot.spell.empty(); });
#endif
    return tables;
}

/** The explosion slice (the structure of spell_aoe_explosionshot: the Spell slot 1, the offense of the explosion row with its radius). */
inline gamedata::SpellSlice explosion_slice(bool small) {
    gamedata::SpellSlice slice;
    slice.path = small ? "slices/test/explosion_small.dynamicslice" : "slices/test/explosion.dynamicslice";
    slice.asset = spell_kind::made_up_asset(small ? 0x72 : 0x71);
    slice.root = small ? 0x0000030000000072ULL : 0x0000030000000071ULL;
    slice.interestRadius = 50.0F;
    slice.spellSlot = kExplosionSpellSlot;
    slice.slots = {{1, "Spell", 2912}, {2, "Ownership", 3217}, {3, "TemporaryAffiliation", 3563}};
    slice.volumes = {{"DetectionVolume", 0.0F, -1.0F}};
    slice.offenses = {{"ExplosionDamage", small ? "Bow_ExplosionShot_Explosion_Dmg" : "Bow_ExplosionShot_Explosion", small ? 1.25F : 2.5F}};
    return slice;
}

inline gamedata::SpellTables bow_spells() {
    gamedata::SpellTables tables;
    tables.slices = {explosion_slice(false), explosion_slice(true)};
    tables.spells = {{kExplosion, 0}, {kSmallExplosion, 1}};
    return tables;
}

/** The ability data of the cases: the shop, the cooldowns, the node rows, the burn, and the spell rows of the three abilities. */
inline gamedata::AbilityTables bow_abilities() {
    gamedata::AbilityTables tables = shop_abilities();
    tables.cooldowns.push_back({kPenetrating, kPenetratingCooldown, 0.0F});
    tables.cooldowns.push_back({kExplosive, kExplosiveCooldown, 0.0F});
    tables.cooldowns.push_back({kRapid, kRapidCooldown, 0.0F});
    std::vector<gamedata::StatusRow> rows = tables.statusEffects;
    rows.push_back(status(kBurn, 10.0F,
                          cells({{"TickRate", 1.0F}, {"HealthModifierDamageBased", -0.08F}, {"StackMax", 1.0F}, {"DurationMax", 20.0F},
                                 {"SourceRuneChargeOnTick", 0.26F}},
                                {{"DamageType", "Corruption"}, {"EffectCategories", "Debuff+Burn+Dot"}, {"RefreshDuration", "TRUE"}}),
                          true));
    tables.statusEffects = numbered(std::move(rows));
    tables.spells.push_back({"Spell_Ability_Bow_Explosive_Base", 0.0F, 0.0F, 10.0F, 0, 0.0F, 500.0F,
                             cells({{"SpawnSecondaryOnCollision", 1.0F}},
                                   {{"SecondarySpellID", kExplosion}, {"DamageTableRow", "Bow_ExplosionShot_Projectile"}, {"TargetTypes", "Enemy"}})});
    tables.spells.push_back({"Spell_Ability_Bow_Explosive_Upgrade", 0.0F, 0.0F, 10.0F, 0, 0.0F, 500.0F,
                             cells({{"SpawnSecondaryOnCollision", 1.0F}},
                                   {{"SecondarySpellID", kSmallExplosion}, {"DamageTableRow", "Bow_ExplosionShot_Projectile"}, {"TargetTypes", "Enemy"}})});
    tables.spells.push_back({kExplosion, 2.5F, 5.0F, 10.0F, 0, 0.0F, 1000.0F, cells({}, {{"TargetTypes", "Enemy"}, {"ShapeTypes", "Sphere"}})});
    tables.spells.push_back({kSmallExplosion, 1.25F, 5.0F, 10.0F, 0, 0.0F, 1000.0F, cells({}, {{"TargetTypes", "Enemy"}, {"ShapeTypes", "Sphere"}})});
    // The node rows of the game data (javelindata_ability_bow).
    tables.abilityRows.push_back({kRapidAccuracy,
                                  cells({{"OnHit", 1.0F}, {"OnBlockedHit", 1.0F}, {"RequireReaction", 1.0F}, {"NumConsecutiveHits", 3.0F},
                                         {"MaxConsecutiveHits", 3.0F}, {"ResetConsecutiveOnSuccess", 1.0F}, {"IgnoreResetConsecutiveOnDeath", 1.0F},
                                         {"InActionTime", 0.1F}, {"InSequenceTime", 0.1F}, {"CooldownTimer", -0.5F}},
                                        {{"WeaponTag", "Bow"}, {"RequiredAbilityId", kRapid}, {"DamageTableRow", "Bow_RapidAttack_Standard,Bow_RapidAttack_KnockBack"},
                                         {"InAction", "Bow_RapidShot"}, {"InSequence", kRapid}, {"CooldownId", kRapid},
                                         {"CDRImmediatelyOptions", "AbilitySpecific"}})});
    tables.abilityRows.push_back({kFinalBlow,
                                  cells({{"OnHit", 1.0F}, {"OnBlockedHit", 1.0F}, {"RequireReaction", 1.0F}, {"InActionTime", 0.1F},
                                         {"InSequenceTime", 0.1F}, {"BaseDamage", 0.25F}},
                                        {{"WeaponTag", "Bow"}, {"RequiredAbilityId", kRapid}, {"DamageTableRow", "Bow_RapidAttack_KnockBack"},
                                         {"InAction", "Bow_RapidShot"}, {"InSequence", kRapid}})});
    tables.abilityRows.push_back({kFlameArrow,
                                  cells({{"OnHit", 1.0F}, {"RequireReaction", 1.0F}, {"ForceStatusEffectDamageTableRow", 1.0F}},
                                        {{"WeaponTag", "Bow"}, {"RequiredAbilityId", kExplosive}, {"DamageTableRow", "Bow_ExplosionShot_Projectile"},
                                         {"StatusEffectDamageTableIdForRowOverride", "DamageTable"},
                                         {"StatusEffectDamageTableRowOverride", "Common_DamageOverTime_Override"}, {"OtherApplyStatusEffect", kBurn}})});
    tables.abilityRows.push_back({kContained, cells({{"RequireReaction", 1.0F}}, {{"WeaponTag", "Bow"}, {"RequiredAbilityId", kExplosive}})});
    tables.abilityRows.push_back({kBloodSoaked, cells({}, {{"WeaponTag", "Bow"}, {"RequiredAbilityId", kPenetrating},
                                                          {"DamageTableRow", "Bow_Penetrating_Attack"}, {"DamageTableRowOverride", "Bow_Penetrating_AttackA1"}})});
    tables.abilityRows.push_back({kPenetrating, cells({{"SupportsSmartcast", 1.0F}}, {{"WeaponTag", "Bow"}, {"CooldownId", kPenetrating}})});
    tables.abilityRows.push_back({kExplosive, cells({}, {{"WeaponTag", "Bow"}, {"CooldownId", kExplosive}})});
    tables.abilityRows.push_back({kRapid, cells({}, {{"WeaponTag", "Bow"}, {"CooldownId", kRapid}})});
    return tables;
}

/** The progression data of the cases: the shop and the bow tree with the three abilities and their nodes. */
inline gamedata::ProgressionData bow_progression() {
    gamedata::ProgressionData data = shop_progression();
    gamedata::RankTable ranks;
    for (std::uint32_t rank = 0; rank < 20; ++rank) {
        ranks.experience.push_back(10);
        ranks.resetCost.push_back(0);
    }
    data.progressions.push_back({kBowTable, 0, gamedata::ProgressionKind::weapon, 19, ranks});
    std::uint32_t column = 0;
    for (const auto& [id, active] : std::vector<std::pair<const char*, bool>>{{kPenetrating, true},   {kExplosive, true},   {kRapid, true},
                                                                              {kBloodSoaked, false},  {kFlameArrow, false}, {kContained, false},
                                                                              {kRapidAccuracy, false}, {kFinalBlow, false}}) {
        gamedata::AbilityNode node;
        node.id = id;
        node.table = gamedata::progression_id(kBowTable);
        node.column = column++;
        node.active = active;
        data.nodes.push_back(node);
    }
    return data;
}

/** The world of a case: the projectile shop with the bow, the rows, and the data of the three abilities. */
struct BowShop : ProjectileShop {
    explicit BowShop(bool observe = false, bool spellData = true, bool actions = true) : ProjectileShop(observe) {
        gamedata::CombatData combat = host.combat.data();
        add_bow_rows(combat);
        if (!host.combat.set(combat)) {
            throw std::runtime_error("bow test: the combat tables are not valid: " + host.combat.error());
        }
        host.progression.set(bow_progression());
        if (!host.abilities.set(bow_abilities())) {
            throw std::runtime_error("bow test: the ability data is not valid: " + host.abilities.error());
        }
        gamedata::ProjectileTables projectiles = bow_projectiles();
        if (!actions) {
#ifndef SOULWARDEN_B1_BASELINE
            projectiles.actions.clear();
#endif
            std::erase_if(projectiles.shots, [](const gamedata::ShotRule& shot) { return shot.ability && shot.spell.empty(); });
        }
        if (!host.projectiles.set(projectiles)) {
            throw std::runtime_error("bow test: the projectile data is not valid: " + host.projectiles.error());
        }
        if (spellData && !host.spells.set(bow_spells())) {
            throw std::runtime_error("bow test: the spell data is not valid: " + host.spells.error());
        }
    }
    /** The bow in the hand of a character, the three abilities (and more nodes) learned, and the bar Q R F. */
    void learn(world::Entity& character, std::vector<const char*> more = {}) {
        world::GiveOptions options;
        options.gearScore = 100;
        const world::ItemResult given = host.world.inventory().give(character.characterId, "TestBow", 1, options);
        if (!given.ok() || given.slots.empty()
            || !host.world.inventory().equip(character.characterId, given.slots.front(), world::paperdoll::kMainHand1).ok()) {
            throw std::runtime_error("bow test: the bow");
        }
        world::Entity& again = *host.world.find_character(character.characterId);
        again.combat.selectedWeapon = world::paperdoll::kMainHand1;
        const std::uint32_t tree = gamedata::progression_id(kBowTable);
        for (const char* node : {kPenetrating, kExplosive, kRapid}) {
            again.skills.pools[tree][gamedata::progression_id(node)].level = 1;
        }
        for (const char* node : more) {
            again.skills.pools[tree][gamedata::progression_id(node)].level = 1;
        }
        again.skills.bars[tree] = {gamedata::progression_id(kPenetrating), gamedata::progression_id(kExplosive), gamedata::progression_id(kRapid)};
    }
    /** A shot item of the client of A (from 0.5 m east of A and 0.6 m above its feet) to the east, or in a direction. */
    static InputShotItem bow_item(const char* attack, const char* row, std::array<float, 3> direction = {1.0F, 0.0F, 0.0F},
                                  const char* ammo = "DefaultArrow") {
        InputShotItem shot;
        shot.origin = {kPlace.x + 0.5F, kPlace.y, kHeight + 0.6F};
        const float size = std::hypot(direction[0], direction[1], direction[2]);
        shot.direction = {direction[0] / size, direction[1] / size, direction[2] / size};
        shot.attack = crc(attack);
        shot.row = row != nullptr ? crc(row) : 0;
        shot.ammo = ammo != nullptr ? crc(ammo) : 0;
        return shot;
    }
    /** One input step of A with a shot item (and bits). */
    void send_item(const InputShotItem& shot, std::vector<unsigned> bits = {}) {
        InputEntry entry = walker.entry(std::move(bits));
        entry.shotItems = {shot};
        walker.send(entry);
    }
    /** The input number of the last press of a key of A (press). */
    std::uint16_t pressInput{0};
    /** A press of a key of A (3 input steps: the press, the hold, the release). */
    void press(unsigned place) {
        walker.press(place);
        pressInput = static_cast<std::uint16_t>(walker.input - 2);
    }
    /** Steps with no input until the next input step of A is `steps` steps after the last press. */
    void until(int steps) {
        while (static_cast<int>(static_cast<std::uint16_t>(walker.input + 1 - pressInput)) < steps) {
            walker.idle(1);
        }
    }
    /** The input step `steps` after the last press with a shot item (the steps before it with no input). */
    void item_at(int steps, const InputShotItem& shot, std::vector<unsigned> bits = {}) {
        until(steps);
        send_item(shot, std::move(bits));
    }
    std::uint64_t presses(world::CastVerdict verdict) {
        return host.world.abilities().counts().presses[static_cast<std::size_t>(verdict)];
    }
    const world::AbilityCooldown* cooldown(const char* node, const char* character = kCharacterA) {
        const auto* timers = host.world.abilities().cooldowns_of(character);
        if (timers == nullptr) {
            return nullptr;
        }
        const auto found = timers->find(gamedata::progression_id(node));
        return found == timers->end() ? nullptr : &found->second;
    }
    /** The seconds from now to the end of a cooldown (negative: no cooldown). */
    float remaining(const char* node, const char* character = kCharacterA) {
        const world::AbilityCooldown* timer = cooldown(node, character);
        if (timer == nullptr) {
            return -1.0F;
        }
        return std::chrono::duration<float>(timer->end - host.world.clock().now()).count();
    }
    /** A wolf at a place (east and north of A), facing west; observed by the client of A after some steps. */
    world::Entity* wolf_at(float east, float north = 0.0F) {
        world::Entity* entity = host.world.creatures().spawn("Test_Wolf", at(kPlace.x + east, kPlace.y + north), 90.0F).entity;
        walker.idle(3);
        return host.world.creatures().find(entity->id);
    }
    float lost(world::EntityId id) {
        const world::Entity* entity = host.world.creatures().find(id);
        return entity != nullptr ? kWolfHealth - entity->vitals.health : -1.0F;
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
    /** The counts of the stage (0 on the tree before the stage). */
#ifndef SOULWARDEN_B1_BASELINE
    std::uint64_t volley_shots() {
        return host.world.abilities().counts().volleyShots;
    }
    std::uint64_t shot_confirms() {
        return host.world.abilities().counts().shotConfirms;
    }
    std::uint64_t explosions() {
        return host.world.abilities().counts().explosions;
    }
    std::uint64_t window_delays() {
        return host.world.abilities().counts().windowDelays;
    }
    std::uint64_t pierce_hits() {
        return host.world.projectiles().counts().pierceHits;
    }
    std::uint64_t damage_ticks() {
        return host.world.creatures().counts().damageTicks;
    }
#else
    std::uint64_t volley_shots() {
        return 0;
    }
    std::uint64_t shot_confirms() {
        return 0;
    }
    std::uint64_t explosions() {
        return 0;
    }
    std::uint64_t window_delays() {
        return 0;
    }
    std::uint64_t pierce_hits() {
        return 0;
    }
    std::uint64_t damage_ticks() {
        return 0;
    }
#endif
    bool has_effect(world::EntityId id, const char* status) {
        const world::Entity* entity = host.world.creatures().find(id);
        return entity != nullptr && std::any_of(entity->creature.effects.begin(), entity->creature.effects.end(),
                                                [&](const world::CreatureEffect& effect) { return effect.status == status; });
    }
    /**
     * The navigation data of the place: flat ground, and a wall (cells with a solid 3 m high) from `from` m to `to` m east of A for the whole
     * tile (no wall: `from` >= `to`). A projectile that falls stops on the ground; a wall stops a projectile and an area.
     */
    void wall_east(const std::filesystem::path& folder, float from, float to) {
        TestNavRegion region;
        region.tiles[{31, 31}] = [from, to](int x, int, std::vector<TestFloor>& floors) {
            const float px = 1984.0F + (static_cast<float>(x) + 0.5F) * 0.25F;
            if (px >= kPlace.x + from && px < kPlace.x + to) {
                floors = {{kHeight + 3.0F, gamedata::NavFloor::kBlocked}};
            }
        };
        region.write(folder);
        if (!host.navigation.set_directory(folder.string())) {
            throw std::runtime_error("bow test: the navigation data");
        }
        walker.idle(10);
    }
};

} // namespace soulwarden::backend::tests::bow

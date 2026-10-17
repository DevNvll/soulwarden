#pragma once

// The world of the cases of stage B2 (the rules of the Boltcaster of the bow build of the user, and the stacks of a status on a creature):
// the bow shop of stage B1 (support/bow_fixture.h) with the perks, the affix, the ability rows, and the status rows of the Boltcaster
// (Electrified, Bolt), of Shirking Shock II, and of Surprise Attack as the game data has them (the ids and the cells of the rows; the cells of
// a sound and of the user interface that no rule reads are left out), a made-up damage type Lightning with a made-up wire number, and the
// made-up rows of the test bow in the list of the bolt rule. No id of an account.
//
// The proof that a case fails on the tree before the stage (stage U3, 77ac4ff): that tree builds the cases with SOULWARDEN_B2_BASELINE (the
// members of the stage count as absent: an effect of a creature has one stack, the counts of the stage are 0).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "support/bow_fixture.h"
#include "support/stamina_fixture.h"
#include "world/stamina.h"

namespace soulwarden::backend::tests::boltcaster {

using namespace bow;

inline constexpr const char* kCharge = "Status_Perk_Artifact_Set1_Bow_StaticCharge";
inline constexpr const char* kRemoval = "Status_Perk_Artifact_Set1_Bow_StaticCharge_Removal";
inline constexpr const char* kBolt = "Status_Perk_Artifact_Set1_Bow_StaticDischarge";
inline constexpr const char* kShockTracker = "Status_ItemPerk_Weapon_DodgeSuccess_LightningDMG_Tracker";
inline constexpr const char* kShock = "Status_ItemPerk_Weapon_DodgeSuccess_LightningDMG";
inline constexpr const char* kSurpriseTracker = "Status_Passive_Bow_Hunter_DmgOnFirst_Tracker";
inline constexpr const char* kSurpriseSelf = "Status_Passive_Bow_Hunter_DmgOnFirst_SelfTracker";
/** The rows of the rules. */
inline constexpr const char* kElectrifiedRule = "GlobalPerk_Artifact_Set1_Bow";
inline constexpr const char* kChargeRule = "GlobalPerk_Artifact_Set1_BowPerk";
inline constexpr const char* kBoltRule = "GlobalPerk_Artifact_Set1_BowPerk2";
inline constexpr const char* kShockTrackerRule = "Ability_Perk_Weapon_DodgeSuccess_LightningDMG_Tracker";
inline constexpr const char* kShockRule = "Ability_Perk_Weapon_DodgeSuccess_LightningDMG";
inline constexpr const char* kSurprise = "Passive_Bow_Hunter_DmgOnFirst";
inline constexpr const char* kSurpriseHelper = "Passive_Bow_Hunter_DmgOnFirst_Tracker";
/** The perks. */
inline constexpr const char* kElectrified = "PerkID_Artifact_Set1_Bow";
inline constexpr const char* kBoltPerk = "PerkID_Artifact_Set1_BowPerk";
inline constexpr const char* kShirking = "PerkID_Fixed_Weapon_DodgeSuccess_LightningDMG";
/** A made-up stack row with a damage part (StackMax 2, DontReplaceStack) and a made-up dispel window (the form of the CC immunity row). */
inline constexpr const char* kTestStacks = "Test_Weaken_Stacks";
inline constexpr const char* kTestWindow = "Test_Removal_Window";
/**
 * A made-up perk with made-up rules of the new conditions and effects: a status of the target of the attacker only with a stack size
 * (CheckStatusEffectsOnTargetOwned), the stacks of a status of the attacker (StatusEffectStackSize), and the end of a status of the attacker
 * with AbilitySpecific (the form of Momentum of the rapier).
 */
inline constexpr const char* kTestPerk = "PerkID_Test_B2_Conditions";
inline constexpr const char* kOwnedRule = "Test_B2_OwnedCharges";
inline constexpr const char* kSelfStacksRule = "Test_B2_SelfStacks";
inline constexpr const char* kMomentumRule = "Test_B2_Momentum";
inline constexpr const char* kSelfStacks = "Test_B2_Self_Stacks";
inline constexpr const char* kMomentum = "Test_B2_Momentum_Status";
/** The made-up wire number of the damage type Lightning of the cases. */
inline constexpr std::uint8_t kLightningWire = 10;
/** The elemental mitigation of the test wolf (shop_combat). */
inline constexpr float kElemental = 0.1F;

/** The 21 bow rows of the bolt rule (game data) and the made-up rows of the test bow. */
inline std::string bolt_rows() {
    return "BowAttack1,BowAttack2,BowAttackJumpBack,BowAttackKnockBack,Bow_RapidAttack_Standard,Bow_RapidAttack_KnockBack,BowAttack_Hipshot,"
           "Bow_Gunbutt,Bow_PoisonShot,Bow_PoisonShot_Upgrade,Bow_PoisonShotCloud,Bow_Gunbutt_Large,Bow_Smoke_Attack,Bow_RainOfArrows_Attack,"
           "Bow_Penetrating_Attack,Bow_Penetrating_AttackA1,Bow_Penetrating_AttackA2,Bow_ExplosionShot_Projectile,Bow_ExplosionShot_Explosion,"
           "Bow_ExplosionShot_Explosion_Dmg,Bow_Spread_Shot,TestBowLight,TestBowHeavy";
}

/** The status rows of the stage (javelindata_statuseffects_artifacts rows 4 to 6, the dodge perks, the Surprise Attack trackers). */
inline std::vector<gamedata::StatusRow> boltcaster_statuses() {
    return {
        status(kCharge, 3.0F,
               cells({{"ShowInUITray", 1.0F}, {"ShowTextInDamageNumbers", 1.0F}, {"DurationMax", 3.0F}, {"TickRate", 1.0F}, {"IsNegative", 1.0F},
                      {"RefreshDuration", 1.0F}, {"StackMax", 3.0F}, {"RemoveUnappliedStacks", 1.0F}, {"UIPriority", 2.0F}, {"UiNameplatePriority", 2.0F}},
                     {{"EffectCategories", "Debuff+Tracking+Boltcaster"}}),
               true),
        status(kRemoval, 0.2F, cells({{"DurationMax", 0.2F}, {"TickRate", 1.0F}, {"StackMax", 1.0F}},
                                     {{"RemoveStatusEffects", kCharge}, {"RemoveStatusEffectCategories", "Boltcaster"}})),
        status(kBolt, 0.0F,
               cells({{"DurationMax", 0.1F}, {"TickRate", 1.0F}, {"HealthModifierDamageBased", -0.8F}, {"IsNegative", 1.0F}, {"StackMax", 3.0F},
                      {"UIPriority", 2.0F}, {"UiNameplatePriority", 2.0F}, {"ApplicationCooldown", 3.0F}},
                     {{"EffectCategories", "Debuff+DMG"}, {"DamageType", "Lightning"}, {"RemoveStatusEffects", kCharge},
                      {"RemoveStatusEffectCategories", "Boltcaster"}}),
               true),
        status(kShockTracker, 10.0F,
               cells({{"ShowInUITray", 1.0F}, {"ShowTextInDamageNumbers", 1.0F}, {"DurationMax", 12.0F}, {"TickRate", 1.0F}, {"RequireReaction", 1.0F},
                      {"RemoveOnDeath", 1.0F}, {"RefreshDuration", 1.0F}, {"StackMax", 1.0F}, {"UIPriority", 3.0F}, {"UiNameplatePriority", 3.0F}},
                     {{"EffectCategories", "Buff+DMG"}, {"HitCondition", "OnHit"}, {"EquipAbility", kShockRule}}),
               true, 1),
        status(kShock, 0.1F,
               cells({{"DurationMax", 1.0F}, {"TickRate", 1.0F}, {"HealthModifierDamageBased", -0.236F}, {"IsNegative", 1.0F}, {"StackMax", 1.0F},
                      {"SourceRuneChargeOnApply", 0.433F}},
                     {{"EffectCategories", "Debuff+DMG"}, {"DamageType", "Lightning"}})),
        status(kSurpriseTracker, 10.0F,
               cells({{"UseLightweightReplication", 1.0F}, {"DurationMax", 10.0F}, {"TickRate", 11.0F}, {"RemoveOnDeath", 1.0F}, {"StackMax", 1.0F}})),
        status(kSurpriseSelf, 15.0F,
               cells({{"UseLightweightReplication", 1.0F}, {"DurationMax", 15.0F}, {"TickRate", 1.0F}, {"RemoveOnDeath", 1.0F}, {"RefreshDuration", 1.0F},
                      {"StackMax", 1.0F}})),
        status(kTestStacks, 5.0F, cells({{"StackMax", 2.0F}, {"DontReplaceStack", 1.0F}, {"DMGSlash", -0.1F}}, {{"EffectCategories", "Debuff+Weaken"}}), true),
        status(kTestWindow, 1.0F, cells({{"StackMax", 1.0F}}, {{"RemoveStatusEffectCategories", "Weaken"}})),
        status(kSelfStacks, 10.0F, cells({{"StackMax", 3.0F}}, {{"EffectCategories", "Buff"}})),
        status(kMomentum, 3.0F, cells({{"StackMax", 1.0F}}, {{"EffectCategories", "Buff"}})),
    };
}

/** The tracker row of Shirking Shock II (in the perk table and in the ability data). */
inline gamedata::CellValues shock_tracker_cells() {
    return cells({{"OnHitTakenWhileInvulnerable", 1.0F}, {"RequireReaction", 1.0F}, {"InActionTime", 0.001F}, {"InSequenceTime", 0.001F}},
                 {{"DontHaveStatusEffect", kShockTracker}, {"InAction", "Dodge"}, {"InSequence", "Dodge_Roll,Dodge_Roll_Fast,Dodge_Roll_Slow"},
                  {"SelfApplyStatusEffect", kShockTracker}});
}

/** The node row and the helper row of Surprise Attack, and the row that the tracker of Shirking Shock II equips (javelindata_ability_bow, the perk tables). */
inline std::vector<gamedata::AbilityRow> boltcaster_rows() {
    return {
        {kSurprise, cells({{"OnHit", 1.0F}, {"BaseDamage", 0.2F}},
                          {{"WeaponTag", "Bow"}, {"TargetStatusEffect", kSurpriseTracker}, {"TargetStatusEffectComparison", "NotEqual"},
                           {"SelfApplyStatusEffect", kSurpriseSelf}, {"OtherApplyStatusEffect", kSurpriseTracker}})},
        {kSurpriseHelper, cells({{"EquipWhenUnsheathed", 1.0F}, {"OnHit", 1.0F}},
                                {{"WeaponTag", "Bow"}, {"RequiredEquippedAbilityId", kSurprise}, {"StatusEffect", kSurpriseSelf},
                                 {"TargetStatusEffect", kSurpriseTracker}, {"OtherApplyStatusEffect", kSurpriseTracker}})},
        {kShockRule, cells({{"OnHit", 1.0F}, {"RequireReaction", 1.0F}, {"ForceStatusEffectDamageTableRow", 1.0F}, {"ActivationCooldown", 0.5F}},
                           {{"StatusEffect", kShockTracker}, {"StatusEffectDamageTableIdForRowOverride", "DamageTable"},
                            {"StatusEffectDamageTableRowOverride", "Common_DamageOverTime_Override"}, {"OtherApplyStatusEffect", kShock},
                            {"RemoveStatusEffects", kShockTracker}})},
        {kShockTrackerRule, shock_tracker_cells()},
    };
}

/** The perk data of the cases: the shop perks and the three perks of the Boltcaster with their affix and their ability rows. */
inline gamedata::PerkTables boltcaster_perks() {
    gamedata::PerkTables tables = shop_perks();
    tables.perks.push_back(perk(kElectrified, "Inherent", "OnEquip", "Bow", "", {"Affix_Artifact_Set1_Bow"}, {kElectrifiedRule, kBoltRule}));
    tables.perks.push_back(perk(kBoltPerk, "Generated", "OnActive", "Bow", "", {}, {kChargeRule}));
    // The potency of Shirking Shock II: made-up steps that give 1.274 at the gear score 200 (the value of the game at 800: 1 + 75 x 0.00365).
    tables.perks.push_back(perk(kShirking, "Generated", "OnUnsheathed", "EquippableMainHand+EquippableTwoHand", "Shield", {}, {kShockTrackerRule}, 1,
                                {{0.0F, 0.00274F}}));
    tables.affixes["affix_artifact_set1_bow"] = cells({{"DamagePercentage", 0.99F}}, {{"DamageType", "Lightning"}});
    tables.abilities["globalperk_artifact_set1_bow"] =
        cells({{"OnHit", 1.0F}, {"BaseDamage", 0.2F}}, {{"AttackType", "Light,Heavy"}, {"DamageTypes", "Lightning"}});
    tables.abilities["globalperk_artifact_set1_bowperk"] =
        cells({{"OnHit", 1.0F}, {"RequireReaction", 1.0F}}, {{"AttackType", "Light,Heavy"}, {"OtherApplyStatusEffect", kCharge}});
    tables.abilities["globalperk_artifact_set1_bowperk2"] =
        cells({{"OnHit", 1.0F}, {"RequireReaction", 1.0F}, {"TargetStatusEffectStackSize", 3.0F}, {"DisableScalePerTargetSEStack", 1.0F},
               {"ActivationCooldown", 1.0F}},
              {{"DamageTableRow", bolt_rows()}, {"DamageTypes", "Lightning"}, {"TargetStatusEffect", kCharge}, {"TargetStatusEffectComparison", "Equal"},
               {"OtherApplyStatusEffect", std::string(kBolt) + "," + kRemoval}, {"RemoveTargetStatusEffectsList", kCharge}});
    tables.abilities["ability_perk_weapon_dodgesuccess_lightningdmg_tracker"] = shock_tracker_cells();
    tables.perks.push_back(perk(kTestPerk, "Generated", "OnEquip", "Bow", "", {}, {kOwnedRule, kSelfStacksRule, kMomentumRule}));
    tables.abilities["test_b2_ownedcharges"] =
        cells({{"OnHit", 1.0F}, {"TargetStatusEffectStackSize", 2.0F}, {"CheckStatusEffectsOnTargetOwned", 1.0F}, {"BaseDamage", 0.5F}},
              {{"TargetStatusEffect", kCharge}, {"TargetStatusEffectComparison", "GreaterThanOrEqual"}});
    tables.abilities["test_b2_selfstacks"] = cells({{"OnHit", 1.0F}, {"StatusEffectStackSize", 2.0F}, {"BaseDamage", 0.3F}},
                                                   {{"StatusEffect", kSelfStacks}, {"StatusEffectComparison", "GreaterThanOrEqual"}});
    tables.abilities["test_b2_momentum"] = cells({{"OnHit", 1.0F}, {"BaseDamage", 0.25F}},
                                                 {{"AttackType", "Light,Heavy"}, {"StatusEffect", kMomentum}, {"RemoveStatusEffects", "AbilitySpecific"},
                                                  {"StatusEffectsList", kMomentum}});
    return tables;
}

/** The world of a case: the bow shop with the data of the stage, and the stamina data (a dodge of Shirking Shock II). */
struct BoltShop : BowShop {
    explicit BoltShop(bool observe = false) : BowShop(observe) {
        gamedata::CombatData combat = host.combat.data();
        combat.damageTypes.push_back({"Lightning", kLightningWire, gamedata::DamageCategory::elemental});
        for (gamedata::DefenseRow& defense : combat.defenses) {
            defense.weakness.resize(combat.damageTypes.size(), 0.0F);
            defense.absorption.resize(combat.damageTypes.size(), 0.0F);
        }
        if (!host.combat.set(combat)) {
            throw std::runtime_error("boltcaster test: the combat tables are not valid: " + host.combat.error());
        }
        gamedata::AbilityTables tables = bow_abilities();
        std::vector<gamedata::StatusRow> rows = tables.statusEffects;
        for (gamedata::StatusRow& row : boltcaster_statuses()) {
            rows.push_back(std::move(row));
        }
        tables.statusEffects = numbered(std::move(rows));
        for (gamedata::AbilityRow& row : boltcaster_rows()) {
            tables.abilityRows.push_back(std::move(row));
        }
        if (!host.abilities.set(tables)) {
            throw std::runtime_error("boltcaster test: the ability data is not valid: " + host.abilities.error());
        }
        gamedata::ProgressionData progression = bow_progression();
        gamedata::AbilityNode node;
        node.id = kSurprise;
        node.table = gamedata::progression_id(kBowTable);
        node.column = 20;
        progression.nodes.push_back(node);
        host.progression.set(progression);
        if (!host.perks.set(boltcaster_perks())) {
            throw std::runtime_error("boltcaster test: the perk data is not valid: " + host.perks.error());
        }
        stamina::set_stamina(host);
    }
    /**
     * Gives a character the test bow with perks at a gear score in the first weapon slot, selects it, and learns the three abilities (the bar
     * Q R F) and more nodes of the bow tree.
     */
    void arm(const char* characterId, world::PerkIds perks, std::uint32_t gearScore = 100, std::vector<const char*> nodes = {}) {
        world::GiveOptions options;
        options.gearScore = gearScore;
        options.perks = std::move(perks);
        const world::ItemResult given = host.world.inventory().give(characterId, "TestBow", 1, options);
        if (!given.ok() || given.slots.empty() || !host.world.inventory().equip(characterId, given.slots.front(), world::paperdoll::kMainHand1).ok()) {
            throw std::runtime_error("boltcaster test: the bow with perks");
        }
        world::Entity& own = *host.world.find_character(characterId);
        own.combat.selectedWeapon = world::paperdoll::kMainHand1;
        const std::uint32_t tree = gamedata::progression_id(kBowTable);
        for (const char* node : {kPenetrating, kExplosive, kRapid}) {
            own.skills.pools[tree][gamedata::progression_id(node)].level = 1;
        }
        for (const char* node : nodes) {
            own.skills.pools[tree][gamedata::progression_id(node)].level = 1;
        }
        own.skills.bars[tree] = {gamedata::progression_id(kPenetrating), gamedata::progression_id(kExplosive), gamedata::progression_id(kRapid)};
        host.world.gear().refresh(own);
    }
    /** The perks of the Boltcaster of the build: Electrified, Bolt, Shirking Shock II. */
    static world::PerkIds boltcaster() {
        return world::PerkIds{kElectrified, kBoltPerk, kShirking, "", ""};
    }
    /** One hit of a character with the bow in its hand (a projectile of a row; an ability, or a light or heavy shot). */
    world::HitResult shot(world::Entity& target, const char* row, const char* ability = "", const char* characterId = kCharacterA) {
        world::Entity& own = *host.world.find_character(characterId);
        const world::ItemInstance* instance = world::Combat::weapon_in_hand(own);
        world::HitWeapon use{"TestBow", world::Combat::weapon_slot_in_hand(own).value_or(0), host.combat.weapon_of("TestBow"),
                             instance != nullptr ? instance->gearScore : 100U, ability};
        use.kind = world::HitKind::projectile;
        return host.world.combat().strike(own, target, use, *host.combat.row_index(row), 1.0F, "a test shot");
    }
    /** The stacks of a status on a creature now (0: none; the tree before the stage has one stack for each effect). */
    unsigned stacks(world::EntityId id, const char* status) {
        const world::Entity* entity = host.world.creatures().find(id);
        if (entity == nullptr) {
            return 0;
        }
        const auto now = host.clock.now();
        for (const world::CreatureEffect& effect : entity->creature.effects) {
            if (effect.status == status && now < effect.ends) {
#ifndef SOULWARDEN_B2_BASELINE
                return effect.stacks;
#else
                return 1;
#endif
            }
        }
        return 0;
    }
    /** @return The end of an effect of a creature in seconds from now (negative: none). */
    float ends_in(world::EntityId id, const char* status) {
        const world::Entity* entity = host.world.creatures().find(id);
        if (entity == nullptr) {
            return -1.0F;
        }
        for (const world::CreatureEffect& effect : entity->creature.effects) {
            if (effect.status == status) {
                return std::chrono::duration<float>(effect.ends - host.clock.now()).count();
            }
        }
        return -1.0F;
    }
    bool has_status(const char* status, const char* characterId = kCharacterA) {
        const world::Entity* own = host.world.find_character(characterId);
        return own != nullptr && host.world.statuses().has(*own, status);
    }
    /** The stage counts (0 on the tree before the stage). */
#ifndef SOULWARDEN_B2_BASELINE
    std::uint64_t stacks_added() {
        return host.world.creatures().counts().stacksAdded;
    }
    std::uint64_t blocked() {
        return host.world.creatures().counts().statusesBlocked;
    }
    std::uint64_t instant_damage() {
        return host.world.creatures().counts().instantDamage;
    }
#else
    std::uint64_t stacks_added() {
        return 0;
    }
    std::uint64_t blocked() {
        return 0;
    }
    std::uint64_t instant_damage() {
        return 0;
    }
#endif
};

/** @return The (status number, packed byte) entries of the compact map of a creature status fragment. */
inline std::vector<std::pair<std::uint16_t, std::uint8_t>> compact_entries(const Fragment& fragment) {
    std::vector<std::pair<std::uint16_t, std::uint8_t>> entries;
    const auto found = fragment.fields.find({3, 1});
    if (found == fragment.fields.end()) {
        return entries;
    }
    server::hub::Reader reader(found->second);
    const auto count = progress_wire::read_full_form(reader);
    for (std::uint32_t index = 0; count && index < *count; ++index) {
        const std::uint16_t status = reader.u16();
        entries.emplace_back(status, reader.u8());
    }
    return entries;
}

/** @return The packed byte of a status in the last compact map of a creature that a client got from the message `from` on (0: none). */
inline std::uint8_t last_packed(const FakeClient& client, std::uint16_t status, std::size_t from = 0) {
    const std::vector<Fragment> fragments = fragments_of(client, kStatusSlot, server::rep::kTypeStatusEffectsState, from);
    if (fragments.empty()) {
        return 0;
    }
    for (const auto& [index, packed] : compact_entries(fragments.back())) {
        if (index == status) {
            return packed;
        }
    }
    return 0;
}

/** @return The packed bytes of a status in each compact map of a creature that a client got from the message `from` on (0: absent). */
inline std::vector<std::uint8_t> packed_sequence(const FakeClient& client, std::uint16_t status, std::size_t from = 0) {
    std::vector<std::uint8_t> sequence;
    for (const Fragment& fragment : fragments_of(client, kStatusSlot, server::rep::kTypeStatusEffectsState, from)) {
        std::uint8_t packed = 0;
        for (const auto& [index, value] : compact_entries(fragment)) {
            packed = index == status ? value : packed;
        }
        sequence.push_back(packed);
    }
    return sequence;
}

/** @return The damage numbers of a type that a client got from the message `from` on (the amount of each entry of that type). */
inline std::vector<float> numbers_of(const FakeClient& client, std::uint8_t type, std::size_t from = 0) {
    std::vector<float> amounts;
    for (const auto& entries : damage_entries(client, from)) {
        for (const auto& [wire, amount] : entries) {
            if (wire == type) {
                amounts.push_back(amount);
            }
        }
    }
    return amounts;
}

} // namespace soulwarden::backend::tests::boltcaster

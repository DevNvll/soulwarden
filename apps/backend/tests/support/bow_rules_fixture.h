#pragma once

// The world of the cases of stage B4 (the other rules of the bow build of the user): the haste shop of stage B3 (support/haste_fixture.h)
// with the rows of the game data of Long Range, Finishing Shot, Enchanted Ward (plain and II), Refreshing, Vital Synergy, Alchemist Reprieve,
// The Thirst, the two Toast perks of the earring, the threshold abilities of the attributes of the build, and the food Desert Sunrise (the ids
// and the cells of the rows; the cells of a sound and of the user interface that no rule reads are left out), made-up trinket stat lines of
// the test jewelry (the form of the armor stats rows of the game: TrinketBonus 0.01 with one Scale...PerGS), a made-up DoT status and a
// made-up debuff on a creature, and the mark CanAttackCancel of the dodge fragments (stamina data format 2). The perks are on the test bow and
// the test armor (made-up places: the rules are the same for each item). No id of an account.
//
// The proof that a case fails on the tree before the stage (stage B3, 8fa1cc3): that tree builds the cases with SOULWARDEN_B4_BASELINE (the
// members of the stage count as absent: no trinket stat lines, no attack type of a contact, no mark CanAttackCancel, the counts of the stage
// are 0).

#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "support/haste_fixture.h"

namespace soulwarden::backend::tests::rules {

using namespace haste;

/** The nodes of the bow tree (javelindata_ability_bow). */
inline constexpr const char* kLongRange = "Passive_Bow_Hunter_DmgVsDist";
inline constexpr const char* kFinishing = "Passive_Bow_Hunter_DmgVsLow";
/** The perks and their rows (the perk tables of the game). */
inline constexpr const char* kWard = "PerkID_Armor_DefBasic";
inline constexpr const char* kWardRow = "Global_Armor_DefBasic";
inline constexpr const char* kWardII = "PerkID_Slottable_Common_DefBasic";
inline constexpr const char* kWardIIRow = "Ability_Perk_Armor_DefBasic";
inline constexpr const char* kRefreshing = "PerkID_Common_CDR";
inline constexpr const char* kRefreshingRow = "Global_Common_CDR";
inline constexpr const char* kSynergy = "PerkID_Chest_HealthStam_Synergy";
inline constexpr const char* kSynergyRow = "Global_Chest_HealthLowStamRegen";
inline constexpr const char* kSynergyRow2 = "Global_Chest_StamLowHealthRegen";
inline constexpr const char* kAlchemist = "PerkID_Amulet_PotionHeal";
inline constexpr const char* kAlchemistRow = "Global_Amulet_PotionHeal";
inline constexpr const char* kThirst = "PerkID_Artifact_Set1_Earring1";
inline constexpr const char* kThirstRow = "GlobalPerk_Artifact_Set1_Earring1";
inline constexpr const char* kThirstSurvival = "GlobalPerk_Artifact_Set1_Earring1_2";
inline constexpr const char* kThirstMana = "GlobalPerk_Artifact_Set1_Earring1_4";
inline constexpr const char* kToastDamage = "PerkID_Fixed_Earring_OnPotionDmg";
inline constexpr const char* kToastDamageRow = "Ability_Perk_Earring_OnPotionDmg";
inline constexpr const char* kToastFort = "PerkID_Fixed_Earring_OnPotionFort";
inline constexpr const char* kToastFortRow = "Ability_Perk_Earring_OnPotionFort";
/** The statuses (javelindata_statuseffects_perks, _items). */
inline constexpr const char* kSynergyHeal = "Status_Perk_Chest_StamLowHealthRegen";
inline constexpr const char* kSynergyStamina = "Status_Perk_Chest_HealthLowStamRegen";
inline constexpr const char* kEmpower = "Status_ItemPerk_Earring_OnPotionEmpower";
inline constexpr const char* kFortify = "Status_ItemPerk_Earring_OnPotionFort";
inline constexpr const char* kDotFood = "FoodDOTT5";
inline constexpr const char* kDotFoodRow = "Global_Consumable_FoodDoT_DurRed_T5";
/** Made-up statuses: a DoT and a debuff on a creature, a poison on a character (the form of AI_Poison_55), a status of no category. */
inline constexpr const char* kTestBurn = "Test_B4_Burn";
inline constexpr const char* kMark = "Test_B4_Mark";
inline constexpr const char* kPoison = "Test_B4_Poison";
inline constexpr const char* kPlain = "Test_B4_Plain";
/** The threshold abilities of the attributes (javelindata_ability_global: the rows of attributes.abilities of the progression file). */
inline constexpr const char* kDex25 = "Dex_Bonus_25_1";
inline constexpr const char* kDex50 = "Dex_Bonus_50_1";
inline constexpr const char* kDex150 = "Dex_Bonus_150_1";
inline constexpr const char* kStr25 = "Str_Bonus_25_1";
inline constexpr const char* kStr25Gather = "Str_Bonus_25_2";
inline constexpr const char* kCon100 = "Con_Bonus_100_1";
inline constexpr const char* kFoc100 = "Foc_Bonus_100_1";
/** The made-up jewelry with trinket stat lines (the armor stats rows AmuletFOCT5, RingINTT5, EarringCONT5 of the game). */
inline constexpr const char* kRing = "TestRing";
inline constexpr const char* kEarring = "TestEarring";
/** The consumables of the cases (shop_perks: TestPotion has PotionHealT5, SurvivalPotion 25 s; TestManaPotion PotionMana 15 s). */
inline constexpr const char* kDotFoodItem = "TestDotFood";

/** The status rows of the stage. */
inline std::vector<gamedata::StatusRow> rules_statuses() {
    gamedata::StatusRow empower = status(kEmpower, 8.0F,
                                         cells({{"ShowInUITray", 1.0F}, {"DurationMax", 16.0F}, {"TickRate", 1.0F}, {"RemoveOnDeath", 1.0F},
                                                {"DMGSlash", 0.0943F}, {"DMGThrust", 0.0943F}, {"StackMax", 1.0F}},
                                               {{"EffectCategories", "Buff+Empower"}}));
    empower.damage = 0.0943F;
    return {
        status(kSynergyHeal, -1.0F,
               cells({{"ShowInUITray", 1.0F}, {"AlwaysReplicate", 1.0F}, {"DurationMax", -1.0F}, {"TickRate", 1.0F}, {"HealMod", 0.1F},
                      {"RemoveOnDeath", 1.0F}, {"StackMax", 1.0F}},
                     {{"EffectCategories", "NonDispellableBuff+Anointed"}})),
        status(kSynergyStamina, -1.0F,
               cells({{"ShowInUITray", 1.0F}, {"AlwaysReplicate", 1.0F}, {"DurationMax", -1.0F}, {"TickRate", 1.0F}, {"StaminaRate", 0.1F},
                      {"RemoveOnDeath", 1.0F}, {"StackMax", 1.0F}},
                     {{"EffectCategories", "NonDispellableBuff+Energized"}})),
        empower,
        status(kFortify, 8.0F,
               cells({{"ShowInUITray", 1.0F}, {"DurationMax", 16.0F}, {"TickRate", 1.0F}, {"RemoveOnDeath", 1.0F}, {"PhysicalArmor", 0.1571F},
                      {"ElementalArmor", 0.1571F}, {"StackMax", 1.0F}, {"DontReplaceStack", 1.0F}},
                     {{"EffectCategories", "Buff+ArmorFortify"}})),
        status(kDotFood, 2400.0F, cells({{"ShowInUITray", 1.0F}, {"DurationMax", 43200.0F}},
                                        {{"EffectCategories", "ResistBoost+Food+FoodUtility"}, {"EquipAbility", kDotFoodRow}})),
        status(kTestBurn, 10.0F, cells({{"StackMax", 1.0F}, {"DurationMax", 20.0F}}, {{"EffectCategories", "Debuff+DoT+Burn"}}), true),
        status(kMark, 10.0F, cells({{"StackMax", 1.0F}}, {{"EffectCategories", "Debuff"}}), true),
        status(kPoison, 20.0F, cells({{"StackMax", 1.0F}, {"RemoveOnDeath", 1.0F}}, {{"EffectCategories", "Debuff+Poison+Dot"}})),
        status(kPlain, 10.0F, cells({{"StackMax", 1.0F}}, {{"EffectCategories", "Buff"}})),
        status("ATB_Str_Bonus_25_2_Effect", -1.0F, cells({{"EFFMining", 0.1F}})),
    };
}

/** The node rows, the threshold rows, and the row that the food equips (ability data). */
inline std::vector<gamedata::AbilityRow> rules_rows() {
    return {
        {kLongRange, cells({{"OnHit", 1.0F}, {"DistFromDefender", 10.0F}, {"BaseDamage", 0.1F}},
                           {{"WeaponTag", "Bow"}, {"DistComparisonType", "GreaterThanOrEqual"}})},
        {kFinishing, cells({{"TargetHealthPercent", 50.0F}, {"BaseDamage", 0.15F}}, {{"WeaponTag", "Bow"}})},
        {kDex25, cells({{"CritChance", 0.05F}}, {{"IgnoreDisabledAttackTypes", "Heartgem"}})},
        {kDex50, cells({{"OnHit", 1.0F}, {"MyHealthPercent", 100.0F}, {"BaseDamage", 0.05F}},
                       {{"IgnoreDisabledAttackTypes", "Heartgem"}, {"MyComparisonType", "Equal"}})},
        {kDex150, cells({{"TargetStatusEffectDurationMult", 0.05F}}, {{"IgnoreDisabledAttackTypes", "Heartgem"}, {"TargetStatusEffectDurationCats", "Dot"}})},
        {kStr25, cells({{"OnHit", 1.0F}, {"RequireReaction", 1.0F}, {"BaseDamage", 0.03F}},
                       {{"AttackType", "Light"}, {"IgnoreDisabledAttackTypes", "Heartgem"}})},
        {kStr25Gather, cells({{"IsNonCombatAbility", 1.0F}},
                             {{"OnEquipStatusEffect", "ATB_Str_Bonus_25_2_Effect"}, {"IgnoreDisabledAttackTypes", "Heartgem"}})},
        {kCon100, cells({{"MaxHealth", 0.03F}}, {{"IgnoreDisabledAttackTypes", "Heartgem"}})},
        {kFoc100, cells({{"MaxMana", 0.1F}, {"ManaRate", 0.2F}}, {{"IgnoreDisabledAttackTypes", "Heartgem"}})},
        {kDotFoodRow, cells({{"StatusEffectDurationReduction", -0.25F}},
                            {{"StatusEffectCategories", "Burn,Bleed,Poison,Shock,Frostburn"}, {"StatusEffectDurationCats", "Burn,Bleed,Poison"}})},
    };
}

/** The perk data: the haste perks of stage B3 and the perks of the stage (on the test bow, the armor, and the jewelry). */
inline gamedata::PerkTables rules_perks() {
    gamedata::PerkTables tables = haste_perks();
    const char* everywhere = "Bow+Armor+Jewelry";
    tables.perks.push_back(perk(kWard, "Generated", "OnEquip", everywhere, "", {}, {kWardRow}));
    tables.perks.push_back(perk(kWardII, "DefensiveCharm", "OnEquip", everywhere, "", {}, {kWardIIRow}));
    tables.perks.push_back(perk(kRefreshing, "Generated", "OnActive", everywhere, "", {}, {kRefreshingRow}));
    tables.perks.push_back(perk(kSynergy, "Generated", "OnActive", everywhere, "", {}, {kSynergyRow, kSynergyRow2}));
    tables.perks.push_back(perk(kAlchemist, "Generated", "OnEquip", everywhere, "", {}, {kAlchemistRow}));
    tables.perks.push_back(perk(kThirst, "Inherent", "OnEquip", everywhere, "", {}, {kThirstRow, kThirstSurvival, kThirstMana}));
    tables.perks.push_back(perk(kToastDamage, "Generated", "OnEquip", everywhere, "", {}, {kToastDamageRow}));
    tables.perks.push_back(perk(kToastFort, "Generated", "OnEquip", everywhere, "", {}, {kToastFortRow}));
    tables.abilities["global_armor_defbasic"] = cells({{"IsStackableAbility", 1.0F}, {"OnHitTaken", 1.0F}, {"RequireReaction", 1.0F},
                                                       {"BaseDamageReduction", 0.01F}},
                                                      {{"AttackType", "Light,Heavy"}, {"IgnoreDisabledAttackTypes", "Heartgem"}});
    tables.abilities["ability_perk_armor_defbasic"] =
        cells({{"IsStackableAbility", 1.0F}, {"IsStackableMax", 5.0F}, {"OnHitTaken", 1.0F}, {"RequireReaction", 1.0F}, {"BaseDamageReduction", 0.016F}},
              {{"AttackType", "Light,Heavy"}, {"IgnoreDisabledAttackTypes", "Heartgem"}});
    tables.abilities["global_common_cdr"] =
        cells({{"IsStackableAbility", 1.0F}, {"IsStackableMax", 3.0F}, {"CooldownTimer", -0.02F}}, {{"CDRImmediatelyOptions", "ActiveWeapon"}});
    tables.abilities["global_chest_healthlowstamregen"] = cells({});
    tables.abilities["global_chest_stamlowhealthregen"] = cells({});
    tables.abilities["global_amulet_potionheal"] = cells({{"ConsumableHealMod", 0.02F}});
    tables.abilities["globalperk_artifact_set1_earring1"] = cells({{"ConsumableHealMod", 0.33F}});
    tables.abilities["globalperk_artifact_set1_earring1_2"] =
        cells({{"OnUsedConsumable", 1.0F}, {"CooldownTimer", 0.25F}}, {{"CooldownId", "SurvivalPotion"}});
    tables.abilities["globalperk_artifact_set1_earring1_4"] = cells({{"OnUsedConsumable", 1.0F}, {"CooldownTimer", 0.25F}}, {{"CooldownId", "PotionMana"}});
    // The list of the potions of the game rows is long: the cases use the form with their test items (also a lower-case id, as "serumregent5").
    tables.abilities["ability_perk_earring_onpotiondmg"] =
        cells({{"OnUsedConsumable", 1.0F}, {"ActivationCooldown", 8.0F}},
              {{"IsConsumableIds", "TestPotion,testmanapotion"}, {"SelfApplyStatusEffect", kEmpower}});
    tables.abilities["ability_perk_earring_onpotionfort"] =
        cells({{"OnUsedConsumable", 1.0F}, {"ActivationCooldown", 8.0F}}, {{"IsConsumableIds", "TestPotion,testmanapotion"}, {"SelfApplyStatusEffect", kFortify}});
    tables.consumables.push_back({kDotFoodItem, {kDotFood}, {0.0F}, "Food", 1.0F, {}, {"Food"}, false, false});
    return tables;
}

/** The world of a case: the haste shop with the data of the stage. */
struct RulesShop : HasteShop {
    explicit RulesShop(bool observe = false) : HasteShop(observe) {
        gamedata::AbilityTables tables = host.abilities.tables();
        std::vector<gamedata::StatusRow> rows = tables.statusEffects;
        for (gamedata::StatusRow& row : rules_statuses()) {
            rows.push_back(std::move(row));
        }
        tables.statusEffects = numbered(std::move(rows));
        for (gamedata::AbilityRow& row : rules_rows()) {
            tables.abilityRows.push_back(std::move(row));
        }
        if (!host.abilities.set(tables)) {
            throw std::runtime_error("rules test: the ability data is not valid: " + host.abilities.error());
        }
        gamedata::ProgressionData progression = host.progression.data();
        std::uint32_t column = 50;
        for (const char* id : {kLongRange, kFinishing}) {
            gamedata::AbilityNode node;
            node.id = id;
            node.table = gamedata::progression_id(kBowTable);
            node.column = column++;
            progression.nodes.push_back(node);
        }
        progression.attributeAbilities["dexterity"] = {{25, {kDex25}}, {50, {kDex50}}, {150, {kDex150}}};
        progression.attributeAbilities["strength"] = {{25, {kStr25, kStr25Gather}}};
        progression.attributeAbilities["constitution"] = {{100, {kCon100}}};
        progression.attributeAbilities["focus"] = {{100, {kFoc100}}};
        host.progression.set(progression);
        if (!host.perks.set(rules_perks())) {
            throw std::runtime_error("rules test: the perk data is not valid: " + host.perks.error());
        }
        // The catalog of the bow shop (support/projectile_fixture.h) and the test jewelry, the head, and the food of the stage.
        std::vector<gamedata::ItemDefinition> items = shop::shop_catalog();
        items.push_back(shop::definition("TestBow", "Weapon", "Weapon+EquippableTwoHand+Bow+Ranged", 5, 100, 600));
        items.push_back(shop::definition("TestStaff", "Weapon", "Weapon+EquippableTwoHand+MagicStaff", 5, 100, 600));
        items.push_back(shop::definition(kRing, "Armor", "EquippableRing+Jewelry", 5, 100, 800));
        items.push_back(shop::definition(kEarring, "Armor", "EquippableToken+Jewelry", 5, 100, 800));
        items.push_back(shop::definition("TestHead", "Armor", "EquippableHead+Armor+Light", 5, 100, 600));
        items.push_back(shop::definition(kDotFoodItem, "Consumable", "Consumable+Food", 1, 0, 0, 10));
        host.catalog.set(items);
#ifndef SOULWARDEN_B4_BASELINE
        // The trinket stat lines of the jewelry (AmuletFOCT5, RingINTT5, EarringCONT5 of the game).
        host.catalog.set_trinkets({{"testamulet", gamedata::TrinketStats{0.01F, 0.625F, 0.0F, 0.0F}},
                                   {"testring", gamedata::TrinketStats{0.01F, 0.0F, 1.25F, 0.0F}},
                                   {"testearring", gamedata::TrinketStats{0.01F, 0.0F, 0.0F, 0.625F}}});
        // The mark CanAttackCancel of the dodge fragments (stamina data format 2; game data 0.2667 s).
        gamedata::StaminaTables stamina = stamina::stamina_tables();
        for (gamedata::DodgeFragment& fragment : stamina.fragments) {
            fragment.attackCancel = stamina::kBufferStart;
        }
        if (!host.stamina.set(stamina)) {
            throw std::runtime_error("rules test: the stamina data is not valid: " + host.stamina.error());
        }
#endif
    }
    /** Gives A an item with perks at a gear score and puts it into a paperdoll slot. @return True when it is there. */
    bool wear_on(const char* characterId, const char* item, std::uint32_t slot, world::PerkIds perks = {}, std::uint32_t gearScore = 100) {
        world::GiveOptions options;
        options.gearScore = gearScore;
        options.perks = std::move(perks);
        const world::ItemResult given = host.world.inventory().give(characterId, item, 1, options);
        return given.ok() && !given.slots.empty() && host.world.inventory().equip(characterId, given.slots.front(), slot).ok();
    }
    /** Sets the spent points of an attribute of a character (the value is the minimum plus them) and refreshes its gear. */
    void attribute(unsigned type, std::uint32_t value, const char* characterId = kCharacterA) {
        world::Entity& own = *host.world.find_character(characterId);
        own.attributes.spent[type] = value > own.attributes.minimum ? value - own.attributes.minimum : 0;
        host.world.gear().refresh(own);
    }
    /** A creature hit on a character with a contact of an attack type (the bite of the test wolf: Slash, physical). */
    world::CreatureHitOutcome bite(world::Entity& wolf, const char* attackType, const char* characterId = kCharacterA) {
        gamedata::CreatureContact contact;
        contact.factor = 1.0F;
        contact.type = "Slash";
        contact.wire = 2;
        contact.category = gamedata::DamageCategory::physical;
        contact.row = "Test_Bite";
#ifndef SOULWARDEN_B4_BASELINE
        contact.attackType = attackType;
#else
        (void)attackType;
#endif
        return host.world.health().creature_hit(wolf, contact, *host.world.find_character(characterId));
    }
    /** The counts of the stage (0 on the tree before the stage). */
#ifndef SOULWARDEN_B4_BASELINE
    std::uint64_t taken_reductions() {
        return host.world.passives().counts().takenReductions;
    }
    std::uint64_t script_statuses() {
        return host.world.passives().counts().scriptStatuses;
    }
    std::uint64_t consumable_rules() {
        return host.world.passives().counts().consumableRules;
    }
    std::uint64_t attack_cancels() {
        return host.world.stamina().counts().attackCancels;
    }
    float cooldown_line() {
        return character()->gear.cooldownReduction;
    }
#else
    std::uint64_t taken_reductions() {
        return 0;
    }
    std::uint64_t script_statuses() {
        return 0;
    }
    std::uint64_t consumable_rules() {
        return 0;
    }
    std::uint64_t attack_cancels() {
        return 0;
    }
    float cooldown_line() {
        return 0.0F;
    }
#endif
};

} // namespace soulwarden::backend::tests::rules

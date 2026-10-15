#pragma once

// The world of the itemization cases (stage 1.3b) and of the cases that use the same data (the status effects of a
// player, stage 3.3b-S1; the review corrections of stage 1.3b): the made-up items, perks, status rows, creature, combat
// tables, and the clients A and B. All ids, names, and values are made up, with the id texts of the game where a rule of
// the backend names them: no value of a capture is here.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "server/rep/combat_messages.h"
#include "server/rep/states/state_codecs.h"
#include "support/check.h"
#include "support/progress_decoder.h"
#include "support/server_host.h"
#include "support/state_decoder.h"
#include "support/test_players.h"
#include "support/test_progression.h"
#include "world/perk_roll.h"

namespace soulwarden::backend::tests::shop {

using nlohmann::json;

inline constexpr const char* kCharacterA = "aaaaaaaa-1111-4000-8000-0000000000a1";
inline constexpr const char* kCharacterB = "bbbbbbbb-1111-4000-8000-0000000000b1";
inline constexpr float kHeight = 50.0F;
inline constexpr state::WorldPosition kPlace{2000.0F, 2000.0F, kHeight};
inline constexpr std::chrono::microseconds kTick{33333};
inline constexpr std::uint32_t kWolfSlots[] = {5, 9, 15};
inline constexpr std::uint32_t kStatusSlot = 8;
inline constexpr std::uint32_t kBite = 3;
inline constexpr float kWolfHealth = 100000.0F;
/** The base damage of the test weapons (gear score 100, level 1, no attribute: the core damage is this value). */
inline constexpr float kWeaponDamage = 100.0F;
/** The input bits of the stage (client_input.cpp). */
inline constexpr unsigned kQuickBits[] = {404, 117, 140, 513};
inline constexpr unsigned kRuneBit = 450;
inline constexpr unsigned kAbilityBits[] = {402, 115, 139};
inline constexpr unsigned kVirtualBit = 479;
inline constexpr unsigned kHoldBit = 39;
inline constexpr unsigned kReleaseBit = 211;

inline const std::uint32_t kGreatAxe = gamedata::progression_id("GreatAxeAbilityTable");
inline const std::uint32_t kWarHammer = gamedata::progression_id("WarHammerAbilityTable");
inline const char* const kHammerNodes[] = {"Ability_WarHammer_WreckingBall", "Ability_WarHammer_Shockwave", "Ability_WarHammer_PathOfDestiny",
                                    "Upgrade_WarHammer_Shockwave_Weaken"};
inline const char* const kAxeNodes[] = {"Ability_Greataxe_Charge", "Ability_Greataxe_Maelstorm", "Ability_Greataxe_GravityWell"};

inline bool near(float left, float right, float tolerance = 0.01F) {
    return std::abs(left - right) <= tolerance;
}

inline state::WorldPosition at(float x, float y, float z = kHeight) {
    return state::WorldPosition{x, y, z};
}

inline gamedata::ItemDefinition definition(const char* id, const char* type, const char* itemClass, std::uint32_t tier,
                                     std::uint32_t minGs, std::uint32_t maxGs, std::uint32_t maxStack = 1) {
    gamedata::ItemDefinition item;
    item.id = id;
    item.nameKey = std::string("@") + id + "_MasterName";
    item.type = type;
    item.itemClass = itemClass;
    item.tier = tier;
    item.minGearScore = minGs;
    item.maxGearScore = maxGs;
    item.maxStack = maxStack;
    return item;
}

/** The items of the cases: the build kinds (weapons, armor, jewelry, a heart rune) and the consumables. */
inline std::vector<gamedata::ItemDefinition> shop_catalog() {
    return {definition("TestAxe", "Weapon", "Weapon+EquippableTwoHand+2HAxe+Melee", 5, 100, 600),
            definition("TestHammer", "Weapon", "Weapon+EquippableTwoHand+2HHammer+Melee", 5, 100, 600),
            definition("TestChest", "Armor", "EquippableChest+Armor+Heavy", 5, 100, 600),
            definition("TestLegs", "Armor", "EquippableLegs+Armor+Light", 5, 100, 600),
            definition("TestAmulet", "Armor", "EquippableAmulet+Jewelry", 5, 100, 600),
            definition("TestAmuletSpecial", "Armor", "EquippableAmulet+Jewelry", 5, 100, 600),
            definition("TestRune", "Weapon", "HeartGem", 3, 625, 625),
            definition("TestDropSword", "Weapon", "Weapon+EquippableMainHand+Sword+Melee", 3, 300, 400),
            definition("TestPotion", "Consumable", "Consumable+Potion", 1, 0, 0, 10),
            definition("TestPotionB", "Consumable", "Consumable+Potion", 1, 0, 0, 10),
            definition("TestRegenPotion", "Consumable", "Consumable+Potion", 1, 0, 0, 10),
            definition("TestManaPotion", "Consumable", "Consumable+Potion", 1, 0, 0, 10),
            definition("TestFood", "Consumable", "Consumable+Food", 1, 0, 0, 10),
            definition("TestOldFood", "Consumable", "Consumable+Food", 1, 0, 0, 10),
            definition("TestDust", "Consumable", "Consumable", 1, 0, 0, 10),
            definition("TestSiegePotion", "Consumable", "Consumable+SiegeWarOnly", 1, 0, 0, 10)};
}

/** The progression tables of the cases: the base values of the tests, the mana regeneration, the two weapons with nodes. */
inline gamedata::ProgressionData shop_progression() {
    gamedata::ProgressionData data = test_progression();
    data.manaRegenRate = 2.0F;
    data.manaRegenDelay = 0.0F;
    data.manaRegenDelayOnEmpty = 2.0F;
    gamedata::RankTable ranks;
    for (std::uint32_t rank = 0; rank < 20; ++rank) {
        ranks.experience.push_back(10);
        ranks.resetCost.push_back(0);
    }
    data.progressions.push_back({"GreatAxeAbilityTable", 0, gamedata::ProgressionKind::weapon, 19, ranks});
    data.progressions.push_back({"WarHammerAbilityTable", 0, gamedata::ProgressionKind::weapon, 19, ranks});
    std::uint32_t column = 0;
    for (const char* id : kAxeNodes) {
        gamedata::AbilityNode node;
        node.id = id;
        node.table = kGreatAxe;
        node.column = column++;
        node.active = true;
        data.nodes.push_back(node);
    }
    column = 0;
    for (const char* id : kHammerNodes) {
        gamedata::AbilityNode node;
        node.id = id;
        node.table = kWarHammer;
        node.column = column++;
        node.active = std::string(id).rfind("Ability_", 0) == 0;
        data.nodes.push_back(node);
    }
    return data;
}

inline gamedata::CreatureSlice wolf_slice() {
    gamedata::CreatureSlice slice;
    slice.path = "slices/test/wolf.dynamicslice";
    slice.asset.fill(0x21);
    slice.subId = 2;
    slice.root = 0x1000000000000021ULL;
    slice.interestRadius = 60.0F;
    slice.movementSlot = kWolfSlots[0];
    slice.vitalsSlot = kWolfSlots[1];
    slice.markerSlot = kWolfSlots[2];
    slice.statusSlot = kStatusSlot;
    slice.slots = {{kWolfSlots[0], "ActionList", 11},
                   {kStatusSlot, "StatusEffects", 4236},
                   {kWolfSlots[1], "Vitals", 15},
                   {kWolfSlots[2], "Marker", 5399}};
    slice.components = {{slice.root, 0x0000000000000A01ULL, "Root/Vitals"}, {slice.root + 3, 0x0000000000000C03ULL, "Head/HitVolume"}};
    slice.actions = {{"Idle_No_Task", "Idle", 0, false}, {"Reaction", "r_R2_F", 15, true},       {"Death", "r_Death", 4, true},
                     {"Attack_Bite", "Attack_Bite", 7, false}, {"Move_Combat", "Move_Combat", 11, false}};
    slice.reactions = {35, 10, 15, 26, 79};
    gamedata::CreatureBehavior values;
    values.sightAngle = 180.0F;
    values.sightDistance = 12.0F;
    values.combatAngle = 180.0F;
    values.combatDistance = 30.0F;
    values.restLeash = 20.0F;
    values.combatLeash = 70.0F;
    values.walkSpeed = 2.0F;
    values.runSpeed = 5.0F;
    slice.behavior = values;
    gamedata::CreatureContact contact;
    contact.time = 0.5F;
    contact.reach = 2.2F;
    contact.factor = 1.0F;
    contact.type = "Slash";
    contact.wire = 4;
    contact.category = gamedata::DamageCategory::physical;
    contact.row = "Test_Bite";
    contact.hitCharge = 3.0F;
    slice.attacks = {gamedata::CreatureAttack{kBite, 1.4F, {contact}}};
    return slice;
}

inline gamedata::CreatureKind wolf_kind() {
    gamedata::CreatureKind kind;
    kind.id = "Test_Wolf";
    kind.vitals = kind.id;
    kind.category = "Test_Wolf_Category";
    kind.vitalsId = 0x21112222;
    kind.categoryId = 0x21112223;
    kind.name = kind.id;
    kind.vitalsName = kind.id;
    kind.family = "Wildlife";
    kind.creatureType = "Solo";
    kind.healthFactor = 1.0F;
    kind.level = 1;
    kind.bodies = {{0, 100}};
    return kind;
}

/** The combat tables of the cases. The wolf has the mitigation 0.2 (physical) and 0.1 (elemental). */
inline gamedata::CombatData shop_combat() {
    gamedata::CombatData data;
    data.constants = {0.025F, 0.0112F, 0.6667F, 5, 100, 500};
    data.damageTypes = {{"True", 1, gamedata::DamageCategory::direct},
                        {"Slash", 4, gamedata::DamageCategory::physical},
                        {"Strike", 6, gamedata::DamageCategory::physical},
                        {"Corruption", 11, gamedata::DamageCategory::elemental},
                        {"Arcane", 7, gamedata::DamageCategory::elemental}};
    data.attributeMinimum = 5;
    for (const unsigned attribute : {world::attribute::kStrength, world::attribute::kDexterity, world::attribute::kIntelligence,
                                     world::attribute::kFocus}) {
        data.attributeDamage[attribute] = {0.0F, 1.25F};
    }
    // id, attack type, type, factor, added, attack charge, hit charge
    data.damageRows = {{"Test_Light", "Light", 1, 1.0F, 0.0F, 6.0F, 0.0F},
                       {"Test_Heavy", "Heavy", 1, 1.0F, 0.0F, 0.0F, 0.0F},
                       {"Warhammer_Damage_WreckingBall", "Ability", 2, 0.9F, 0.0F, 0.0F, 0.0F},
                       {"Warhammer_Damage_WreckingBall_Push", "Ability", 2, 0.9F, 0.0F, 0.0F, 0.0F},
                       {"Warhammer_Damage_Shockwave", "Ability", 2, 0.6F, 0.0F, 0.0F, 0.0F},
                       {"Warhammer_Damage_Shockwave_L2", "Ability", 2, 0.6F, 0.0F, 0.0F, 0.0F},
                       {"Warhammer_Damage_PathOfDestiny", "Ability", 2, 0.7F, 0.0F, 0.0F, 0.0F},
                       {"GreatAxe_GravityWell", "Ability", 1, 0.25F, 0.0F, 0.0F, 0.0F},
                       {"GreatAxe_GravityWell_Burst", "Ability", 2, 1.0F, 0.0F, 0.0F, 0.0F},
                       {"GreatAxe_GravityWell_Burst_Perk", "Ability", 3, 0.5F, 0.0F, 0.0F, 0.0F},
                       {"Rune_Detonate_AoE", "Heartgem", 4, 1.0F, 0.0F, 0.0F, 0.0F}};
    gamedata::WeaponRow axe;
    axe.id = "TestAxeRow";
    axe.melee = true;
    axe.baseDamage = kWeaponDamage;
    axe.lightRow = 0;
    axe.heavyRow = 1;
    axe.mastery = "GreatAxeAbilityTable";
    gamedata::WeaponRow hammer = axe;
    hammer.id = "TestHammerRow";
    hammer.mastery = "WarHammerAbilityTable";
    gamedata::WeaponRow rune = axe;
    rune.id = "TestRuneRow";
    rune.mastery.clear();
    rune.lightRow.reset();
    rune.heavyRow.reset();
    data.weapons = {axe, hammer, rune};
    data.items = {{"testaxe", 0}, {"testhammer", 1}, {"testrune", 2}};
    gamedata::DefenseRow wolf;
    wolf.physical = 0.2F;
    wolf.elemental = 0.1F;
    wolf.weakness.assign(data.damageTypes.size(), 0.0F);
    wolf.absorption.assign(data.damageTypes.size(), 0.0F);
    data.defenses = {wolf};
    data.vitals = {{"test_wolf", 0}};
    data.armor = {{"testchest", {1.0F, 1.0F, 0.5F}}, {"testlegs", {1.0F, 1.0F, 0.3F}}};
    return data;
}

/** The status rows of the cases: the numbers are the positions in the CRC order (the rule of the loader, finding F10). */
inline std::vector<gamedata::StatusRow> numbered(std::vector<gamedata::StatusRow> rows) {
    std::vector<std::pair<std::uint32_t, std::size_t>> order;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        order.emplace_back(gamedata::progression_id(rows[index].id), index);
    }
    std::sort(order.begin(), order.end());
    for (std::size_t position = 0; position < order.size(); ++position) {
        rows[order[position].second].index = static_cast<std::uint16_t>(100 + position * 10);
    }
    return rows;
}

inline gamedata::StatusRow status(const char* id, float duration, gamedata::CellValues cells, bool nameplate = false, std::uint32_t stopOnHit = 0) {
    gamedata::StatusRow row;
    row.id = id;
    row.duration = duration;
    row.nameplate = nameplate;
    row.stopOnHit = stopOnHit;
    row.cells = std::move(cells);
    return row;
}

inline gamedata::CellValues cells(std::map<std::string, float, std::less<>> numbers, std::map<std::string, std::string, std::less<>> texts = {}) {
    gamedata::CellValues values;
    values.numbers = std::move(numbers);
    values.texts = std::move(texts);
    return values;
}

/** The ability data of the cases: the cooldowns and the spells of the build, the status rows of the perks, the rune, and the consumables. */
inline gamedata::AbilityTables shop_abilities(bool cost = false) {
    gamedata::AbilityTables tables;
    tables.cooldowns = {{"Ability_Greataxe_Charge", 20.0F, 0.0F},     {"Ability_Greataxe_Maelstorm", 20.0F, 0.0F},
                        {"Ability_Greataxe_GravityWell", 30.0F, 0.0F}, {"Ability_WarHammer_WreckingBall", 15.0F, 0.0F},
                        {"Ability_WarHammer_Shockwave", 20.0F, 0.0F},  {"Ability_WarHammer_PathOfDestiny", 22.0F, 2.0F}};
    tables.statusEffects = numbered({
        status("WarHammer_Shockwave_Stun", 2.0F, {}, true),
        status("WarHammer_Shockwave_Weaken", 10.0F, {}, true),
        status("Status_Perk_Hammer_Shockwave_Rend", 7.0F,
               cells({{"PhysicalArmor", -0.05F}, {"ElementalArmor", -0.05F}}, {{"EffectCategories", "Debuff+ArmorRend"}}), true),
        status("Status_Perk_Weapon_Attunement_VoidDMG", 0.1F,
               cells({{"TickRate", 1.0F}, {"HealthModifierDamageBased", -0.1F}, {"SourceRuneChargeOnApply", 0.216F}},
                     {{"DamageType", "Corruption"}, {"EffectCategories", "Debuff+DMG"}})),
        status("ChainLVoidTrigger", 0.1F, cells({}, {{"CastSpell", "ChainVoidPerk3"}})),
        status("ChainVoidDamage", 0.0F, cells({{"HealthModifierDamageBased", -0.15F}, {"SourceRuneChargeOnApply", 0.14F}},
                                              {{"DamageType", "Corruption"}})),
        status("PotionHealT5", -1.0F, cells({{"Health", 400.0F}, {"HealthModifierPercent", 0.1F}}, {{"EffectCategories", "SurvivalPotion"}})),
        status("GrandPotionRegen", -1.0F, cells({{"TickRate", 1.0F}, {"Health", 40.0F}}, {{"EffectCategories", "Heal+SurvivalPotion"}})),
        status("PotionMana", -1.0F, cells({{"Mana", 40.0F}})),
        status("FoodCONT5", 2400.0F, cells({{"MODConstitution", 10.0F}}, {{"EffectCategories", "Attributes+Food"}})),
        status("FoodCONT4", 2400.0F, cells({{"MODConstitution", 4.0F}}, {{"EffectCategories", "Attributes+Food"}})),
        status("GemstoneDust", 20.0F, cells({{"PhysicalArmor", 0.5F}}), false, 2),
        // The statuses of the heart rune with the cells of the game data that the rules read: the end status, the removal
        // marks, the haste of the variants (MoveSpeedMod is the field of the row: the status() helper sets it below).
        status("Rune_Detonate", 3.0F, cells({{"RemoveOnDeath", 1.0F}, {"DontApplyOnEndEffectOnRemove", 1.0F}, {"ShowInUITray", 1.0F}},
                                            {{"OnEndStatusEffect", "Rune_Detonate_CastAoE"}})),
        status("Rune_Detonate_Major", 3.0F, cells({{"RemoveOnDeath", 1.0F}, {"DontApplyOnEndEffectOnRemove", 1.0F}, {"ShowInUITray", 1.0F},
                                                   {"SprintSpeedMod", 0.2F}},
                                                  {{"OnEndStatusEffect", "Rune_Detonate_CastAoE"}, {"EffectCategories", "Haste"}})),
        status("Rune_Detonate_Dominator", 3.0F, cells({{"PhysicalArmor", -0.5F}, {"ElementalArmor", -0.5F}, {"RemoveOnDeath", 1.0F},
                                                       {"DontApplyOnEndEffectOnRemove", 1.0F}, {"ShowInUITray", 1.0F}, {"SprintSpeedMod", 0.2F}},
                                                      {{"OnEndStatusEffect", "Rune_Detonate_CastAoE"}, {"EffectCategories", "Haste+ArmorRend"}})),
        status("Rune_Detonate_CastAoE", 1.0F, cells({{"RemoveOnDeath", 1.0F}}, {{"CastSpell", "Rune_Detonate_AoE"}})),
    });
    for (gamedata::StatusRow& row : tables.statusEffects) {
        if (row.id == "Rune_Detonate_Major" || row.id == "Rune_Detonate_Dominator") {
            row.moveSpeed = 0.2F;
        }
    }
    // The limits of two categories of the game data (StatusEffectCategoryData).
    tables.statusCategories = {{"Haste", {{"MoveSpeedMod", 0.4F}, {"SprintSpeedMod", 0.4F}}},
                               {"ArmorFortify", {{"PhysicalArmor", 2.0F}, {"ElementalArmor", 2.0F}}}};
    gamedata::SpellRow shockwave{"WarHammer_Spell_Shockwave", 3.0F, 0.0F, 0.0F, 0, 0.0F, 6000.0F, {}};
    if (cost) {
        // The made-up cost of case IZ8: the spell of Shockwave names a cost id of the mana table.
        shockwave.cells.texts["ManaCost"] = "Test_ManaCost";
    }
    tables.spells = {shockwave,
                     {"WarHammer_Spell_Shockwave_L2", 4.0F, 0.0F, 0.0F, 0, 0.0F, 6000.0F, {}},
                     {"WarHammer_Spell_PathOfDestiny", 4.0F, 2.0F, 3.8F, 3, 200.0F, 770.0F, {}},
                     {"WarHammer_Spell_WreckingBall", 0.0F, 0.0F, 0.0F, 0, 0.0F, 0.0F, cells({}, {{"ManaCost", "SpecialAttack"}})},
                     {"Rune_Detonate_AoE", 5.0F, 5.0F, 20.0F, 0, 0.0F, 300.0F, {}},
                     {"InsatiableGravityWell", 4.0F, 0.0F, 0.0F, 0, 0.0F, 300.0F, cells({}, {{"DamageTableRow", "GreatAxe_GravityWell_Burst_Perk"}})},
                     {"ChainVoidPerk3", 6.0F, 0.0F, 0.0F, 0, 0.0F, 3000.0F,
                      cells({{"MaxChainNum", 3.0F}, {"ChainDelayDuration", 0.3F}}, {{"StatusEffects", "ChainVoidDamage"}, {"DamageType", "Corruption"}})}};
    tables.manaCosts = {{"Test_ManaCost", 30.0F}};
    // A learned node that makes the cost lower (ManaCostMult of an ability row: client code 0x67EAED0).
    tables.abilityCosts = {{"Upgrade_WarHammer_Shockwave_Weaken", {"Test_ManaCost"}, -0.5F, {}, 0.0F}};
    return tables;
}

inline gamedata::PerkRow perk(const char* id, const char* type, const char* condition, const char* itemClass, const char* labels,
                       std::vector<std::string> affixes, std::vector<std::string> abilities, std::uint32_t channel = 1,
                       std::vector<gamedata::ScalingStep> scaling = {}) {
    gamedata::PerkRow row;
    row.id = id;
    row.type = type;
    row.condition = condition;
    row.itemClass = itemClass;
    row.labels = labels;
    row.affixes = std::move(affixes);
    row.abilities = std::move(abilities);
    row.channel = channel;
    row.scaling = std::move(scaling);
    return row;
}

/** The perk data of the cases. The scaling of the potency perks: 0.002 for each gear score from 100 to 600 (potency 2 at 600). */
inline gamedata::PerkTables shop_perks() {
    gamedata::PerkTables tables;
    const std::vector<gamedata::ScalingStep> scaled = {{0.0F, 0.002F}, {600.0F, 0.0F}};
    tables.perks = {
        perk("PerkID_Stat_TwoHandSoldier", "Inherent", "OnEquip", "EquippableTwoHand+EquippableMainHand+Jewelry", "Attribute", {"Test_Affix_Zero"}, {}),
        perk("PerkID_Stat_ArmorSoldier", "Inherent", "OnEquip", "Armor+Jewelry", "Attribute", {"Test_Affix_Zero"}, {}),
        perk("PerkID_StatSwap_BaseDamage", "Inherent", "OnEquip", "", "Attribute", {}, {"Test_Ability_BaseDamage"}),
        perk("PerkID_StatSwap_HP", "Inherent", "OnEquip", "", "Attribute", {}, {"Test_Ability_HP"}),
        perk("PerkID_Test_Strength", "Generated", "OnEquip", "", "Test_Strength", {"Test_Affix_Strength"}, {}, 1, scaled),
        perk("PerkID_Gem_VoidDMG4", "Gem", "OnEquip", "EquippableMainHand+EquippableTwoHand", "ConversionGem", {"Test_Affix_VoidConversion"}, {}),
        perk("PerkID_Gem_SlashWard", "Gem", "OnEquip", "EquippableAmulet+Armor", "WardGem", {"Test_Affix_SlashWard"}, {}),
        perk("PerkID_Gem_EmptyGemSlot", "Gem", "OnEquip", "", "EmptyGem", {}, {}),
        perk("PerkID_Ability_Hammer_Shockwave", "Generated", "OnActive", "2HHammer+Armor", "Skill", {}, {"GlobalPerk_Ability_Hammer_Shockwave"},
             1, scaled),
        perk("PerkID_Ability_GreatAxe_GravityWell", "Generated", "OnActive", "2hAxe+Armor", "Skill+2hAxe", {},
             {"GlobalPerk_Ability_GreatAxe_GravityWell"}, 1, scaled),
        perk("PerkID_Weapon_CDRBasic", "Generated", "OnUnsheathed", "EquippableMainHand+EquippableTwoHand", "Proc", {}, {"Global_Weapon_CDRBasic"},
             1, scaled),
        perk("PerkID_Weapon_Attunement_Void", "Generated", "OnActive", "EquippableMainHand+EquippableTwoHand", "Elemental", {},
             {"Global_Weapon_Attunement_Void"}),
        perk("PerkID_Weapon_ChainVoid", "Generated", "OnUnsheathed", "EquippableMainHand+EquippableTwoHand", "Elemental+Void", {},
             {"Global_Weapon_ChainVoid"}),
        perk("PerkID_Test_Unknown_Condition", "Generated", "OnEquip", "", "Test_Unknown", {}, {"Test_Ability_UnknownCondition"}),
        perk("PerkID_HeartgemRune_Detonate_Major", "Generated", "OnEquip", "HeartGem", "", {}, {"Heartgem_Rune_Detonate_Major"}),
        perk("PerkID_HeartgemRune_Detonate_Dominator", "Generated", "OnEquip", "HeartGem", "", {}, {"Heartgem_Rune_Detonate_Dominator"}),
        perk("PerkID_Test_Channel3", "Generated", "OnEquip", "", "Test_High", {}, {}, 3),
        perk("PerkID_Test_Deprecated", "Generated", "OnEquip", "", "Test_Old", {}, {}),
        perk("PerkID_Fixed_Earring_PotionCDR", "Generated", "OnEquip", "", "ConsumeCDR", {}, {"Global_Earring_PotionCDR"}),
    };
    tables.perks[17].deprecated = true;
    tables.affixes = {{"test_affix_zero", cells({{"MODStrength", 0.0F}})},
                      {"test_affix_strength", cells({{"MODStrength", 10.0F}})},
                      {"test_affix_voidconversion", cells({{"DamagePercentage", 0.5F}}, {{"DamageType", "Corruption"}})},
                      {"test_affix_slashward", cells({{"ABSSlash", 0.25F}})}};
    tables.abilities = {
        {"test_ability_basedamage", cells({{"BaseDamage", 0.1F}, {"IsStackableMax", 2.0F}})},
        {"test_ability_hp", cells({{"MaxHealth", 0.1F}, {"IsStackableMax", 6.0F}})},
        {"globalperk_ability_hammer_shockwave",
         cells({{"OnHit", 1.0F}, {"RequireReaction", 1.0F}},
               {{"DamageTableRow", "Warhammer_Damage_Shockwave,Warhammer_Damage_Shockwave_L2"},
                {"TargetStatusEffectCategory", "IgnoreDebuffs"}, {"TargetStatusEffectComparison", "NotEqual"},
                {"OtherApplyStatusEffect", "Status_Perk_Hammer_Shockwave_Rend"}})},
        {"globalperk_ability_greataxe_gravitywell",
         cells({{"OnHit", 1.0F}, {"DmgPctToHealth", 0.15F}, {"ActivationCooldown", 1.0F}}, {{"RemoteDamageTableRow", "GreatAxe_GravityWell_Burst"}})},
        {"global_weapon_cdrbasic",
         cells({{"OnHit", 1.0F}, {"ActivationCooldown", 0.4F}, {"CooldownTimer", -0.05F}},
               {{"AttackType", "Light,Heavy"}, {"CDRImmediatelyOptions", "ActiveWeapon"}})},
        {"global_weapon_attunement_void",
         cells({{"OnHit", 1.0F}, {"ActivationCooldown", 1.5F}}, {{"OtherApplyStatusEffect", "Status_Perk_Weapon_Attunement_VoidDMG"}})},
        {"global_weapon_chainvoid",
         cells({{"OnHit", 1.0F}, {"ActivationCooldown", 2.0F}}, {{"AttackType", "Light,Heavy"}, {"OtherApplyStatusEffect", "ChainLVoidTrigger"}})},
        {"test_ability_unknowncondition", cells({{"OnHit", 1.0F}, {"DamageIsMelee", 1.0F}, {"BaseDamage", 0.5F}})},
        {"heartgem_rune_detonate_major", cells({})},
        {"heartgem_rune_detonate_dominator", cells({{"OnHit", 1.0F}, {"BaseDamage", 0.2F}}, {{"RemoteDamageTableRow", "Rune_Detonate_AoE"}})},
        {"global_earring_potioncdr", cells({{"OnUsedConsumable", 1.0F}, {"CooldownTimer", -0.2F}})},
        {"ability_rune_detonate", cells({})},
    };
    gamedata::PerkBucket attributes;
    attributes.id = "Test_AttributeBucket";
    attributes.type = "Inherent";
    attributes.chance = 1.0F;
    attributes.entries = {{"PerkID_Stat_TwoHandSoldier", false, 1.0F}};
    gamedata::PerkBucket generated;
    generated.id = "Test_GeneratedBucket";
    generated.type = "Generated";
    generated.chance = 0.5F;
    generated.entries = {{"PerkID_Weapon_CDRBasic", false, 3.0F}, {"PerkID_Test_Channel3", false, 100.0F},
                         {"PerkID_Test_Deprecated", false, 100.0F}, {"Test_NestedBucket", true, 1.0F}};
    gamedata::PerkBucket nested;
    nested.id = "Test_NestedBucket";
    nested.type = "Generated";
    nested.chance = 0.0F;
    nested.entries = {{"PerkID_Weapon_Attunement_Void", false, 1.0F}};
    gamedata::PerkBucket gems;
    gems.id = "Test_GemBucket";
    gems.type = "Gem";
    gems.chance = 1.0F;
    gems.entries = {{"PerkID_Gem_EmptyGemSlot", false, 1.0F}};
    tables.buckets = {attributes, generated, nested, gems};
    gamedata::PerkLayout drop;
    drop.buckets = {"Test_AttributeBucket", "Test_GeneratedBucket", "Test_GemBucket", "", ""};
    drop.canHavePerks = true;
    gamedata::PerkLayout rune;
    rune.fixed = {"PerkID_HeartgemRune_Detonate_Major", "PerkID_HeartgemRune_Detonate_Dominator", "", "", ""};
    rune.forceRarity = 4;
    tables.layouts = {drop, rune};
    tables.items = {{"testdropsword", 0}, {"testrune", 1}};
    tables.labels = {{"Attribute", {100.0F, {}}}, {"Proc", {100.0F, {}}}, {"Elemental", {100.0F, {{"Sword", 0.0F}}}},
                     {"Test_High", {100.0F, {}}}, {"Test_Old", {100.0F, {}}}, {"EmptyGem", {100.0F, {}}}};
    // The swaps: a weapon gets BaseDamage for the Soldier perk; jewelry gets ArmorSoldier, and an amulet then HP (a chain of
    // two steps); a later class row that a first row covers is not loaded; an ItemId row wins over the class rows.
    tables.swaps = {{1, false, "Weapon", "PerkID_Stat_TwoHandSoldier", "PerkID_StatSwap_BaseDamage"},
                    {2, false, "Jewelry", "PerkID_Stat_TwoHandSoldier", "PerkID_Stat_ArmorSoldier"},
                    {3, false, "EquippableAmulet+Jewelry", "PerkID_Stat_ArmorSoldier", "PerkID_StatSwap_HP"},
                    {4, false, "EquippableAmulet+Jewelry", "PerkID_Stat_TwoHandSoldier", "PerkID_Test_Strength"},
                    {5, true, "TestAmuletSpecial", "PerkID_Stat_TwoHandSoldier", "PerkID_Test_Strength"}};
    tables.consumables = {
        {"TestPotion", {"PotionHealT5"}, {0.0F}, "SurvivalPotion", 25.0F, {}, {}, false, true},
        {"TestPotionB", {"PotionHealT5"}, {0.0F}, "SurvivalPotion", 25.0F, {}, {}, false, true},
        {"TestRegenPotion", {"GrandPotionRegen"}, {5.0F}, "RegenPotion", 30.0F, {}, {}, false, false},
        {"TestManaPotion", {"PotionMana"}, {0.0F}, "PotionMana", 15.0F, {}, {}, false, false},
        {"TestFood", {"FoodCONT5"}, {0.0F}, "Food", 1.0F, {}, {"Attributes"}, false, false},
        {"TestOldFood", {"FoodCONT4"}, {0.0F}, "Food", 1.0F, {}, {"Attributes"}, false, false},
        {"TestDust", {"GemstoneDust"}, {0.0F}, "AbsorbBoost", 100.0F, {}, {}, false, false},
        {"TestSiegePotion", {"PotionHealT5"}, {0.0F}, "SurvivalPotion", 25.0F, {}, {}, false, true},
    };
    tables.equipLoad = {500.0F, 26.0F, 46.0F, 100.0F, {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}};
    tables.itemLoads = {{"testchest", 199.6F}, {"testlegs", 31.6F}};
    tables.runes = {{"testrunerow", {"Ability_Rune_Detonate"}}};
    // Tier 3: 1 perk to the gear score 324, 2 perks above; channel 2. Tier 5: 3 to 499, 4 to 599, 5 at 600; channel 4.
    tables.tiers.resize(6);
    tables.tiers[3] = {2, {{1, {{0, 324}}}, {2, {{325, 700}}}}};
    tables.tiers[5] = {4, {{3, {{0, 499}}}, {4, {{500, 599}}}, {5, {{600, 1000}}}}};
    tables.minLevelRollPerks = 7;
    return tables;
}

/** The client of a case: its input reports with action bits, and its messages. */
struct Walker {
    ServerHost& host;
    FakeClient& client;
    state::WorldPosition position;
    std::uint16_t input{10};
    std::deque<InputEntry> history{};

    InputEntry entry(std::vector<unsigned> bits = {}) {
        InputEntry entry = report_entry(++input, position.x, position.y, position.z);
        entry.actionBits = std::move(bits);
        entry.hasLook = true;
        entry.yaw = 1.5708F;
        return entry;
    }
    void send(const InputEntry& entry) {
        history.push_front(entry);
        if (history.size() > 5) {
            history.pop_back();
        }
        host.clock.advance(kTick);
        host.datagram(client.peer, client.input_report(std::vector<InputEntry>(history.begin(), history.end())));
        host.tick();
        client.exchange(host);
    }
    void idle(int steps) {
        for (int index = 0; index < steps; ++index) {
            send(entry());
        }
    }
    void bits(std::vector<unsigned> bits) {
        send(entry(std::move(bits)));
    }
    void press(unsigned place) {
        bits({kAbilityBits[place], kVirtualBit});
        bits({kHoldBit});
        bits({kReleaseBit});
    }
};

/** The options of the game server of a case. `observe`: each session observes the other characters near it. */
inline service::RepServerOptions shop_options(bool observe) {
    service::RepServerOptions options;
    options.serverVersion = "test-version";
    options.observePlayers = observe;
    return options;
}

/** The world of a case: character A (with a client) near a place, B (enter_b) next to it. */
struct Shop {
    ServerHost host;
    FakeClient a{27100};
    FakeClient b{27101};
    Player alpha;
    Player beta;
    Walker walker{host, a, kPlace};

    explicit Shop(bool perks = true, bool cost = false, bool observe = false) : host(shop_options(observe)) {
        host.catalog.set(shop_catalog());
        host.progression.set(shop_progression());
        if (!host.creatures.set_full({wolf_slice()}, {wolf_kind()}, {}, {{1, kWolfHealth, 100.0F, 50.0F}})) {
            throw std::runtime_error("itemization test: the creature data is not valid");
        }
        if (!host.combat.set(shop_combat())) {
            throw std::runtime_error("itemization test: the combat tables are not valid");
        }
        if (!host.abilities.set(shop_abilities(cost))) {
            throw std::runtime_error("itemization test: the ability data is not valid: " + host.abilities.error());
        }
        if (perks && !host.perks.set(shop_perks())) {
            throw std::runtime_error("itemization test: the perk data is not valid: " + host.perks.error());
        }
        host.terrain.source = [](float, float) { return std::optional<float>(kHeight); };
        host.world.set_creature_behavior(false);
        alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kPlace);
        host.enter(a, alpha.registration());
        walker.idle(20);
    }
    world::World& world() {
        return host.world;
    }
    world::Entity* character() {
        return host.world.find_character(kCharacterA);
    }
    world::Entity* wolf(float east, float north = 0.0F) {
        world::Entity* entity = host.world.creatures().spawn("Test_Wolf", at(kPlace.x + east, kPlace.y + north), 90.0F).entity;
        walker.idle(5);
        return entity;
    }
    /** Gives A an item with perks in the bag and puts it into a paperdoll slot. @return The result of the equip. */
    world::ItemResult wear(const char* item, std::uint32_t slot, world::PerkIds perks = {}, std::uint32_t gearScore = 100) {
        world::GiveOptions options;
        options.gearScore = gearScore;
        options.perks = perks;
        const world::ItemResult given = host.world.inventory().give(kCharacterA, item, 1, options);
        if (!given.ok() || given.slots.empty()) {
            return given;
        }
        return host.world.inventory().equip(kCharacterA, given.slots.front(), slot);
    }
    void enter_b() {
        beta = add_player(host.store, "76561202255233019", "Tester B", kCharacterB, "Beta", at(kPlace.x - 1.0F, kPlace.y + 1.0F));
        host.enter(b, beta.registration());
        for (int index = 0; index < 20; ++index) {
            host.clock.advance(kTick);
            host.datagram(b.peer, b.report_at(static_cast<std::uint16_t>(100 + index), kPlace.x - 1.0F, kPlace.y + 1.0F, kHeight));
            host.tick();
            b.exchange(host);
            a.exchange(host);
        }
    }
    void pass(float seconds) {
        walker.idle(static_cast<int>(std::lround(seconds * 30.0F)));
    }
    /** One strike of A on a creature with a row of the weapon in the hand. */
    world::HitResult strike(world::Entity& target, const char* row, const char* item = "TestHammer") {
        world::Entity& own = *character();
        const gamedata::WeaponRow* weapon = host.combat.weapon_of(item);
        const world::ItemInstance* instance = world::Combat::weapon_in_hand(own);
        const world::HitWeapon use{item, world::Combat::weapon_slot_in_hand(own).value_or(0), weapon,
                                   instance != nullptr ? instance->gearScore : 100U, {}};
        return host.world.combat().strike(own, target, use, *host.combat.row_index(row), 1.0F, "a test hit");
    }
};

/** @return The fragments of one slot and type that a client got from the message `from` on. */
inline std::vector<Fragment> fragments_of(const FakeClient& client, std::uint32_t slot, std::uint32_t type, std::size_t from = 0) {
    std::vector<Fragment> found;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        for (const Record& record : read_bundle(client.got[index].body).records) {
            for (const Fragment& fragment : record.fragments) {
                if (fragment.slot == slot && fragment.type == type) {
                    found.push_back(fragment);
                }
            }
        }
    }
    return found;
}

/** @return The damage entries (type, amount) of each damage number that a client got from the message `from` on. */
inline std::vector<std::vector<std::pair<std::uint8_t, float>>> damage_entries(const FakeClient& client, std::size_t from = 0) {
    std::vector<std::vector<std::pair<std::uint8_t, float>>> numbers;
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
        reader.u8();
        reader.u8();
        const std::uint32_t count = reader.size();
        std::vector<std::pair<std::uint8_t, float>> entries;
        for (std::uint32_t entry = 0; entry < count && reader.ok(); ++entry) {
            const std::uint8_t type = reader.u8();
            const float amount = reader.f32();
            reader.f32();
            entries.emplace_back(type, amount);
        }
        numbers.push_back(entries);
    }
    return numbers;
}

inline std::size_t perk_index(const ServerHost& host, const char* id) {
    const gamedata::PerkRow* row = host.perks.find(id);
    return row != nullptr ? row->index : 0;
}

/** The slots of the visible group (field 1, 3 of group 2) and of the other group (field 3 of group 1) of a paperdoll part. */
struct PaperdollPart {
    std::vector<std::uint32_t> visible;
    std::vector<std::uint32_t> other;
    bool local{false};
};

inline PaperdollPart paperdoll_part(const Bytes& state) {
    Fragment fragment;
    fragment.type = 3183;
    std::size_t position = 0;
    PaperdollPart part;
    if (!detail::read_state(state, position, fragment)) {
        return part;
    }
    const auto slots = [&](std::pair<unsigned, unsigned> key) {
        std::vector<std::uint32_t> list;
        const auto found = fragment.fields.find(key);
        if (found != fragment.fields.end()) {
            server::hub::Reader reader(found->second);
            const auto count = progress_wire::read_full_form(reader);
            for (std::uint32_t index = 0; count && index < *count; ++index) {
                list.push_back(reader.u32());
            }
        }
        return list;
    };
    part.visible = slots({2, 3});
    part.other = slots({1, 3});
    part.local = fragment.fields.count({1, 6}) != 0;
    return part;
}

} // namespace soulwarden::backend::tests::shop

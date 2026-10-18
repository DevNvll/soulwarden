#pragma once

// The world of the cases of stage 3.7c (tests/procedural_tests.cpp): the procedural results (the craft gear score, the perks of the
// generator with the inputs of a craft), the perk items, the Azoth, the currency ingredients, and the craft bonus of the statuses. The data
// of refining (format 2, crafting_fixture.h) with the members of format 3, made-up items, perks, buckets, and statuses. All ids, keys, items,
// and values are made up.

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "gamedata/ability_data.h"
#include "gamedata/perk_data.h"
#include "gamedata/progression_tables.h"
#include "support/crafting_fixture.h"
#include "support/shop_fixture.h"

namespace soulwarden::backend::tests::procedural {

using namespace crafting;

/** The items of stage 3.7c: the outputs (a tool of tier 2, a great axe and a bag of tier 5, a crown with an override), the materials, the resources. */
inline std::vector<gamedata::ItemDefinition> procedural_items() {
    using chests::chest_item;
    std::vector<gamedata::ItemDefinition> items = refine_items();
    items.push_back(chest_item("TestSkinKnifeT2", "Weapon", "EquippableTool+SkinningKnife", 2, 1, 200, 300));
    items.push_back(chest_item("TestGreatAxeT5", "Weapon", "Weapon+EquippableTwoHand+2HAxe+Melee", 5, 1, 500, 600));
    items.push_back(chest_item("TestBagT5", "Bag", "Bag", 5, 1, 500, 600));
    gamedata::ItemDefinition crown = chest_item("TestCrown", "Armor", "EquippableHead+Armor", 5, 1, 600, 600);
    crown.gearScoreOverride = 600;
    items.push_back(crown);
    items.push_back(chest_item("TestEngineChest", "Armor", "EquippableChest+Armor", 2, 1, 200, 300));
    for (const auto& [id, tier] : std::vector<std::pair<const char*, std::uint32_t>>{
             {"TestMetal2", 2}, {"TestMetal2Fae", 2}, {"TestTimber2", 2}, {"TestTimber3", 3}, {"TestHide2", 2}, {"TestMetal5", 5},
             {"TestTimber5", 5}, {"TestHide5", 5}, {"TestToolCharm", 1}, {"TestAxeCharm", 5}, {"TestGemCut", 4}, {"TestPins", 1},
             {"TestAttrCharm", 5}, {"TestSwordCharm", 2}, {"TestBagCharm", 1}, {"TestNoBucket", 1}}) {
        items.push_back(chest_item(id, "Resource", "Resource", tier, 1000));
    }
    return items;
}

/** One procedural recipe of the made-up data (format 3) at the station Blacksmith5 (the key blacksmith2). */
inline nlohmann::json procedural_json(const std::string& id, nlohmann::json ingredients, const std::string& item, const std::string& skill,
                                      std::uint32_t level, std::uint32_t baseGearScore, std::uint32_t baseTier, bool push = true,
                                      std::uint32_t maxCraftGS = 0) {
    nlohmann::json recipe = recipe_json(id, {"blacksmith2"}, std::move(ingredients), item, 1, 25, {}, skill, level);
    recipe["procedural"] = true;
    recipe["baseGearScore"] = baseGearScore;
    recipe["baseTier"] = baseTier;
    recipe["perkItems"] = 1;
    recipe["push"] = push;
    recipe["perkCost"] = 15;
    recipe["maxCraftGS"] = maxCraftGS;
    return recipe;
}

/**
 * The made-up crafting data of format 3: the data of refine_document and the procedural recipes TestKnifeT2Recipe (7 TestMetal2, 2 TestTimber,
 * 1 TestHide2: TestSkinKnifeT2; Engineering 0; BaseGearScore 200), TestKnifeCapRecipe (the same with MaxCraftGS 207), TestAxeT5Recipe
 * (Weaponsmithing 150: TestGreatAxeT5, 500), TestBagT5Recipe (push false: TestBagT5, 500), TestCrownRecipe (400: the override 600 of the
 * crown), the currency recipe TestAzothRecipe (1 TestOre and 250 Azoth_Currency), the rank gear score of Engineering (T2: 5 below rank 50,
 * 55 below 100, else 75) and of Weaponsmithing (T5: 5 from 150, 25 from 200), the gear score of the materials (TestMetal2Fae 5, each T5
 * material 20), the resources, the cap 2,500 of Azoth, and the list Crafting of the perk generation data with the modifiers 1.
 */
inline std::string procedural_document() {
    using nlohmann::json;
    json document = json::parse(refine_document());
    document["format"] = 3;
    for (json& recipe : document["recipes"]) {
        recipe["maxCraftGS"] = 0;
    }
    document["categories"]["testmetal2"] = {"TestMetal2", "TestMetal2Fae"};
    document["categories"]["testtimber"] = {"TestTimber2", "TestTimber3"};
    document["categories"]["testhide2"] = {"TestHide2"};
    document["categories"]["testmetal5"] = {"TestMetal5"};
    document["categories"]["testtimber5"] = {"TestTimber5"};
    document["categories"]["testhide5"] = {"TestHide5"};
    const json knife = json::array({json::array({"Category_Only", "TestMetal2", 7}), json::array({"Category_Only", "TestTimber", 2}),
                                    json::array({"Category_Only", "TestHide2", 1})});
    const json axe = json::array({json::array({"Category_Only", "TestMetal5", 15}), json::array({"Category_Only", "TestTimber5", 3}),
                                  json::array({"Category_Only", "TestHide5", 2})});
    document["recipes"].push_back(procedural_json("TestKnifeT2Recipe", knife, "TestSkinKnifeT2", "Engineering", 0, 200, 2));
    document["recipes"].push_back(procedural_json("TestKnifeCapRecipe", knife, "TestSkinKnifeT2", "Engineering", 0, 200, 2, true, 207));
    document["recipes"].push_back(procedural_json("TestAxeT5Recipe", axe, "TestGreatAxeT5", "Weaponsmithing", 150, 500, 5));
    document["recipes"].push_back(procedural_json("TestBagT5Recipe", json::array({json::array({"Category_Only", "TestHide5", 4})}), "TestBagT5",
                                                  "Armoring", 0, 500, 5, false));
    document["recipes"].push_back(procedural_json("TestCrownRecipe", json::array({json::array({"Category_Only", "TestMetal5", 1})}), "TestCrown",
                                                  "Armoring", 0, 400, 5));
    json azoth = recipe_json("TestAzothRecipe", {"blacksmith2"},
                             json::array({json::array({"Item", "TestOre", 1}), json::array({"Currency", "Azoth_Currency", 250})}), "TestRefined", 1, 0,
                             {}, "Smelting", 0);
    azoth["maxCraftGS"] = 0;
    document["recipes"].push_back(azoth);
    json engineering = json::array();
    json weaponsmithing = json::array();
    json armoring = json::array();
    for (int rank = 0; rank <= 250; ++rank) {
        engineering.push_back({0, rank < 50 ? 5 : rank < 100 ? 55 : 75, 0, 0, 0});
        weaponsmithing.push_back({0, 75, 75, 75, rank >= 200 ? 25 : rank >= 150 ? 5 : 0});
        armoring.push_back({0, 0, 0, 0, 0});
    }
    document["rankGearScore"] = {{"Engineering", engineering}, {"Weaponsmithing", weaponsmithing}, {"Armoring", armoring}};
    document["materials"] = {{"TestRareOre", {0, 10, 5, 0, 0}}, {"TestOreTop", {1, 0, 0, 0, 0}}, {"TestMetal2Fae", {0, 10, 5, 0, 5}},
                             {"TestMetal5", {0, 0, 0, 20, 20}}, {"TestTimber5", {0, 0, 0, 20, 20}}, {"TestHide5", {0, 0, 0, 20, 20}}};
    document["currencies"] = {{"Azoth_Currency", {{"cap", 2500}}}};
    const auto resource = [](const char* bucket) { return json{{"bucket", bucket}, {"gemPerk", ""}, {"socket", ""}}; };
    document["resources"] = {{"TestToolCharm", resource("Test_ToolCharmBucket")}, {"TestAxeCharm", resource("Test_AxeCharmBucket")},
                             {"TestGemCut", resource("Test_GemCutBucket")},       {"TestPins", resource("Test_PinsBucket")},
                             {"TestAttrCharm", resource("Test_AttrCharmBucket")}, {"TestSwordCharm", resource("Test_SwordCharmBucket")},
                             {"TestBagCharm", resource("Test_BagCharmBucket")},   {"TestNoBucket", resource("Test_MissingBucket")}};
    document["perkGeneration"] = {
        {"tiers", json::array({json::array(), json::array(),
                               json::array({json::array({1, json::array({json::array({0, 169})})}), json::array({2, json::array({json::array({170, 700})})})}),
                               json::array({json::array({1, json::array({json::array({0, 269})})}), json::array({2, json::array({json::array({270, 324})})}),
                                            json::array({3, json::array({json::array({325, 700})})})}),
                               json::array({json::array({2, json::array({json::array({0, 369})})}), json::array({3, json::array({json::array({370, 449})})}),
                                            json::array({4, json::array({json::array({450, 700})})})}),
                               json::array({json::array({3, json::array({json::array({0, 469})})}),
                                            json::array({4, json::array({json::array({470, 599}), json::array({626, 674})})}),
                                            json::array({5, json::array({json::array({600, 625}), json::array({675, 1000})})})})})},
        {"chanceModifiers", {1.0, 1.0, 1.0}}};
    return document.dump();
}

/** One bucket of the made-up perk data. */
inline gamedata::PerkBucket bucket(const char* id, const char* type, float chance, std::vector<gamedata::BucketEntry> entries) {
    gamedata::PerkBucket value;
    value.id = id;
    value.type = type;
    value.chance = chance;
    value.entries = std::move(entries);
    return value;
}

/**
 * The perk data of stage 3.7c: the data of the itemization cases (shop_perks: the Soldier perk and its swap to BaseDamage on a weapon, the
 * gems) and the perks, buckets, and layouts of the outputs: the knife (one Generated bucket of 0.15), the great axe (Inherent 0.9, Generated
 * 0.5, Gem 0.6, Generated 0.35, Generated 0.45), the bag (four Generated buckets of 0.5), the crown (one Generated bucket), and the buckets of
 * the resources (each one perk, chance 1). Tier 2: channel 2; tier 5: channel 4.
 */
inline gamedata::PerkTables procedural_perks() {
    gamedata::PerkTables tables = shop::shop_perks();
    using shop::perk;
    for (const auto& [id, itemClass, labels] : std::vector<std::tuple<const char*, const char*, const char*>>{
             {"PerkID_TestTool_XP", "EquippableTool", "ToolXP"},     {"PerkID_TestTool_Luck", "EquippableTool", "ToolLuck"},
             {"PerkID_TestCraft_A", "EquippableTwoHand", "TA"},      {"PerkID_TestCraft_B", "EquippableTwoHand", "TB"},
             {"PerkID_TestCraft_C", "EquippableTwoHand", "TC"},      {"PerkID_TestCraft_D", "EquippableTwoHand", "TD"},
             {"PerkID_TestSword_Only", "Sword", "SwordOnly"},         {"PerkID_TestBag_A", "Bag", "BA"},
             {"PerkID_TestBag_B", "Bag", "BB"},                      {"PerkID_TestBag_C", "Bag", "BC"},
             {"PerkID_TestBag_D", "Bag", "BD"},                      {"PerkID_TestBag_E", "Bag", "BE"}}) {
        tables.perks.push_back(perk(id, "Generated", "OnEquip", itemClass, labels, {}, {}));
    }
    tables.perks.push_back(perk("PerkID_TestEngineer", "Generated", "OnEquip", "Armor", "Engineer", {"Test_Affix_Engineer"}, {}));
    tables.affixes["test_affix_engineer"] = shop::cells({}, {{"StatusEffect", "Status_Test_Engineer"}});
    tables.buckets.push_back(bucket("Test_ToolBucket", "Generated", 0.15F, {{"PerkID_TestTool_XP", false, 1.0F}, {"PerkID_TestTool_Luck", false, 1.0F}}));
    tables.buckets.push_back(bucket("Test_ToolCharmBucket", "Generated", 1.0F, {{"PerkID_TestTool_XP", false, 100.0F}}));
    tables.buckets.push_back(bucket("Test_CraftAttr", "Inherent", 0.9F, {{"PerkID_Stat_TwoHandSoldier", false, 30.0F}}));
    tables.buckets.push_back(bucket("Test_Craft3", "Generated", 0.5F, {{"PerkID_TestCraft_A", false, 1.0F}}));
    tables.buckets.push_back(bucket("Test_CraftGem", "Gem", 0.6F, {{"PerkID_Gem_EmptyGemSlot", false, 1.0F}}));
    tables.buckets.push_back(bucket("Test_Craft4", "Generated", 0.35F, {{"PerkID_TestCraft_B", false, 1.0F}}));
    tables.buckets.push_back(bucket("Test_Craft5", "Generated", 0.45F, {{"PerkID_TestCraft_C", false, 1.0F}}));
    tables.buckets.push_back(bucket("Test_AxeCharmBucket", "Generated", 1.0F, {{"PerkID_TestCraft_D", false, 100.0F}}));
    tables.buckets.push_back(bucket("Test_GemCutBucket", "Gem", 1.0F, {{"PerkID_Gem_VoidDMG4", false, 50.0F}}));
    tables.buckets.push_back(bucket("Test_PinsBucket", "Gem", 1.0F, {{"PerkID_Gem_EmptyGemSlot", false, 140.0F}}));
    tables.buckets.push_back(bucket("Test_AttrCharmBucket", "Inherent", 1.0F, {{"PerkID_Stat_TwoHandSoldier", false, 1.0F}}));
    tables.buckets.push_back(bucket("Test_SwordCharmBucket", "Generated", 1.0F, {{"PerkID_TestSword_Only", false, 1.0F}}));
    tables.buckets.push_back(bucket("Test_BagA", "Generated", 0.5F, {{"PerkID_TestBag_A", false, 1.0F}}));
    tables.buckets.push_back(bucket("Test_BagB", "Generated", 0.5F, {{"PerkID_TestBag_B", false, 1.0F}}));
    tables.buckets.push_back(bucket("Test_BagC", "Generated", 0.5F, {{"PerkID_TestBag_C", false, 1.0F}}));
    tables.buckets.push_back(bucket("Test_BagD", "Generated", 0.5F, {{"PerkID_TestBag_D", false, 1.0F}}));
    tables.buckets.push_back(bucket("Test_BagCharmBucket", "Generated", 1.0F, {{"PerkID_TestBag_E", false, 1.0F}}));
    const auto layout = [&](const char* id, std::array<std::string, gamedata::kPerkPlaces> buckets) {
        gamedata::PerkLayout value;
        value.buckets = buckets;
        value.slots = buckets;
        value.canHavePerks = true;
        tables.items[id] = tables.layouts.size();
        tables.layouts.push_back(value);
    };
    layout("testskinknifet2", {"Test_ToolBucket", "", "", "", ""});
    layout("testgreataxet5", {"Test_CraftAttr", "Test_Craft3", "Test_CraftGem", "Test_Craft4", "Test_Craft5"});
    layout("testbagt5", {"Test_BagA", "Test_BagB", "Test_BagC", "Test_BagD", ""});
    layout("testcrown", {"Test_Craft3", "", "", "", ""});
    tables.tiers.resize(6);
    tables.tiers[2] = {2, {{1, {{0, 169}}}, {2, {{170, 700}}}}};
    tables.tiers[5].maxChannel = 4;
    return tables;
}

/** The ability data of stage 3.7c: two foods of Engineering (5 and 60), the status of the perk of the test chest (2), and a trophy (5, 3 stacks). */
inline gamedata::AbilityTables procedural_abilities() {
    gamedata::AbilityTables tables;
    // The loader needs one cooldown row (a made-up ability).
    tables.cooldowns = {{"Ability_Test_Craft", 10.0F, 0.0F}};
    tables.statusEffects = shop::numbered({
        shop::status("FoodTestEngineer", 30.0F, shop::cells({{"MGSEngineering", 5.0F}, {"MaxGSEngineering", 5.0F}},
                                                            {{"EffectCategories", "TradeSkill+FoodUtility+Food"}})),
        shop::status("FoodTestEngineerBig", 30.0F, shop::cells({{"MaxGSEngineering", 60.0F}}, {{"EffectCategories", "TradeSkill+Food"}})),
        shop::status("Status_Test_Engineer", -1.0F, shop::cells({{"MGSEngineering", 2.0F}, {"MaxGSEngineering", 2.0F}, {"StackMax", 10.0F}})),
        shop::status("TestTrophyEngineer", -1.0F, shop::cells({{"MaxGSEngineering", 5.0F}, {"MaxGSWeaponsmithing", 5.0F}, {"StackMax", 3.0F}})),
    });
    return tables;
}

/** The progression tables of stage 3.7c: Smelting and the trade skills Engineering, Weaponsmithing, Armoring (ranks 0 to 250), no expansion. */
inline gamedata::ProgressionData procedural_progression(bool expansion = false) {
    gamedata::ProgressionData data = refine_progression();
    gamedata::RankTable ranks;
    for (std::uint32_t rank = 0; rank <= 250; ++rank) {
        ranks.experience.push_back(100);
    }
    for (const char* skill : {"Engineering", "Weaponsmithing", "Armoring"}) {
        data.progressions.push_back({skill, 0, gamedata::ProgressionKind::tradeSkill, 250, ranks});
    }
    if (expansion) {
        // The row of no expansion with a made-up MaxCraftGS of 590 (the cap of a craft with no MaxCraftGS of the item).
        data.expansions.push_back({"None", 60, 590, 625, 250, ""});
    }
    return data;
}

/** A crafting host with the data of stage 3.7c (format 3), its items, its perk data, its statuses, and the trade skills. */
struct Procedural : Refiner {
    explicit Procedural(bool withB = true, const std::string& database = ":memory:", bool existing = false, bool expansion = false)
        : Refiner(withB, database, existing) {
        host.catalog.set(procedural_items());
        host.progression.set(procedural_progression(expansion));
        if (!host.progression.loaded()) {
            throw std::runtime_error("procedural test: the progression tables were not set");
        }
        if (!host.perks.set(procedural_perks())) {
            throw std::runtime_error("procedural test: the perk data was not set: " + host.perks.error());
        }
        if (!host.abilities.set(procedural_abilities())) {
            throw std::runtime_error("procedural test: the ability data was not set: " + host.abilities.error());
        }
        if (!host.crafting.set(procedural_document())) {
            throw std::runtime_error("procedural test: the crafting data was not set: " + host.crafting.error());
        }
    }
    void rank(const char* character, const char* skill, std::uint32_t value) {
        if (!host.world.skills().set_rank(character, skill, gamedata::ProgressionKind::tradeSkill, value).ok()) {
            throw std::runtime_error(std::string("procedural test: the rank of ") + skill);
        }
    }
    void azoth(const char* character, std::uint64_t amount) {
        if (!host.world.skills().set_balance(character, world::kAzothCurrency, amount, 2500).ok()) {
            throw std::runtime_error("procedural test: the Azoth balance");
        }
    }
    std::uint64_t balance(const char* character) {
        const std::optional<world::SkillView> view = host.world.skills().view(character);
        return view ? world::Skills::balance(view->skills, world::kAzothCurrency) : 0;
    }
    /** @return The first instance of an item in the bag of a character, or null. */
    const world::ItemInstance* item(const char* character, const char* id) {
        world::Entity* entity = this->entity(character);
        if (entity == nullptr) {
            return nullptr;
        }
        for (const world::BagSlot& slot : entity->bag.slots) {
            if (slot.item && slot.item->itemId == id) {
                return &*slot.item;
            }
        }
        return nullptr;
    }
    /** The knife materials of one job and a request for it (the entries of the selected members). */
    void give_knife(const char* character, std::uint32_t jobs, const char* metal = "TestMetal2") {
        give(character, metal, 7 * jobs);
        give(character, "TestTimber2", 2 * jobs);
        give(character, "TestHide2", jobs);
    }
    void give_axe(const char* character, std::uint32_t jobs) {
        give(character, "TestMetal5", 15 * jobs);
        give(character, "TestTimber5", 3 * jobs);
        give(character, "TestHide5", 2 * jobs);
    }
};

inline std::vector<Entry> knife_entries(const char* metal = "TestMetal2", const char* timber = "TestTimber2") {
    return {entry(metal), entry(timber), entry("TestHide2")};
}

inline std::vector<Entry> axe_entries() {
    return {entry("TestMetal5"), entry("TestTimber5"), entry("TestHide5")};
}

} // namespace soulwarden::backend::tests::procedural

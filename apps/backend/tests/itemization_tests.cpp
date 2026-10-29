// Cases for the itemization (stage 1.3b): the perk data and the swap rule of the client, the
// perks of an item instance (the rows, the wire, the commands), the perk roll of a loot item, the
// values of the gear (attributes, health, armor, absorption, equip load, damage), the perks of a
// hit (a rend, a heal, a burst, a shorter cooldown, a damage over time, a gem conversion), the
// heart rune, the consumables, the cost of an ability (I14), the cooldown of the client (I8), two
// sessions, and the load. All ids, names, and values are made up, with the id texts of the game
// where a rule of the backend names them: no value of a capture is here.

#include <chrono>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "server/http/character_commands.h"
#include "server/rep/client_input.h"
#include "server/rep/combat_messages.h"
#include "server/rep/item_requests.h"
#include "support/cases.h"
#include "support/check.h"
#include "server/rep/states/state_codecs.h"
#include "support/item_decoder.h"
#include "support/progress_decoder.h"
#include "support/server_host.h"
#include "support/state_decoder.h"
#include "support/test_players.h"
#include "support/test_progression.h"
#include "world/perk_roll.h"

#include "support/shop_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace shop;

/** IZ1: the perk data: the file of the tool, the setter, the swap rule of the client, the potency, the perk counts, the classes. */
void perk_data(const Options& options) {
    const auto path = options.workDirectory / "perk-data.json";
    std::filesystem::create_directories(options.workDirectory);
    const json good = {
        {"format", 2},
        {"count", 3},
        {"tables", json::array()},
        {"perks", {{"PerkID_A", "Generated", "OnEquip", "Weapon", "", "LabelA", {{0, 0.002}, {600, 0}}, {"Affix_A"}, json::array(), 1, false},
                   {"PerkID_B", "Gem", "OnEquip", "", "", "", json::array(), json::array(), {"Ability_B"}, 2, false}}},
        {"affixes", {{"Affix_A", {{"MODStrength", 5}}}}},
        {"abilities", {{"Ability_B", {{"OnHit", 1}, {"AttackType", "Light"}}}, {"Ability_Rune_Detonate", json::object()}}},
        {"buckets", {{"Bucket_A", {"Generated", 0.5, json::array({json::array({"PerkID_A", 1}), json::array({"[PBID]Bucket_B", 2})}), false, false}},
                     {"Bucket_B", {"Gem", 1.0, json::array({json::array({"PerkID_B", 1})}), true, true}}}},
        {"layouts", {{{"", "", "", "", ""}, {"PerkID_B", "", "", "", ""}, {"Bucket_A", "", "", "", ""}, -1, true}}},
        {"items", {{"testitem", 0}}},
        {"consumables", {{"testpotion", {{"id", "TestPotion"}, {"effects", {"PotionHealT5"}}, {"durations", {0}}, {"cooldown", "SurvivalPotion"},
                                          {"cooldownTime", 25}, {"removeEffects", json::array()}, {"removeCategories", json::array()},
                                          {"noCombat", false}, {"shared", true}, {"level", 55}}}}},
        {"encumbrance", {{"equipLoadMax", 500}, {"ratioMedium", 26}, {"ratioHeavy", 46}, {"ratioOverburdened", 100}, {"damageLight", 0.15},
                         {"damageMedium", 0}, {"damageHeavy", -0.15}, {"healLight", 1.3}, {"healMedium", 1}, {"healHeavy", 0.7}}},
        {"equipLoad", {{"testchest", 200}}},
        {"labels", {{"LabelA", {100, {{"Sword", 5}}}}}},
        {"swaps", {{1, "ItemClass", "Weapon", "PerkID_A", "PerkID_B"}}},
        {"runes", {{"runedetonatet1", {"Ability_Rune_Detonate"}}}},
        {"generation", {{"minLevelRollPerks", 7}, {"tiers", {{2, {{1, {{0, 169}}}, {2, {{170, 700}}}}}}}}}};
    const auto load = [&](const json& document) {
        std::ofstream(path, std::ios::binary | std::ios::trunc) << document.dump();
        gamedata::PerkData data;
        const bool loaded = data.load(path.string());
        return std::make_pair(loaded, std::move(data));
    };
    {
        auto [loaded, data] = load(good);
        check(loaded && data.list_size() == 3 && data.find("perkid_a")->index == 1 && data.by_index(2)->id == "PerkID_B"
                  && data.affix("AFFIX_A")->number("MODStrength") == 5.0F && data.ability("ability_b")->text("AttackType") == "Light"
                  && data.bucket("bucket_a")->entries[1].bucket && data.layout_of("TestItem")->fixed[0] == "PerkID_B"
                  && data.consumable("TESTPOTION")->shared && data.consumable("TESTPOTION")->level == 55 && data.item_load("TestChest") == 200.0F && data.tables().equipLoad.heal[2] == 0.7F
                  && data.rune_abilities("RuneDetonateT1").size() == 1 && data.max_perks(0, 200) == 2,
              "the file of the tool loads: the perks in the sequence of the list (index 1 and 2), the cells, the buckets, the "
              "layouts, the consumables, the equip load, the heart runes, and the perk counts; the lookups ignore the case");
    }
    json bad = good;
    bad["buckets"]["Bucket_A"][2][0][0] = "PerkID_Nothing";
    check(!load(bad).first, "a bucket that names a perk that the file does not have: the file is refused as a whole");
    bad = good;
    bad["perks"][0].erase(10);
    check(!load(bad).first, "a perk with ten values (the tool writes eleven): refused");
    bad = good;
    bad["count"] = 4;
    check(!load(bad).first, "a count that is not the length of the list plus 1: refused");
    bad = good;
    bad["consumables"]["testpotion"].erase("shared");
    check(!load(bad).first, "a consumable with no shared mark: refused");
    check(!gamedata::PerkData{}.load((options.workDirectory / "no-such-file.json").string()), "no file: no data (the backend works with no perk)");

    gamedata::PerkData data;
    check(data.set(shop_perks()), "the perk data of the cases is complete");
    const auto swap = [&](world::PerkIds perks, const char* item, const char* itemClass) {
        data.apply_swaps(perks, item, itemClass);
        return perks;
    };
    check(swap({"PerkID_Stat_TwoHandSoldier"}, "TestAxe", "Weapon+EquippableTwoHand+2HAxe+Melee")[0] == "PerkID_StatSwap_BaseDamage",
          "the swap of a class row: a weapon has the class Weapon, and the Soldier perk becomes BaseDamage");
    check(swap({"PerkID_Stat_TwoHandSoldier"}, "TestAmulet", "EquippableAmulet+Jewelry")[0] == "PerkID_StatSwap_HP",
          "the first class row of the sequence wins (row 2 Jewelry, not row 4), and the next pass swaps the new perk (row 3): a "
          "chain of two steps, as the passes of the client");
    check(swap({"PerkID_Stat_TwoHandSoldier"}, "TestAmuletSpecial", "EquippableAmulet+Jewelry")[0] == "PerkID_Test_Strength",
          "an ItemId row wins over the class rows");
    check(swap({"PerkID_Stat_TwoHandSoldier", "PerkID_StatSwap_BaseDamage"}, "TestAxe", "Weapon+EquippableTwoHand")
                  == world::PerkIds{"PerkID_StatSwap_BaseDamage", "", "", "", ""},
          "a perk whose new perk the item has already goes out of its place, and the other perks move forward");
    check(swap({"PerkID_Gem_VoidDMG4", "PerkID_Weapon_CDRBasic"}, "TestAxe", "Weapon+EquippableTwoHand")
                  == world::PerkIds{"PerkID_Gem_VoidDMG4", "PerkID_Weapon_CDRBasic", "", "", ""},
          "a perk with no swap row does not change");
    const gamedata::PerkRow& scaled = *data.find("PerkID_Test_Strength");
    check(near(gamedata::PerkData::potency(scaled, 100), 1.0F) && near(gamedata::PerkData::potency(scaled, 350), 1.5F)
              && near(gamedata::PerkData::potency(scaled, 600), 2.0F) && near(gamedata::PerkData::potency(scaled, 700), 2.0F),
          "the potency: 1 plus the slope times the covered gear score from 100 (1, 1.5, 2 at 100, 350, 600; no slope above 600)");
    check(data.max_perks(5, 450) == 3 && data.max_perks(5, 550) == 4 && data.max_perks(5, 600) == 5 && data.max_perks(3, 300) == 1
              && data.max_perks(3, 0) == 0,
          "the largest perk count of a tier at a gear score (the ranges of the perk generation data)");
    check(gamedata::PerkData::allowed(*data.find("PerkID_Gem_VoidDMG4"), "Weapon+EquippableTwoHand")
              && !gamedata::PerkData::allowed(*data.find("PerkID_Gem_VoidDMG4"), "EquippableAmulet+Jewelry")
              && gamedata::PerkData::allowed(*data.find("PerkID_StatSwap_HP"), "EquippableAmulet+Jewelry"),
          "a perk is for an item with one class of its ItemClass (or with no ItemClass)");
}

/** IZ2: the instance: give with perks, the rows, the wire, the equip, the refusals, the swap at the give, the command. */
void instance(const Options&) {
    Shop shop;
    const std::size_t from = shop.a.got.size();
    world::GiveOptions options;
    options.gearScore = 600;
    options.perks = world::PerkIds{"PerkID_Stat_TwoHandSoldier", "PerkID_Weapon_CDRBasic", "PerkID_Gem_VoidDMG4", "", ""};
    const world::ItemResult given = shop.world().inventory().give(kCharacterA, "TestAxe", 1, options);
    shop.walker.idle(2);
    const std::vector<state::ItemRow> rows = shop.host.store.list_items(kCharacterA);
    const auto row = std::find_if(rows.begin(), rows.end(), [](const state::ItemRow& item) { return item.itemId == "TestAxe"; });
    check(given.ok() && row != rows.end() && row->gearScore == 600
              && row->perks == std::array<std::string, 5>{"PerkID_StatSwap_BaseDamage", "PerkID_Weapon_CDRBasic", "PerkID_Gem_VoidDMG4", "", ""},
          "the give: the row has the gear score and a column for each perk place, with the swap of the client (the Soldier perk of a "
          "weapon is stored as the BaseDamage perk)");
    bool wire = false;
    for (const Fragment& fragment : fragments_of(shop.a, 61, 1755, from)) {
        for (const auto& [element, item] : read_container(fragment.fields.at({0, 0})).elements) {
            wire = wire
                   || (item.gearScore == 600 && item.perks[0] == perk_index(shop.host, "PerkID_StatSwap_BaseDamage")
                       && item.perks[1] == perk_index(shop.host, "PerkID_Weapon_CDRBasic")
                       && item.perks[2] == perk_index(shop.host, "PerkID_Gem_VoidDMG4") && item.perks[3] == 0);
        }
    }
    check(wire, "the bag state of the owner has the index of the list of the client for each perk place");
    const world::ItemResult worn = shop.world().inventory().equip(kCharacterA, given.slots.front(), world::paperdoll::kMainHand1);
    const world::ItemInstance* axe = shop.character()->equipment.item(world::paperdoll::kMainHand1);
    check(worn.ok() && axe != nullptr && axe->perks[2] == "PerkID_Gem_VoidDMG4" && axe->perkIndex[2] == perk_index(shop.host, "PerkID_Gem_VoidDMG4"),
          "the equip keeps the perks and their indexes");
    const world::InstanceId instanceBefore = axe->instance;
    const world::ItemResult changed = shop.world().inventory().set_perks(kCharacterA, true, world::paperdoll::kMainHand1,
                                                                         {"PerkID_Weapon_Attunement_Void", "", "PerkID_Gem_VoidDMG4"});
    const world::ItemResult wrong = shop.world().inventory().set_perks(kCharacterA, true, world::paperdoll::kMainHand1, {"PerkID_Gem_SlashWard"});
    axe = shop.character()->equipment.item(world::paperdoll::kMainHand1);
    bool stored = false;
    for (const state::ItemRow& item : shop.host.store.list_items(kCharacterA)) {
        stored = stored || (item.itemId == "TestAxe" && item.container == state::kContainerPaperdoll && item.perks[0] == "PerkID_Weapon_Attunement_Void"
                      && item.perks[1].empty() && item.perks[2] == "PerkID_Gem_VoidDMG4");
    }
    check(changed.ok() && wrong.error == world::ItemError::badPerk && axe->instance == instanceBefore && axe->gearScore == 600
              && axe->perks[0] == "PerkID_Weapon_Attunement_Void" && axe->perks[1].empty() && axe->perks[2] == "PerkID_Gem_VoidDMG4" && stored,
          "the perk command of an item that the character has (Inventory::set_perks, the change of a character of the user): the "
          "perks change with the checks of a give (an empty part stays an empty place), the instance and the gear score stay, "
          "and a perk that is not for the item changes nothing");
    const auto refused = [&](world::PerkIds perks, const char* item = "TestAxe") {
        world::GiveOptions bad;
        bad.perks = perks;
        return shop.world().inventory().give(kCharacterA, item, 1, bad).error == world::ItemError::badPerk;
    };
    check(refused({"PerkID_Nothing"}) && refused({"PerkID_Gem_VoidDMG4", "PerkID_Gem_EmptyGemSlot"})
              && refused({"PerkID_Weapon_CDRBasic", "PerkID_Weapon_CDRBasic"}) && refused({"PerkID_Gem_VoidDMG4"}, "TestAmulet")
              && refused({"PerkID_Test_Deprecated"}),
          "a give is refused (and nothing changes) for a perk that the data does not have, two gems, one perk in two places, a "
          "perk that is not for the item class, and a deprecated perk");
    // The command of the development routes and of the admin routes.
    namespace commands = server::http::commands;
    const json values{{"item", "TestAmulet"}, {"quantity", 1}, {"gearScore", 500}, {"perks", "PerkID_Stat_TwoHandSoldier,,PerkID_Gem_SlashWard"}};
    const commands::JsonArguments arguments(values);
    service::HttpResponse refusal;
    const commands::WorldCommand step = commands::give_item(kCharacterA, arguments, refusal);
    const service::HttpResponse reply = step ? step(shop.world()) : refusal;
    bool amulet = false;
    for (const state::ItemRow& item : shop.host.store.list_items(kCharacterA)) {
        amulet = amulet || (item.itemId == "TestAmulet" && item.gearScore == 500 && item.perks[0] == "PerkID_StatSwap_HP"
                            && item.perks[1] == "PerkID_Gem_SlashWard" && item.perks[2].empty());
    }
    check(reply.status == 200 && amulet,
          "the give command takes the perks (an empty part is a place with no perk): the amulet gets HP for the Soldier perk (the "
          "chain of the swap) and the ward gem; the perks move forward over the empty place");

    // The first record of a full paperdoll with perks needs two parts: each group of lists stays whole in one part, and
    // each part has isLocalPlayer (live check I12: a second part of the other group took the items of the first one out).
    world::Entity full;
    world::ItemInstance item = *shop.character()->equipment.item(world::paperdoll::kMainHand1);
    item.index = 5310;
    item.perkIndex = {1300, 1301, 1302, 1303, 1304};
    for (const std::uint32_t slot : {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 14U, 15U, 16U, 17U, 24U, 25U, 45U}) {
        full.equipment.slots[slot].item = item;
    }
    const std::vector<Bytes> parts = server::rep::encode_paperdoll_state(full, server::rep::View::owner, server::rep::PartsRequest{0, 900});
    std::vector<PaperdollPart> decoded;
    for (const Bytes& part : parts) {
        decoded.push_back(paperdoll_part(part));
    }
    check(decoded.size() == 2 && decoded[0].other == std::vector<std::uint32_t>{5, 6, 7, 14, 15, 16, 17, 45} && decoded[0].visible.empty()
              && decoded[1].other.empty() && decoded[1].visible == std::vector<std::uint32_t>{0, 1, 2, 3, 4, 24, 25}
              && decoded[0].local && decoded[1].local,
          "a paperdoll of 15 items with perks: two parts, the slots that do not show on the body whole in the first part, the "
          "visible slots whole in the second part, isLocalPlayer in each part");
}

/** IZ3: the perk roll of a loot item: the level rule, the fixed perks, the count roll, the filters, one Inherent perk. */
void loot_roll(const Options&) {
    gamedata::PerkData data;
    (void)data.set(shop_perks());
    const gamedata::ItemDefinition sword = definition("TestDropSword", "Weapon", "Weapon+EquippableMainHand+Sword+Melee", 3, 300, 400);
    const gamedata::ItemDefinition rune = definition("TestRune", "Weapon", "HeartGem", 3, 625, 625);
    const auto fixed_random = [](float value) { return [value] { return value; }; };
    check(world::roll_perks(data, sword, 350, 6, fixed_random(0.0F)) == world::PerkIds{},
          "a character below the level 7 (MinLevelRollPerks) gets no rolled perk");
    check(world::roll_perks(data, rune, 625, 1, fixed_random(0.99F))
              == world::PerkIds{"PerkID_HeartgemRune_Detonate_Major", "PerkID_HeartgemRune_Detonate_Dominator", "", "", ""},
          "the fixed perks of the item come at each level");
    // Gear score 350, tier 3: two perks at most; the buckets get the chances 1 (attributes), 0.5 (generated), 0 (gem: the
    // count reached the target). A random number of 0.9 fails the chance 0.5: the count roll stops at the attribute bucket.
    check(world::roll_perks(data, sword, 350, 60, fixed_random(0.9F)) == world::PerkIds{"PerkID_Stat_TwoHandSoldier", "", "", "", ""},
          "the count roll from the last bucket: 0.9 is not below the chance 0.5 of the generated bucket; the attribute bucket rolls");
    // 0.1: the generated bucket rolls too. Its candidates: CDRBasic (3) and the nested bucket (Attunement 1); the channel 3
    // perk is above the channel 2 of tier 3, and the deprecated perk is out. The labels Proc and Elemental: the label roll
    // with 0.1 of the sum 100 (Elemental has the weight 0 for a sword) gives Proc.
    check(world::roll_perks(data, sword, 350, 60, fixed_random(0.1F))
              == world::PerkIds{"PerkID_Stat_TwoHandSoldier", "PerkID_Weapon_CDRBasic", "", "", ""},
          "two buckets roll: the channel filter, the deprecated filter, the label weights of the item class, and the weighted pick");
    check(world::roll_perks(data, sword, 320, 60, fixed_random(0.1F)) == world::PerkIds{"PerkID_Stat_TwoHandSoldier", "", "", "", ""},
          "at the gear score 320 the tier has one perk at most: one bucket gets a chance");
    unsigned inherent = 0;
    unsigned rolls = 0;
    std::uint64_t state = 7;
    const world::RandomUnit lcg = [&state] {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<float>(state >> 40) / static_cast<float>(1ULL << 24);
    };
    for (int roll = 0; roll < 2000; ++roll) {
        const world::PerkIds perks = world::roll_perks(data, sword, 350, 60, lcg);
        unsigned count = 0;
        for (const std::string& id : perks) {
            count += !id.empty() && data.find(id)->type == "Inherent" ? 1 : 0;
            rolls += !id.empty() ? 1 : 0;
        }
        inherent = std::max(inherent, count);
    }
    check(inherent == 1 && rolls > 2000, "2,000 rolls: one Inherent perk at most, and more than one perk in a part of the rolls");
}

/** IZ4: the gear values: the attribute bonus (and its state), the health, the armor and the absorption, the equip load, the damage. */
void gear_values(const Options&) {
    Shop shop;
    world::Entity& own = *shop.character();
    const float health = own.vitals.healthMax;
    const std::size_t from = shop.a.got.size();
    check(shop.wear("TestAmulet", world::paperdoll::kAmulet, {"PerkID_Test_Strength", "PerkID_Gem_SlashWard"}, 350).ok(),
          "setup: an amulet with a strength perk (10 at the potency 1.5) and a ward gem");
    shop.walker.idle(2);
    check(own.attributes.bonus[world::attribute::kStrength] == 15 && own.attributes.value(world::attribute::kStrength) == own.attributes.base(world::attribute::kStrength) + 15,
          "the attribute bonus of a perk: MODStrength 10 times the potency 1.5 at the gear score 350, a whole number");
    bool bonusField = false;
    for (const Fragment& fragment : fragments_of(shop.a, kAttributeSlot, 129, from)) {
        bonusField = bonusField || fragment.fields.count({0, 1}) != 0;
    }
    check(bonusField, "the attribute state of the owner has the bonus map (field 1; live check I4)");
    check(shop.wear("TestAmuletSpecial", world::paperdoll::kAmulet, {"PerkID_StatSwap_HP"}, 100).ok() && near(own.gear.healthPart, 0.1F)
              && near(own.vitals.healthMax, std::round(health * 1.1F), 1.0F) && own.attributes.bonus[world::attribute::kStrength] == 0,
          "a new amulet in the slot: the bonus of the old one goes away, and the HP perk adds 10 percent to the maximum health");
    // The armor and the equip load in whole tenths: a heavy chest (199.6 counts as 199 of 500: medium), the legs (31.6 counts
    // as 31: 230 of 500 is 46 percent, heavy; with the parts of a tenth it would be 231.2).
    check(shop.wear("TestChest", world::paperdoll::kChest).ok() && own.gear.loadClass == 1 && near(own.gear.equipLoad, 199.0F),
          "the equip load of the armor in whole tenths (live check I12: the client shows 231.57 as 22.9): 199.6 counts as 199, "
          "39.8 percent of 500 (medium)");
    check(shop.wear("TestLegs", world::paperdoll::kLegs).ok() && own.gear.loadClass == 2 && near(own.gear.equipLoad, 230.0F),
          "199 + 31 = 230 of 500 is 46 percent (heavy)");
    // The absorption of a ward gem: a bite of the slash type does 25 percent less.
    world::Entity* wolf = shop.wolf(2.0F);
    const gamedata::CreatureContact& bite = wolf->creature.body->attacks[0].contacts[0];
    const float without = shop.world().combat().creature_damage(*wolf, bite, own).dealt;
    check(shop.wear("TestAmulet", world::paperdoll::kAmulet, {"PerkID_Gem_SlashWard"}).ok(), "setup: the ward amulet");
    const float with = shop.world().combat().creature_damage(*wolf, bite, own).dealt;
    check(with > 0.0F && near(with, without * 0.75F, 0.5F), "ABSSlash 0.25 of a ward gem: the bite does 25 percent less damage");
    // The passive base damage: two perks of a stack limit 2 and a third one.
    check(shop.wear("TestHammer", world::paperdoll::kMainHand1, {"PerkID_StatSwap_BaseDamage"}).ok(), "setup: a hammer with the BaseDamage perk");
    const world::HitResult hit = shop.strike(*wolf, "Test_Light");
    check(near(own.gear.baseDamage, 0.1F) && near(hit.damage.raw, kWeaponDamage * 1.1F, 0.1F),
          "a passive BaseDamage perk: 10 percent more raw damage for each hit");
}

/** IZ5: the perks of a hit: a rend, a heal and a burst, a shorter cooldown, a damage over time, a chain, a conversion. */
void hit_perks(const Options&) {
    Shop shop;
    world::Entity& own = *shop.character();
    check(shop.wear("TestHammer", world::paperdoll::kMainHand1, {"PerkID_Ability_Hammer_Shockwave", "PerkID_Weapon_CDRBasic"}, 100).ok()
              && shop.wear("TestAxe", world::paperdoll::kMainHand1 + 1,
                           {"PerkID_Ability_GreatAxe_GravityWell", "PerkID_Weapon_Attunement_Void", "PerkID_Gem_VoidDMG4"}, 100)
                     .ok(),
          "setup: a hammer with the Shockwave perk and CDRBasic, an axe with the Gravity Well perk, the void attunement, and a gem");
    own.combat.selectedWeapon = world::paperdoll::kMainHand1;
    shop.world().gear().refresh(own);
    world::Entity* wolf = shop.wolf(2.0F);
    const world::EntityId wolfId = wolf->id;
    // The rend of Shockwave: the status on the creature, and 5 percent less mitigation for the next hit.
    const float before = shop.strike(*wolf, "Test_Light").damage.dealt;
    (void)shop.strike(*wolf, "Warhammer_Damage_Shockwave");
    wolf = shop.world().creatures().find(wolfId);
    const bool rend = std::any_of(wolf->creature.effects.begin(), wolf->creature.effects.end(),
                                  [](const world::CreatureEffect& effect) { return effect.status == "Status_Perk_Hammer_Shockwave_Rend" && near(effect.armor, -0.05F); });
    const float after = shop.strike(*wolf, "Test_Light").damage.dealt;
    check(rend && near(after, kWeaponDamage * (1.0F - 0.2F * 0.95F), 0.1F) && near(before, kWeaponDamage * 0.8F, 0.1F),
          "Sundering Shockwave: a hit of the row of Shockwave gives the rend (armor -0.05 at the potency 1), and the next hit "
          "takes 0.2 x 0.95 of the mitigation");
    // An OnActive perk of the axe does nothing while the hammer is in the hand.
    const std::uint64_t heals = shop.world().gear().counts().perkHeals;
    (void)shop.strike(*wolf, "GreatAxe_GravityWell_Burst");
    check(shop.world().gear().counts().perkHeals == heals, "a perk with OnActive of the weapon that is not in the hand has no effect");
    // Refreshing Move: a light hit takes 5 percent of the cooldown of each ability of the bar of the weapon in the hand.
    // (The cooldown of Shockwave of the press of a case of the abilities is not here: a timer of the system.)
    // The axe in the hand: the heal of the Gravity Well perk, once in its ActivationCooldown, and the burst of the perk.
    own.combat.selectedWeapon = world::paperdoll::kMainHand1 + 1;
    shop.world().gear().refresh(own);
    own.vitals.health = 100.0F;
    shop.walker.idle(1);
    const std::size_t from = shop.a.got.size();
    const world::HitResult burst = shop.strike(*wolf, "GreatAxe_GravityWell_Burst", "TestAxe");
    const float healed = own.vitals.health - 100.0F;
    (void)shop.strike(*wolf, "GreatAxe_GravityWell_Burst", "TestAxe");
    const float second = own.vitals.health - 100.0F;
    shop.walker.idle(2);
    check(burst.verdict == world::HitVerdict::accepted && near(healed, burst.damage.dealt * 0.15F, 0.5F) && near(second, healed, 0.01F),
          "Insatiable Gravity Well: a hit of the burst heals 15 percent of its damage (potency 1), one time in the ActivationCooldown");
    check(shop.world().abilities().counts().areaHits >= 1, "the burst of the perk (InsatiableGravityWell) hurts the creature near the character");
    // The gem: half of the damage of a hit of the axe is Corruption (the elemental mitigation 0.1), and the number has two entries.
    const auto numbers = damage_entries(shop.a, from);
    check(!numbers.empty() && numbers.front().size() == 2 && numbers.front()[1].first == 11
              && near(numbers.front()[0].second, burst.damage.raw * 0.5F * (1.0F - 0.2F * 0.95F), 0.1F)
              && near(numbers.front()[1].second, burst.damage.raw * 0.5F * (1.0F - 0.1F * 0.95F), 0.1F),
          "the gem conversion: 50 percent of the raw damage has the type Corruption with the elemental defense (with the rend of "
          "the creature); the damage number has two entries (Strike and Corruption)");
    // The void attunement: a damage over time of 10 percent of the raw damage of the hit, through the creature system.
    check(shop.world().gear().counts().tickHits >= 1, "the void attunement gives a damage over time (a tick with its damage type)");
    // A perk row with a condition that the backend does not evaluate has no effect.
    check(shop.wear("TestChest", world::paperdoll::kChest, {"PerkID_Test_Unknown_Condition"}).ok(), "setup: a chest with a perk of an unknown condition");
    const world::HitResult plain = shop.strike(*wolf, "Test_Light", "TestAxe");
    check(near(plain.damage.raw, kWeaponDamage, 0.1F), "a perk row with a condition that the backend cannot evaluate (TargetMarker) adds nothing");
}

/** IZ6: the heart rune: the charge of hits, the state of the owner, the key, one explosion, the variant, the refusals. */
void heart_rune(const Options&) {
    Shop shop;
    world::Entity& own = *shop.character();
    check(shop.wear("TestHammer", world::paperdoll::kMainHand1).ok(), "setup: a hammer in the hand");
    world::Entity* wolf = shop.wolf(2.0F);
    const world::EntityId wolfId = wolf->id;
    (void)shop.strike(*wolf, "Test_Light");
    check(shop.world().runes().percent_of(kCharacterA) == 0.0F, "no heart rune: a hit gives no charge");
    world::GiveOptions options;
    (void)shop.world().inventory().give(kCharacterA, "TestRune", 1, options);
    const world::ItemResult rune = shop.world().inventory().equip(kCharacterA, 0, world::paperdoll::kHeartRune);
    check(rune.ok() && own.equipment.item(world::paperdoll::kHeartRune) != nullptr, "setup: the heart rune in slot 45");
    const std::size_t from = shop.a.got.size();
    for (int hit = 0; hit < 5; ++hit) {
        (void)shop.strike(*shop.world().creatures().find(wolfId), "Test_Light");
    }
    shop.walker.idle(2);
    const auto charges = fragments_of(shop.a, 48, 5257, from);
    check(near(shop.world().runes().percent_of(kCharacterA), 50.0F) && !charges.empty() && charges.back().fields.at({0, 0})[0] == 50,
          "five hits of a row with AttackRuneCharge 6: 30 of 60 is 50 percent; the owner gets the charge state (slot 48, type 5257) "
          "with the byte 50");
    shop.walker.bits({kRuneBit});
    check(shop.world().runes().counts().activations[static_cast<std::size_t>(world::RuneVerdict::notFull)] == 1,
          "the key of the rune (bit 450) with a charge that is not full changes nothing");
    for (int hit = 0; hit < 6; ++hit) {
        (void)shop.strike(*shop.world().creatures().find(wolfId), "Test_Light");
    }
    check(near(shop.world().runes().percent_of(kCharacterA), 100.0F), "the charge stops at its maximum");
    // Two more creatures: one in the radius 5 and one behind it at 9 m.
    world::Entity* near5 = shop.wolf(-3.0F);
    world::Entity* far9 = shop.wolf(-9.0F);
    const world::EntityId nearId = near5->id;
    const world::EntityId farId = far9->id;
    const float nearHealth = near5->vitals.health;
    const float farHealth = far9->vitals.health;
    shop.walker.bits({kRuneBit});
    shop.walker.bits({kRuneBit});
    const bool dominator = std::any_of(own.statuses.begin(), own.statuses.end(),
                                       [](const world::ActiveStatus& status) { return status.status == "Rune_Detonate_Dominator"; });
    check(shop.world().runes().counts().activations[0] == 1 && shop.world().runes().counts().activations[static_cast<std::size_t>(world::RuneVerdict::active)] == 1
              && shop.world().runes().percent_of(kCharacterA) == 0.0F && dominator && near(own.gear.physicalArmor, -0.5F),
          "a full charge: the key activates (the charge goes to 0) with the status of the first variant that a perk of the rune "
          "has (Dominator, before Major: armor -50 percent); a second key in the status changes nothing");
    shop.pass(3.2F);
    near5 = shop.world().creatures().find(nearId);
    far9 = shop.world().creatures().find(farId);
    check(shop.world().runes().counts().explosions == 1 && near5->vitals.health < nearHealth && near(far9->vitals.health, farHealth),
          "at the end of the status (3 s): one explosion; the creature 3 m away takes its damage, the creature 9 m away does not");
    shop.pass(3.0F);
    check(shop.world().runes().counts().explosions == 1, "one activation gives one explosion");
    // An activation and a death before the end: no explosion.
    (void)shop.world().runes().set_charge(kCharacterA, 100.0F);
    check(shop.world().runes().activate_character(kCharacterA) == world::RuneVerdict::activated, "setup: a second activation");
    (void)shop.world().health().hurt(kCharacterA, 1.0e7F);
    shop.pass(3.5F);
    check(shop.world().runes().counts().explosions == 1 && shop.world().runes().counts().cancelled == 1,
          "a character that died before the end of the status makes no explosion");
}

/** IZ7: the consumables: the quick slot key, the use of the bag, the cooldown and its countdown, the effects, the refusals. */
void consumables(const Options&) {
    Shop shop;
    world::Entity& own = *shop.character();
    world::GiveOptions options;
    (void)shop.world().inventory().give(kCharacterA, "TestPotion", 3, options);
    (void)shop.world().inventory().give(kCharacterA, "TestPotionB", 2, options);
    check(shop.world().inventory().equip(kCharacterA, 0, 14).ok(), "setup: three potions in the quick slot 14 (key 1), two others in the bag");
    own.vitals.health = 100.0F;
    const std::size_t from = shop.a.got.size();
    shop.walker.bits({kQuickBits[0]});
    shop.walker.idle(2);
    const world::ItemInstance* left = own.equipment.item(14);
    check(left != nullptr && left->quantity == 2 && near(own.vitals.health, std::min(100.0F + 400.0F + 0.1F * own.vitals.healthMax, own.vitals.healthMax)),
          "the quick slot key (bit 404): one potion goes, and the character gets Health 400 plus 10 percent of its maximum");
    bool countdown = false;
    for (const Fragment& fragment : fragments_of(shop.a, 45, 2932, from)) {
        if (const auto field = fragment.fields.find({0, 3}); field != fragment.fields.end()) {
            server::hub::Reader reader(field->second);
            reader.size();
            reader.u8();
            reader.size64();
            const std::uint32_t count = reader.size();
            for (std::uint32_t entry = 0; entry < count; ++entry) {
                countdown = countdown || reader.u32() == gamedata::item_id_crc("testpotion");
                reader.u64();
                reader.u32();
                reader.u32();
            }
        }
    }
    check(countdown, "the cooldown state of the owner has the item map (field 3) with the key of the item id (live check I7)");
    own.vitals.health = 100.0F;
    shop.walker.bits({kQuickBits[0]});
    check(own.equipment.item(14)->quantity == 2 && near(own.vitals.health, 100.0F)
              && shop.world().consumables().counts().uses[static_cast<std::size_t>(world::UseVerdict::cooldown)] == 1,
          "a use in the cooldown takes no item and gives nothing");
    // The bag item of the same cooldown id: 80 percent of 25 s (the shared part), then it can be used.
    const std::uint32_t bagSlot = [&] {
        for (std::uint32_t slot = 0; slot < own.bag.slots.size(); ++slot) {
            if (own.bag.slots[slot].item && own.bag.slots[slot].item->itemId == "TestPotionB") {
                return slot;
            }
        }
        return 0U;
    }();
    server::hub::Writer body;
    body.u64(own.id);
    body.u64(own.id);
    body.u32(bagSlot);
    const auto use_bag = [&] {
        shop.host.datagram(shop.a.peer, shop.a.component_message(server::rep::kTypeUseItem, own.actor.bytes, body.data()));
        shop.walker.idle(1);
    };
    shop.pass(19.0F);
    use_bag();
    check(own.bag.slots[bagSlot].item->quantity == 2, "the other potion of the cooldown id: refused before 80 percent of 25 s");
    shop.pass(1.5F);
    use_bag();
    check(own.bag.slots[bagSlot].item->quantity == 1, "the use of the bag (message 2917): taken after 80 percent of 25 s");
    // A food: the attribute bonus for its time; a second food ends the first one (RemoveStatusEffectCategories).
    (void)shop.world().inventory().give(kCharacterA, "TestOldFood", 1, options);
    (void)shop.world().inventory().give(kCharacterA, "TestFood", 1, options);
    const auto slot_of = [&](const char* id) {
        for (std::uint32_t slot = 0; slot < own.bag.slots.size(); ++slot) {
            if (own.bag.slots[slot].item && own.bag.slots[slot].item->itemId == id) {
                return slot;
            }
        }
        return 9999U;
    };
    (void)shop.world().consumables().use(kCharacterA, false, slot_of("TestOldFood"));
    const std::uint32_t oldBonus = own.attributes.bonus[world::attribute::kConstitution];
    shop.pass(1.2F);
    (void)shop.world().consumables().use(kCharacterA, false, slot_of("TestFood"));
    check(oldBonus == 4 && own.attributes.bonus[world::attribute::kConstitution] == 10 && own.statuses.size() == 1,
          "a food gives its attribute bonus (MODConstitution 4), and a newer food ends it (the category Attributes) with its own (10)");
    // A regeneration potion: a tick each second for its time (5 s).
    (void)shop.world().inventory().give(kCharacterA, "TestRegenPotion", 1, options);
    own.vitals.health = 100.0F;
    (void)shop.world().consumables().use(kCharacterA, false, slot_of("TestRegenPotion"));
    shop.pass(6.0F);
    check(near(own.vitals.health, 100.0F + 5.0F * 40.0F, 1.0F) || own.vitals.health > 100.0F + 4.0F * 40.0F - 1.0F,
          "a potion with a TickRate gives its health at each tick of its time (5 ticks of 40)");
    // A mana potion through the one mana path, and a dust that ends after two hits.
    own.vitals.mana = 10.0F;
    (void)shop.world().inventory().give(kCharacterA, "TestManaPotion", 1, options);
    (void)shop.world().consumables().use(kCharacterA, false, slot_of("TestManaPotion"));
    check(near(own.vitals.mana, 50.0F, 0.5F), "a mana potion: Mana 40 through the mana path of the abilities");
    (void)shop.world().inventory().give(kCharacterA, "TestDust", 1, options);
    (void)shop.world().consumables().use(kCharacterA, false, slot_of("TestDust"));
    check(near(own.gear.physicalArmor, 0.5F), "a dust: its armor part counts in the gear values");
    world::Entity* wolf = shop.wolf(2.0F);
    const gamedata::CreatureContact bite = wolf->creature.body->attacks[0].contacts[0];
    shop.world().health().creature_hit(*wolf, bite, own);
    shop.world().health().creature_hit(*wolf, bite, own);
    check(near(own.gear.physicalArmor, 0.0F), "StopOnHitCount 2: the dust ends at the second hit that the character takes");
    (void)shop.world().inventory().give(kCharacterA, "TestSiegePotion", 1, options);
    check(shop.world().consumables().use(kCharacterA, false, slot_of("TestSiegePotion")) == world::UseVerdict::notAllowed
              && own.bag.slots[slot_of("TestSiegePotion")].item->quantity == 1,
          "an item of the class SiegeWarOnly is refused and stays");
}

/** IZ8: the cost of an ability (I14): the payment, the state, the regeneration, the refusal, a cost modifier of a node. */
void costs(const Options&) {
    Shop shop(true, true);
    world::Entity& own = *shop.character();
    (void)shop.wear("TestHammer", world::paperdoll::kMainHand1);
    own.combat.selectedWeapon = world::paperdoll::kMainHand1;
    for (const char* id : kHammerNodes) {
        if (std::string(id) != "Upgrade_WarHammer_Shockwave_Weaken") {
            own.skills.pools[kWarHammer][gamedata::progression_id(id)].level = 1;
        }
    }
    own.skills.bars[kWarHammer] = {gamedata::progression_id("Ability_WarHammer_WreckingBall"), gamedata::progression_id("Ability_WarHammer_Shockwave"),
                                   gamedata::progression_id("Ability_WarHammer_PathOfDestiny")};
    const std::size_t from = shop.a.got.size();
    shop.walker.press(0);
    check(near(own.vitals.mana, 100.0F), "Wrecking Ball: its spell names SpecialAttack, which the mana table does not have: no cost");
    shop.pass(3.0F);
    shop.walker.press(1);
    shop.walker.idle(1);
    check(near(own.vitals.mana, 70.0F, 0.2F), "Shockwave with the made-up cost id (30): the cast pays 30 mana");
    bool regenerating = false;
    for (const Fragment& fragment : fragments_of(shop.a, 47, 1652, from)) {
        if (const auto rate = fragment.fields.find({0, 3}); rate != fragment.fields.end()) {
            server::hub::Reader reader(rate->second);
            regenerating = regenerating || near(reader.f32(), 2.0F);
        }
    }
    check(regenerating, "the mana state of the owner has the rate of the regeneration (2 in each second) while the mana is not full");
    shop.pass(5.0F);
    check(near(own.vitals.mana, 80.0F, 1.0F), "the regeneration: 2 mana in each second (ManaBaseRegenRate), with no delay after a spend above 0");
    shop.pass(10.5F);
    check(near(own.vitals.mana, 100.0F) && !own.vitals.manaRegenFrom, "the mana is full again, and the regeneration stops");
    own.vitals.mana = 20.0F;
    (void)shop.world().abilities().reset_cooldowns(kCharacterA, true);
    const std::uint64_t accepted = shop.world().abilities().counts().presses[0];
    shop.walker.press(1);
    check(shop.world().abilities().counts().presses[0] == accepted
              && shop.world().abilities().counts().presses[static_cast<std::size_t>(world::CastVerdict::noMana)] == 1
              && (shop.world().abilities().cooldowns_of(kCharacterA)->count(gamedata::progression_id("Ability_WarHammer_Shockwave")) == 0
                  || shop.world().abilities().cooldowns_of(kCharacterA)->at(gamedata::progression_id("Ability_WarHammer_Shockwave")).end
                         <= shop.host.clock.now()),
          "a press with less mana than the cost makes no cast and starts no cooldown");
    own.skills.pools[kWarHammer][gamedata::progression_id("Upgrade_WarHammer_Shockwave_Weaken")].level = 1;
    own.vitals.mana = 100.0F;
    shop.walker.press(1);
    check(near(own.vitals.mana, 85.0F, 0.2F), "a learned node with ManaCostMult -0.5 for the cost id: the cast pays 15");
}

/** IZ9: the cooldown of the client (I8): after a reset of the server the client keeps its own cooldown. */
void client_cooldown(const Options&) {
    Shop shop;
    world::Entity& own = *shop.character();
    (void)shop.wear("TestHammer", world::paperdoll::kMainHand1);
    own.combat.selectedWeapon = world::paperdoll::kMainHand1;
    for (const char* id : kHammerNodes) {
        own.skills.pools[kWarHammer][gamedata::progression_id(id)].level = 1;
    }
    own.skills.bars[kWarHammer] = {gamedata::progression_id("Ability_WarHammer_WreckingBall"), gamedata::progression_id("Ability_WarHammer_Shockwave"),
                                   gamedata::progression_id("Ability_WarHammer_PathOfDestiny")};
    shop.walker.press(0);
    shop.pass(2.0F);
    (void)shop.world().abilities().reset_cooldowns(kCharacterA);
    shop.walker.press(0);
    const auto& presses = shop.world().abilities().counts().presses;
    check(presses[0] == 1 && presses[static_cast<std::size_t>(world::CastVerdict::cooldown)] == 1,
          "a reset command ends the cooldown of the server, but a press before the end of the cooldown of the client is not a "
          "cast (the client does not play it: live check I8-05)");
    shop.pass(13.5F);
    shop.walker.press(0);
    check(presses[0] == 2, "after the end of the cooldown of the client the press is a cast");
}

/** IZ10: two sessions (rule MP-10): the gear, the charge, the effects, and the cooldowns of A are values of A; B observes the creature. */
void two_sessions(const Options&) {
    Shop shop;
    shop.enter_b();
    world::Entity& alpha = *shop.character();
    world::Entity& beta = *shop.world().find_character(kCharacterB);
    check(shop.wear("TestAmulet", world::paperdoll::kAmulet, {"PerkID_Test_Strength"}, 600).ok()
              && shop.wear("TestHammer", world::paperdoll::kMainHand1).ok(),
          "setup: A wears an amulet with a strength perk and a hammer");
    (void)shop.world().inventory().give(kCharacterA, "TestRune", 1, world::GiveOptions{});
    (void)shop.world().inventory().equip(kCharacterA, 0, world::paperdoll::kHeartRune);
    check(alpha.attributes.bonus[world::attribute::kStrength] == 20 && beta.attributes.bonus[world::attribute::kStrength] == 0,
          "the attribute bonus is a value of the character that wears the item");
    world::Entity* wolf = shop.wolf(2.0F);
    const world::EntityId wolfId = wolf->id;
    const std::size_t fromA = shop.a.got.size();
    const std::size_t fromB = shop.b.got.size();
    (void)shop.world().runes().set_charge(kCharacterA, 100.0F);
    (void)shop.world().inventory().give(kCharacterA, "TestPotion", 2, world::GiveOptions{});
    (void)shop.world().consumables().use(kCharacterA, false, 0);
    (void)shop.world().runes().activate_character(kCharacterA);
    shop.pass(3.5F);
    for (int index = 0; index < 5; ++index) {
        shop.host.clock.advance(kTick);
        shop.host.tick();
        shop.b.exchange(shop.host);
    }
    wolf = shop.world().creatures().find(wolfId);
    check(!fragments_of(shop.a, 48, 5257, fromA).empty() && fragments_of(shop.b, 48, 5257, fromB).empty(),
          "the charge state goes to the owner only");
    check(fragments_of(shop.b, 45, 2932, fromB).empty() && beta.statuses.empty() && beta.itemCooldowns.empty(),
          "the cooldowns and the effects of a consumable of A are not values of B, and B gets no cooldown state");
    check(wolf != nullptr && wolf->vitals.health < kWolfHealth && !fragments_of(shop.b, kWolfSlots[1], 15, fromB).empty(),
          "the explosion of A hurts the creature, and B (an observer of the creature) gets its vitals state");
}

/** IZ11: the load: 50 characters with perks of a hit near creatures, with ticks, chains, and explosions, with a time limit. */
void load(const Options&) {
    Shop shop;
    for (int index = 0; index < 4000; ++index) {
        (void)shop.world().creatures().spawn("Test_Wolf", at(5000.0F + static_cast<float>(index % 100) * 3.0F, 5000.0F + static_cast<float>(index / 100) * 3.0F), 0.0F);
    }
    std::vector<std::string> characters;
    for (int index = 0; index < 50; ++index) {
        char id[64];
        std::snprintf(id, sizeof(id), "cccccccc-2222-4000-8000-%012d", index);
        char steam[32];
        std::snprintf(steam, sizeof(steam), "765612022552%05d", 40000 + index);
        char name[32];
        std::snprintf(name, sizeof(name), "Loader%c%c", 'a' + index / 26, 'a' + index % 26);
        const state::WorldPosition place = at(kPlace.x + static_cast<float>(index % 10) * 30.0F, kPlace.y + static_cast<float>(index / 10) * 30.0F);
        (void)add_player(shop.host.store, steam, "Load", id, name, place);
        (void)shop.world().spawn_character(id, place);
        world::GiveOptions options;
        options.perks = world::PerkIds{"PerkID_Weapon_ChainVoid", "PerkID_Weapon_Attunement_Void", "PerkID_Weapon_CDRBasic", "PerkID_Gem_VoidDMG4"};
        (void)shop.world().inventory().give(id, "TestAxe", 1, options);
        (void)shop.world().inventory().equip(id, 0, world::paperdoll::kMainHand1);
        (void)shop.world().inventory().give(id, "TestRune", 1, world::GiveOptions{});
        (void)shop.world().inventory().equip(id, 0, world::paperdoll::kHeartRune);
        for (int wolf = 0; wolf < 8; ++wolf) {
            (void)shop.world().creatures().spawn("Test_Wolf", at(place.x + 1.0F + static_cast<float>(wolf % 4), place.y + 1.0F + static_cast<float>(wolf / 4)), 0.0F);
        }
        characters.emplace_back(id);
    }
    const auto started = std::chrono::steady_clock::now();
    const gamedata::WeaponRow* weapon = shop.host.combat.weapon_of("TestAxe");
    const std::uint32_t row = *shop.host.combat.row_index("Test_Light");
    for (int round = 0; round < 40; ++round) {
        for (const std::string& id : characters) {
            world::Entity* character = shop.world().find_character(id);
            std::vector<world::EntityId> near;
            shop.world().for_each_near(world::kMainSpace, character->position, 3.0F, [&](world::Entity& entity) {
                if (entity.kind == world::EntityKind::creature && !entity.creature.dead) {
                    near.push_back(entity.id);
                }
            });
            if (!near.empty()) {
                if (world::Entity* target = shop.world().creatures().find(near.front())) {
                    const world::HitWeapon use{"TestAxe", world::Combat::weapon_slot_in_hand(*character).value_or(0), weapon, 100, {}};
                    (void)shop.world().combat().strike(*character, *target, use, row, 1.0F, "a load hit");
                }
            }
            if (round % 10 == 9) {
                (void)shop.world().runes().set_charge(id, 100.0F);
                (void)shop.world().runes().activate_character(id);
            }
        }
        for (int tick = 0; tick < 10; ++tick) {
            shop.host.clock.advance(kTick);
            shop.host.tick();
        }
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const world::GearCounts& gear = shop.world().gear().counts();
    note("50 characters, 2,000 hits with perks, " + std::to_string(gear.chainHits) + " chain hits, " + std::to_string(gear.tickHits)
         + " ticks, " + std::to_string(shop.world().runes().counts().explosions) + " explosions in a world of 4,400 creatures: "
         + std::to_string(seconds) + " s");
    check(gear.chainHits > 0 && gear.tickHits > 0 && shop.world().runes().counts().explosions >= 100,
          "setup: the hits have chains, ticks, and explosions");
    check(seconds < 5.0, "2,000 hits with their perks, 400 ticks of the world, and the explosions in less than 5 s");
}

} // namespace

std::vector<Case> itemization_cases() {
    return {{"IZ1", "itemization: the perk data file, the swap rule of the client, the potency, the perk counts, the classes", perk_data},
            {"IZ2", "itemization: an item instance with perks (the give, the rows, the wire, the equip, the refusals, the command)", instance},
            {"IZ3", "itemization: the perk roll of a loot item (the level, the fixed perks, the count roll, the filters)", loot_roll},
            {"IZ4", "itemization: the gear values (attribute bonus, health, absorption, equip load, base damage)", gear_values},
            {"IZ5", "itemization: the perks of a hit (rend, heal, burst, damage over time, gem conversion, an unknown condition)", hit_perks},
            {"IZ6", "itemization: the heart rune (charge, state, key, one explosion, the variant, death)", heart_rune},
            {"IZ7", "itemization: the consumables (quick key, bag message, cooldown map, shared cooldown, food, ticks, mana, dust, siege)",
             consumables},
            {"IZ8", "itemization: the cost of an ability (I14: payment, mana state, regeneration, refusal, node modifier)", costs},
            {"IZ9", "itemization: the cooldown of the client after a reset of the server (I8)", client_cooldown},
            {"IZ10", "itemization: two sessions (the gear, the charge, the effects of A; B observes the creature)", two_sessions},
            {"IZ11", "itemization: the load of 50 characters with perks of a hit, ticks, chains, and explosions, with a time limit", load}};
}

} // namespace soulwarden::backend::tests

// Cases for the corrections of the code review of stage 1.3b (findings F1 to F16) and of the blank comparator of an
// AbilityData row (research abilities-shared.md K15), stage 3.3b-S1. Each case fails on the tree before the corrections
// (~/Games/soulwarden/work/trees/baseline-status, with SOULWARDEN_BASELINE_CHECK: the few lines whose interface changed)
// and passes after them. The proofs of the reviewer (code-review/work/apps/backend/tests/review_itemization_faults.cpp)
// are the start of most cases. All ids, names, and values are made up.

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "server/rep/item_requests.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/shop_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace shop;

/** RI1 (F1): a hit uses the gem of the weapon instance of its slot, not of the first item with the same id. */
void gem_of_instance(const Options&) {
    {
        Shop shop;
        check(shop.wear("TestAxe", world::paperdoll::kMainHand1).ok(), "setup: the first axe has no gem");
        check(shop.wear("TestAxe", world::paperdoll::kMainHand1 + 1, {"PerkID_Gem_VoidDMG4"}).ok(), "setup: the second axe has a conversion gem");
        shop.world().combat().select_weapon(shop.character()->owner, 2);
        const world::HitResult hit = shop.strike(*shop.wolf(2), "Test_Light", "TestAxe");
        note("second axe (with the gem) in the hand: converted damage " + std::to_string(hit.damage.converted));
        check(hit.verdict == world::HitVerdict::accepted && hit.damage.converted > 0.0F, "the hit of the second axe has the conversion of its own gem");
    }
    {
        Shop shop;
        check(shop.wear("TestAxe", world::paperdoll::kMainHand1, {"PerkID_Gem_VoidDMG4"}).ok(), "setup: the first axe has the gem");
        check(shop.wear("TestAxe", world::paperdoll::kMainHand1 + 1).ok(), "setup: the second axe has no gem");
        shop.world().combat().select_weapon(shop.character()->owner, 2);
        const world::HitResult hit = shop.strike(*shop.wolf(2), "Test_Light", "TestAxe");
        note("second axe (no gem) in the hand: converted damage " + std::to_string(hit.damage.converted));
        check(hit.verdict == world::HitVerdict::accepted && hit.damage.converted == 0.0F,
              "the hit of the second axe does not take the gem of the first axe");
    }
}

/** RI2 (F2): a fixed perk of a definition counts one time after its swap (the instance has the swapped id). */
void fixed_perk_once(const Options&) {
    Shop shop;
    gamedata::PerkTables data = shop_perks();
    gamedata::PerkLayout fixed;
    fixed.fixed[0] = "PerkID_Stat_TwoHandSoldier";
    fixed.canHavePerks = false;
    data.items["testaxe"] = data.layouts.size();
    data.layouts.push_back(fixed);
    check(shop.host.perks.set(std::move(data)), "setup: an axe with one fixed attribute perk");
    const gamedata::ItemDefinition* definition = shop.host.catalog.find("TestAxe");
    const world::PerkIds rolled = world::roll_perks(shop.host.perks, *definition, 100, 10, [] { return 0.5F; });
    check(shop.world().inventory().grant(kCharacterA, {{"TestAxe", 1, 100, rolled}}, 0).ok(), "setup: the roll of the item is granted");
    check(shop.world().inventory().equip(kCharacterA, 0, world::paperdoll::kMainHand1).ok(), "setup: the axe is in the hand");
    note("one fixed perk after its swap: " + std::to_string(shop.world().gear().perks_of(*shop.character()).size())
         + " perk(s) with an effect, base damage part " + std::to_string(shop.character()->gear.baseDamage));
    check(shop.world().gear().perks_of(*shop.character()).size() == 1 && near(shop.character()->gear.baseDamage, 0.1F),
          "the fixed perk and its swapped id in the instance are one perk: BaseDamage 0.1, not 0.2");
}

/** RI3 (F3): two different perks that swap to the same perk do not stay as that perk in two places. */
void swap_duplicate(const Options&) {
    Shop shop;
    gamedata::PerkTables data = shop_perks();
    data.perks.push_back(perk("PerkID_Test_Sentry", "Inherent", "OnEquip", "Armor", "Attribute", {}, {}));
    data.swaps.push_back({100, false, "Armor", "PerkID_Test_Sentry", "PerkID_StatSwap_HP"});
    data.swaps.push_back({101, false, "Armor", "PerkID_Stat_ArmorSoldier", "PerkID_StatSwap_HP"});
    check(shop.host.perks.set(std::move(data)), "setup: two different old perks swap to the HP perk on armor");
    check(shop.wear("TestChest", world::paperdoll::kChest, {"PerkID_Test_Sentry", "PerkID_Stat_ArmorSoldier"}).ok(),
          "setup: a give with the two different perks");
    const world::ItemInstance& item = *shop.character()->equipment.item(world::paperdoll::kChest);
    note("after the swap: place 0 " + item.perks[0] + ", place 1 " + item.perks[1] + ", health part "
         + std::to_string(shop.character()->gear.healthPart));
    check(item.perks[0] == "PerkID_StatSwap_HP" && item.perks[1].empty() && near(shop.character()->gear.healthPart, 0.1F),
          "the second place that swaps to the same perk goes away: one HP perk, one health part");
}

/** RI4 (F4): a death or a leave cancels a pending rune activation, also when the character lives or comes back before its end. */
void rune_cancel(const Options&) {
    for (const bool leave : {false, true}) {
        Shop shop;
        check(shop.wear("TestRune", world::paperdoll::kHeartRune).ok(), "setup: the heart rune is in its slot");
        const world::EntityId wolfId = shop.wolf(2)->id;
        (void)shop.world().runes().set_charge(kCharacterA, 100);
        check(shop.world().runes().activate_character(kCharacterA) == world::RuneVerdict::activated, "setup: an activation");
        if (leave) {
            check(shop.world().leave(shop.character()->owner), "setup: the character leaves the world");
            check(shop.world().spawn_character(kCharacterA, kPlace) == world::SpawnError::none,
                  "setup: the character is in the world again before the end of the status");
        } else {
            (void)shop.world().health().hurt(kCharacterA, 1.0e7F);
            shop.host.clock.advance(std::chrono::milliseconds(100));
            shop.host.tick();
            check(shop.character()->life.dead, "setup: the character died before the end of the status");
            (void)shop.world().health().heal(kCharacterA);
        }
        shop.host.clock.advance(std::chrono::milliseconds(3100));
        shop.host.tick();
        shop.host.clock.advance(std::chrono::milliseconds(100));
        shop.host.tick();
        const auto explosions = shop.world().runes().counts().explosions;
        note(std::string(leave ? "leave and entry" : "death and life") + ": " + std::to_string(explosions) + " explosion(s), wolf health "
             + std::to_string(shop.world().creatures().find(wolfId)->vitals.health));
        check(explosions == 0, std::string(leave ? "a leave" : "a death") + " cancels the activation: no explosion at the end of the status");
    }
}

/** RI5 (F5): a request cannot use a consumable above the level of the character (RequiredLevel of the item). */
void consumable_level(const Options&) {
    Shop shop(false);
    gamedata::PerkTables data = shop_perks();
#ifndef SOULWARDEN_BASELINE_CHECK
    data.consumables[0].level = 55;  // TestPotion: RequiredLevel 55 of its master item row
#endif
    check(shop.host.perks.set(std::move(data)), "setup: a potion with the RequiredLevel 55");
    world::Entity& own = *shop.character();
    check(own.progression.level < 55, "setup: the character has a level below 55");
    const world::ItemResult given = shop.world().inventory().give(kCharacterA, "TestPotion", 2, {});
    check(given.ok(), "setup: two potions in the bag");
    const std::uint32_t slot = given.slots.front();
    own.vitals.health = 100.0F;
    const world::UseVerdict verdict = shop.world().consumables().use_bag(own.owner, slot);
    note(std::string("level ") + std::to_string(own.progression.level) + ", potion level 55: " + world::use_verdict_text(verdict) + ", quantity "
         + std::to_string(own.bag.slots[slot].item->quantity) + ", health " + std::to_string(own.vitals.health));
    check(verdict != world::UseVerdict::used && own.bag.slots[slot].item->quantity == 2 && near(own.vitals.health, 100.0F),
          "the use is refused: the item stays, and the health does not change");
}

/** RI6 (F6): message 2917 that names a different entity changes nothing. */
void use_message_entity(const Options&) {
    Shop shop;
    shop.enter_b();
    world::Entity& own = *shop.character();
    const world::ItemResult given = shop.world().inventory().give(kCharacterA, "TestPotion", 2, {});
    const std::uint32_t slot = given.slots.front();
    const world::Entity* other = shop.world().find_character(kCharacterB);
    server::hub::Writer body;
    body.u64(other->id);
    body.u64(other->id);
    body.u32(slot);
    own.vitals.health = 100.0F;
    shop.host.datagram(shop.a.peer, shop.a.component_message(server::rep::kTypeUseItem, own.actor.bytes, body.data()));
    shop.walker.idle(1);
    note("a request of A that names the entity of B: A has " + std::to_string(own.bag.slots[slot].item->quantity)
         + " potion(s), health " + std::to_string(own.vitals.health));
    check(own.bag.slots[slot].item->quantity == 2 && near(own.vitals.health, 100.0F), "a use that names a different entity changes nothing");
    server::hub::Writer good;
    good.u64(own.id);
    good.u64(own.id);
    good.u32(slot);
    shop.host.datagram(shop.a.peer, shop.a.component_message(server::rep::kTypeUseItem, own.actor.bytes, good.data()));
    shop.walker.idle(1);
    check(own.bag.slots[slot].item->quantity == 1, "control: the same request with the own entity uses the potion");
}

/** RI7 (F7): a shorter shared cooldown does not erase the longer own cooldown of an item. */
void own_cooldown_stays(const Options&) {
    Shop shop;
    gamedata::PerkTables data = shop_perks();
    data.consumables[0].cooldownTime = 40.0F;
    data.consumables[1].cooldownTime = 10.0F;
    check(shop.host.perks.set(std::move(data)), "setup: two potions of one cooldown id, 40 and 10 seconds (times of the game data)");
    (void)shop.world().inventory().give(kCharacterA, "TestPotion", 3, {});
    (void)shop.world().inventory().give(kCharacterA, "TestPotionB", 3, {});
    check(shop.world().consumables().use(kCharacterA, false, 0) == world::UseVerdict::used, "setup: the 40-second potion at 0 s");
    shop.pass(32.1F);
    check(shop.wear("TestAmulet", world::paperdoll::kAmulet, {"PerkID_Fixed_Earring_PotionCDR"}).ok(), "setup: a perk that shortens the cooldown");
    check(shop.world().consumables().use(kCharacterA, false, 1) == world::UseVerdict::used, "setup: the second potion after the shared time");
    shop.pass(6.5F);
    const world::UseVerdict verdict = shop.world().consumables().use(kCharacterA, false, 0);
    note(std::string("the first potion at 38.6 s: ") + world::use_verdict_text(verdict));
    check(verdict == world::UseVerdict::cooldown, "the own 40-second cooldown of the first potion still runs at 38.6 s");
    shop.pass(1.6F);
    check(shop.world().consumables().use(kCharacterA, false, 0) == world::UseVerdict::used, "control: at 40.2 s the first potion is used");
}

/** RI8 (F8): an item that comes into the bag during a cooldown of its cooldown id gets the countdown. */
void countdown_of_new_item(const Options&) {
    Shop shop;
    (void)shop.world().inventory().give(kCharacterA, "TestPotion", 2, {});
    check(shop.world().consumables().use_bag(shop.character()->owner, 0) == world::UseVerdict::used, "setup: the cooldown of the survival potions runs");
    (void)shop.world().inventory().give(kCharacterA, "TestPotionB", 2, {});
    const std::uint32_t key = gamedata::item_id_crc("testpotionb");
    const bool listed = std::any_of(shop.character()->itemCooldowns.begin(), shop.character()->itemCooldowns.end(),
                                    [key](const world::ItemCooldown& cooldown) { return cooldown.item == key; });
    const world::UseVerdict verdict = shop.world().consumables().use_bag(shop.character()->owner, 1);
    note(std::string("the new potion: in the item map ") + (listed ? "yes" : "no") + ", use: " + world::use_verdict_text(verdict));
    check(verdict == world::UseVerdict::cooldown, "setup: the new potion is in the cooldown of its id");
    check(listed, "the item map of the owner has the countdown of the new potion");
}

/** RI9 (F9): a later press that a check refuses early (an empty place of the bar) closes the contact window of the earlier cast. */
void early_refusal_closes(const Options&) {
    Shop shop;
    check(shop.wear("TestHammer", world::paperdoll::kMainHand1).ok(), "setup: the hammer is in the hand");
    const std::uint32_t ball = gamedata::progression_id("Ability_WarHammer_WreckingBall");
    world::Entity& own = *shop.character();
    own.skills.pools[kWarHammer][ball].level = 1;
    own.skills.bars[kWarHammer] = {ball, 0, 0};
    const world::EntityId wolf = shop.wolf(2)->id;
    const world::SessionId owner = own.owner;
    world::AbilityPress first;
    first.place = 0;
    first.input = 100;
    first.sequence = 1;
    check(shop.world().abilities().press(owner, first) == world::CastVerdict::accepted, "setup: Wrecking Ball");
    world::AbilityPress later = first;
    later.input = 102;
    later.place = 1;
    check(shop.world().abilities().press(owner, later) == world::CastVerdict::emptyPlace, "setup: a later press of an empty place is refused");
    const auto phase = shop.world().abilities().contact_phase(own, owner, 110, 1, wolf);
    note(std::string("a contact after the refused press is a hit of Wrecking Ball: ") + (phase ? "yes" : "no"));
    check(!phase, "the refused press closed the window of the earlier cast");
}

/** RI10 (F10): a file that declares no status rows cannot give a status number to the wire. */
void zero_status_rows(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "review-zero-status.json";
    const nlohmann::json document{{"format", 1}, {"statusRows", 0}, {"cooldowns", {{"Test", 1, 0}}},
                                  {"statusEffects", {{"StatusA", 65000, 1, 0, false, false, 0, true, 0}}}, {"spells", nlohmann::json::array()}};
    std::ofstream(path, std::ios::binary | std::ios::trunc) << document.dump();
    gamedata::AbilityData data;
    const bool loaded = data.load(path.string());
    note(std::string("a file with 0 status rows and the status number 65000: ") + (loaded ? "loaded" : "refused"));
    check(!loaded, "the file is refused");
    nlohmann::json good = document;
    good["statusRows"] = 1;
    good["statusEffects"][0][1] = 0;
    std::ofstream(path, std::ios::binary | std::ios::trunc) << good.dump();
    check(data.load(path.string()), "control: the same file with 1 row and the number 0 loads");
}

/** RI11 (F11): a cast of a different ability does not erase the end of the client cooldown of an ability. */
void client_end_stays(const Options&) {
    Shop shop;
    (void)shop.wear("TestHammer", world::paperdoll::kMainHand1);
    world::Entity& own = *shop.character();
    const std::uint32_t ball = gamedata::progression_id("Ability_WarHammer_WreckingBall");
    const std::uint32_t shock = gamedata::progression_id("Ability_WarHammer_Shockwave");
    own.skills.pools[kWarHammer][ball].level = 1;
    own.skills.pools[kWarHammer][shock].level = 1;
    own.skills.bars[kWarHammer] = {ball, shock, 0};
    world::AbilityPress press;
    press.place = 0;
    press.input = 100;
    press.sequence = 1;
    check(shop.world().abilities().press(own.owner, press) == world::CastVerdict::accepted, "setup: Wrecking Ball (a client cooldown of 15 s)");
    shop.pass(1);
    shop.world().abilities().reset_cooldowns(kCharacterA);
    press.place = 1;
    press.input = 140;
    check(shop.world().abilities().press(own.owner, press) == world::CastVerdict::accepted, "setup: Shockwave after the reset of the server");
    shop.pass(1);
    press.place = 0;
    press.input = 180;
    const world::CastVerdict verdict = shop.world().abilities().press(own.owner, press);
    note(std::string("Wrecking Ball 2 s after its press and after a different cast: ") + world::cast_verdict_text(verdict));
    check(verdict == world::CastVerdict::cooldown, "the end of the client cooldown stays: the press is refused");
}

/** RI12 (F12): a list group of a first paperdoll record stays whole, also when it is larger than the budget. */
void paperdoll_group_whole(const Options&) {
    world::Entity entity;
    world::ItemInstance item;
    item.index = 25000;
    item.quantity = 1;
    item.gearScore = 600;
    item.durability = item.maxDurability = 5000;
    item.perkIndex = {1500, 1501, 1502, 1503, 1504};
    const std::string classes = "EquippableAmulet+EquippableToken+EquippableRing+EquippablePaint+Consumable+EquippableBackpack+Bag+LoggingAxe+"
                                "Sickle+SkinningKnife+Pickaxe+FishingPole+AzothStaff+InstrumentFlute+InstrumentGuitar+InstrumentMandolin+"
                                "InstrumentUprightBass+InstrumentDrums+ThrowableObject+HeartGem+MountBuff1+MountBuff2+MountAttachment";
    std::size_t placed = 0;
    for (unsigned slot = 0; slot < world::paperdoll::kSlotCount; ++slot) {
        if (world::paperdoll::slot_accepts(slot, classes) && !world::paperdoll::visible_slot(slot)) {
            entity.equipment.slots[slot].item = item;
            ++placed;
        }
    }
    const std::vector<server::hub::Bytes> parts = server::rep::encode_paperdoll_state(entity, server::rep::View::owner, {0, 900});
    unsigned groups = 0;
    std::size_t slots = 0;
    for (const server::hub::Bytes& part : parts) {
        const PaperdollPart decoded = paperdoll_part(part);
        groups += decoded.other.empty() ? 0 : 1;
        slots += decoded.other.size();
        note("a part of the owner: " + std::to_string(part.size()) + " bytes, " + std::to_string(decoded.other.size()) + " slots that do not show");
    }
    check(placed > 20 && slots == placed, "setup: each of the " + std::to_string(placed) + " slots is in the parts");
    check(groups == 1, "the group of the slots that do not show is in one part of the first record");
}

/** RI13 (F13): the perk loader refuses a file whose links do not resolve or name the wrong kind. */
void perk_links(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "review-perk-links.json";
#ifdef SOULWARDEN_BASELINE_CHECK
    constexpr int kFormat = 1;
#else
    constexpr int kFormat = 2;
#endif
    const nlohmann::json good{
        {"format", kFormat}, {"count", 2},
        {"perks", {{"PerkID_A", "Generated", "OnEquip", "", "", "", nlohmann::json::array(), {"Affix_A"}, {"Ability_A"}, 1, false}}},
        {"affixes", {{"Affix_A", {{"MODStrength", 5}}}}},
        {"abilities", {{"Ability_A", {{"BaseDamage", 0.1}}}}},
        {"buckets", {{"Bucket_A", {"Generated", 1, nlohmann::json::array({nlohmann::json::array({"PerkID_A", 1})}), true, true}}}},
        {"layouts", {{{"", "", "", "", ""}, {"", "", "", "", ""}, {"Bucket_A", "", "", "", ""}, -1, true}}},
        {"items", {{"testaxe", 0}}}, {"consumables", nlohmann::json::object()},
        {"encumbrance", {{"equipLoadMax", 500}, {"ratioMedium", 26}, {"ratioHeavy", 46}, {"ratioOverburdened", 100}, {"damageLight", 0.15},
                         {"damageMedium", 0}, {"damageHeavy", -0.15}, {"healLight", 1.3}, {"healMedium", 1}, {"healHeavy", 0.7}}},
        {"equipLoad", nlohmann::json::object()}, {"labels", nlohmann::json::object()},
        {"swaps", {{1, "ItemClass", "Weapon", "PerkID_A", "PerkID_A"}}},
        {"generation", {{"minLevelRollPerks", 7}, {"tiers", nlohmann::json::array()}}}};
    const auto load = [&](const nlohmann::json& document) {
        std::ofstream(path, std::ios::binary | std::ios::trunc) << document.dump();
        gamedata::PerkData data;
        return data.load(path.string());
    };
    check(load(good), "control: the complete file loads");
    for (const std::string variant : {"missing-affix", "missing-ability", "missing-swap", "wrong-layout-kind"}) {
        nlohmann::json broken = good;
        if (variant == "missing-affix") {
            broken["affixes"] = nlohmann::json::object();
        } else if (variant == "missing-ability") {
            broken["abilities"] = nlohmann::json::object();
        } else if (variant == "missing-swap") {
            broken["swaps"][0][4] = "PerkID_Missing";
        } else {
            broken["layouts"][0][2][0] = "PerkID_A";
        }
        const bool loaded = load(broken);
        note(variant + ": " + (loaded ? "loaded" : "refused"));
        check(!loaded, variant + ": the file whose parts do not fit together is refused");
    }
}

/** Runs a check tool of tools/gamedata and gives the cases whose line starts with "ok". */
std::set<std::string> passed_cases(const Options& options, const char* tool, const char* folder) {
    const std::filesystem::path script = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / tool;
    std::filesystem::create_directories(options.workDirectory);
    const auto output = options.workDirectory / (std::string(folder) + ".log");
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + script.lexically_normal().string() + "\" \"" + (options.workDirectory / folder).string() + "\" > \""
                                + output.string() + "\" 2>&1";
    (void)std::system(command.c_str());
    std::ifstream log(output);
    std::set<std::string> passed;
    for (std::string line; std::getline(log, line);) {
        if (line.rfind("ok  ", 0) == 0) {
            passed.insert(line.substr(6, line.find(':') - 6));
        }
    }
    return passed;
}

/** RI14 (F14, F15, F16, and the level of F5): the perk tool stops for a missing value column and for two rows that do not agree,
 *  and a bucket ends at the first empty cell. */
void perk_tool(const Options& options) {
    const std::set<std::string> passed = passed_cases(options, "check_perk_tool.py", "review-perk-tool");
    check(passed.count("valid") == 1, "complete tables give the file of format 2 with the level of a consumable (F5)");
    for (const char* name : {"missing-damageLight", "missing-BaseDamage", "missing-StatusEffect"}) {
        check(passed.count(name) == 1, std::string("F14, ") + name + ": the tool stops with status 1 and keeps the old file");
    }
    for (const char* name : {"conflicting-affix", "conflicting-ability"}) {
        check(passed.count(name) == 1, std::string("F15, ") + name + ": the tool stops with status 1 and keeps the old file");
    }
    for (const char* name : {"bucket-gap", "weight-gap", "weight-zero"}) {
        check(passed.count(name) == 1, std::string("F16, ") + name + ": the bucket list ends at the first empty cell (a weight 0 is an entry)");
    }
}

/** RI15 (research K15): a blank comparison of an AbilityData row is LessThanOrEqual (Executioner: 50 percent). */
void blank_comparator(const Options&) {
    Shop shop;
    gamedata::PerkTables data = shop_perks();
    data.perks.push_back(perk("PerkID_Test_Executioner", "Generated", "OnEquip", "EquippableMainHand+EquippableTwoHand", "Test_Exec", {},
                              {"Test_Ability_Executioner"}));
    data.abilities["test_ability_executioner"] = cells({{"OnHit", 1.0F}, {"TargetHealthPercent", 50.0F}, {"BaseDamage", 0.5F}});
    data.labels["Test_Exec"] = {100.0F, {}};
    check(shop.host.perks.set(std::move(data)), "setup: a perk row with TargetHealthPercent 50 and no TargetComparisonType");
    check(shop.wear("TestHammer", world::paperdoll::kMainHand1, {"PerkID_Test_Executioner"}).ok(), "setup: the hammer with the perk");
    world::Entity& wolf = *shop.wolf(2);
    world::HitContext hit;
    hit.row = "Test_Light";
    hit.attackType = "Light";
    std::vector<float> parts;
    for (const float percent : {40.0F, 50.0F, 60.0F}) {
        wolf.vitals.health = wolf.vitals.healthMax * percent / 100.0F;
        parts.push_back(shop.world().gear().hit_damage(*shop.character(), wolf, hit));
    }
    note("the part of the gear at 40, 50, 60 percent of the health of the target: " + std::to_string(parts[0]) + ", " + std::to_string(parts[1])
         + ", " + std::to_string(parts[2]));
    check(near(parts[0], parts[2] + 0.5F) && near(parts[1], parts[2] + 0.5F),
          "the BaseDamage counts at 40 and at 50 percent (LessThanOrEqual, enum 4 of the parser 0x43B6390), not at 60 percent");
}

} // namespace

std::vector<Case> review_itemization_cases() {
    return {{"RI1", "review of 1.3b, F1: a hit uses the gem of its weapon instance", gem_of_instance},
            {"RI2", "review of 1.3b, F2: a fixed perk counts one time after its swap", fixed_perk_once},
            {"RI3", "review of 1.3b, F3: two perks that swap to one perk do not stay in two places", swap_duplicate},
            {"RI4", "review of 1.3b, F4: a death or a leave cancels a rune activation", rune_cancel},
            {"RI5", "review of 1.3b, F5: a consumable above the level of the character is refused", consumable_level},
            {"RI6", "review of 1.3b, F6: message 2917 with a different entity changes nothing", use_message_entity},
            {"RI7", "review of 1.3b, F7: a shared cooldown does not erase the own cooldown of an item", own_cooldown_stays},
            {"RI8", "review of 1.3b, F8: a new item of a running cooldown id gets the countdown", countdown_of_new_item},
            {"RI9", "review of 1.3b, F9: an early refusal closes the contact window of the earlier cast", early_refusal_closes},
            {"RI10", "review of 1.3b, F10: a file with 0 status rows cannot give a status number", zero_status_rows},
            {"RI11", "review of 1.3b, F11: a different cast does not erase the end of the client cooldown", client_end_stays},
            {"RI12", "review of 1.3b, F12: a large list group of a first paperdoll record stays whole", paperdoll_group_whole},
            {"RI13", "review of 1.3b, F13: the perk loader refuses links that do not resolve", perk_links},
            {"RI14", "review of 1.3b, F14 to F16 and F5: the perk tool (check_perk_tool.py)", perk_tool},
            {"RI15", "research K15: a blank comparison of an AbilityData row is LessThanOrEqual", blank_comparator}};
}

} // namespace soulwarden::backend::tests

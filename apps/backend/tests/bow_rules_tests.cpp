// The cases of stage B4 (the other rules of the bow build of the user; research bow-build.md section 4 and bow-build-2.md Q5 to Q8). Made-up ids
// and the rows of the game data (support/bow_rules_fixture.h); no capture and no id of an account. The proof that a case fails on the tree
// before the stage: build this file with SOULWARDEN_B4_BASELINE (support/bow_rules_fixture.h).
//
//   BR1   the game data of the stage: the item catalog of format 4 (the trinket stat lines), the stamina data of format 2 (the mark
//         CanAttackCancel), the creature data with the attack type of a contact; the tool checks; the older formats load
//   BR2   the stat lines of the jewelry: the max health (one pool with Wellness), the base damage of a hit, the cooldown reduction (one sum with
//         Refreshing; the key 0x4F of the stat multiplier state of the owner)
//   BR3   the threshold abilities of the attributes: Dex_Bonus_25_1, Dex_Bonus_50_1, Str_Bonus_25_1, Con_Bonus_100_1 (the gear values), the bonus of
//         a food, no mana part twice, the non-combat row
//   BR4   Long Range: DistFromDefender 10 with GreaterThanOrEqual (9.9 m and 10.1 m)
//   BR5   Finishing Shot: a target condition with no event is a rule of each hit (50 and 51 percent health)
//   BR6   the outgoing time factor: Battle Precision and Dex_Bonus_150_1 on a DoT of a creature with a debuff; no factor without the debuff, for a
//         status of no category, and for a status of the character on itself
//   BR7   Enchanted Ward: a Light and a Heavy creature hit lose a part, a Magic hit and a hit with no attack type do not; plain and II together
//   BR8   Refreshing: the cooldown of a cast with the fixed cooldown rule (IsStackableMax 3) and the cooldown line of the jewelry
//   BR9   Vital Synergy: the statuses of the perk script below half the stamina and below half the health; their end; no perk, no status
//   BR10  the consumables: ConsumableHealMod of the perk rows (Alchemist Reprieve, The Thirst) in the heal of a potion with Divine (decision D9),
//         The Thirst +25 percent for its cooldown id only, the Toast perks (IsConsumableIds, the activation cooldown)
//   BR11  Desert Sunrise: the EquipAbility of a food status shortens a DoT on the character (-25 percent), not a different status
//   BR12  two sessions (MP-10): the owner gets the stat multiplier key 0x4F and the status of Vital Synergy; the observer sees the public
//         status and the max health of the owner, gets no stat multiplier; the rules of A do not change B
//   BR13  the load: the rules of the stage in 300 rounds of hits, creature hits, and ticks with 40 or 4,000 far creatures, with a time limit
//   BR14  the list of the rules: each rule of the stage is applied, with its note; a non-combat threshold row names the gathering system

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "gamedata/creature_data.h"
#include "gamedata/item_catalog.h"
#include "gamedata/stamina_data.h"
#include "support/bow_rules_fixture.h"
#include "support/cases.h"
#include "support/check.h"

namespace soulwarden::backend::tests {
namespace {

using namespace rules;
using nlohmann::json;

/** Runs a Python check of the tree with arguments. @return Its lines (each one a note). */
std::vector<std::string> run_tool(const Options& options, const std::string& script, const std::vector<std::string>& arguments, int* status = nullptr) {
    const std::filesystem::path path = options.goldenDirectory / ".." / ".." / ".." / ".." / script;
    check(std::filesystem::is_regular_file(path), "the check script " + script + " is in the tree");
    std::filesystem::create_directories(options.workDirectory);
    const auto output = options.workDirectory / (std::filesystem::path(script).stem().string() + "-br.log");
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \"" + path.lexically_normal().string() + "\"";
    for (const std::string& argument : arguments) {
        command += " \"" + argument + "\"";
    }
    command += " > \"" + output.string() + "\" 2>&1";
    const int result = std::system(command.c_str());
    if (status != nullptr) {
        *status = result;
    }
    std::ifstream log(output);
    std::vector<std::string> lines;
    for (std::string line; std::getline(log, line);) {
        note(line.substr(0, 200));
        lines.push_back(line);
    }
    std::filesystem::remove(output);
    return lines;
}

/** @return True when the line of a case of a check starts with "ok". */
bool case_ok(const std::vector<std::string>& lines, const std::string& name) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) {
        return line.rfind("ok", 0) == 0 && line.find(" " + name + ":") != std::string::npos;
    });
}

/** @return The view of one rule of a character in the list of the evaluator (an empty view: none). */
world::RuleView view_of(RulesShop& shop, const char* id) {
    for (const world::RuleView& view : shop.world().passives().describe(*shop.character())) {
        if (view.id == id) {
            return view;
        }
    }
    return {};
}

/** @return The damage part of the rules of a projectile hit of A with the bow (Passives::before_hit) on a target. */
float part_of(RulesShop& shop, world::Entity& target, const char* row = "TestBowLight", const char* attackType = "Light") {
    world::HitContext hit;
    hit.row = row;
    hit.attackType = attackType;
    hit.kind = world::HitKind::projectile;
    hit.table = gamedata::progression_id(kBowTable);
    hit.target = target.id;
    hit.weapon = shop.host.combat.weapon_of("TestBow");
    hit.gearScore = 100;
    return shop.world().passives().before_hit(*shop.character(), target, hit).damage;
}

/** @return The length of the cooldown of Rapid Shot of A (its end less its start; -1: none). */
float cooldown_length(RulesShop& shop) {
    const world::AbilityCooldown* timer = shop.cooldown(kRapid);
    return timer == nullptr ? -1.0F : std::chrono::duration<float>(timer->end - timer->start).count();
}

/** BR1: the game data of the stage (formats, loaders, tools). */
void data_formats(const Options& options) {
    // The item catalog of format 4: the member trinkets (the tool check writes the file of the case "trinket").
    const std::filesystem::path work = options.workDirectory / "bow-rules-catalog";
    const std::vector<std::string> lines =
        run_tool(options, "tools/gamedata/check_item_catalog_tool.py", {work.string(), "trinket", "trinket-missing", "trinket-invalid", "upgrade"});
    for (const char* name : {"trinket", "trinket-missing", "trinket-invalid", "upgrade"}) {
        check(case_ok(lines, name), std::string("the item catalog tool: case ") + name);
    }
    gamedata::ItemCatalog catalog;
    const bool loaded = catalog.load((work / "item-catalog-trinket.json").string());
#ifndef SOULWARDEN_B4_BASELINE
    const gamedata::TrinketStats* amulet = catalog.trinket("reviewamulet");
    const gamedata::TrinketStats* ring = catalog.trinket("ReviewRing");
    const gamedata::TrinketStats* earring = catalog.trinket("ReviewEarring");
    check(loaded && amulet != nullptr && ring != nullptr && earring != nullptr && catalog.trinket("ReviewGloves") == nullptr
              && near(amulet->parts(725)[0], 0.0453125F, 0.00001F) && near(ring->parts(725)[1], 0.090625F, 0.00001F)
              && near(earring->parts(800)[2], 0.05F, 0.00001F),
          "the loader reads format 4: the lines of the amulet (725: 4.53 percent max health), the ring (725: 9.06 percent base damage), the "
          "earring (800: 5.00 percent cooldown reduction); no line for gloves");
#else
    check(false, "the loader reads format 4 (no trinket stat lines before stage B4)");
    (void)loaded;
#endif
    std::ifstream input(work / "item-catalog-trinket.json");
    const json document = json::parse(input, nullptr, false);
    const auto load = [&](const json& changed) {
        const auto path = work / "item-catalog-form.json";
        std::ofstream(path) << changed.dump();
        gamedata::ItemCatalog other;
        return other.load(path.string());
    };
    json noMember = document;
    noMember.erase("trinkets");
    json unknownItem = document;
    unknownItem["trinkets"][0][0] = "NotAnItem";
    json negative = document;
    negative["trinkets"][0][2] = -0.5;
    json twice = document;
    twice["trinkets"].push_back(document["trinkets"][0]);
    json older = noMember;
    older["format"] = 3;
    check(!load(noMember) && !load(unknownItem) && !load(negative) && !load(twice) && load(older),
          "a file of format 4 with no trinkets, with an item that the list does not have, with a negative value, or with an item two times does "
          "not load; a file of format 3 loads (no line)");

    // The stamina data of format 2: the mark CanAttackCancel of each dodge fragment (code review of stage B3, finding B3-R1).
    int status = 0;
    const std::vector<std::string> stamina = run_tool(options, "tools/gamedata/check_stamina_tool.py", {(options.workDirectory / "bow-rules-stamina").string()},
                                                      &status);
    check(status == 0 && case_ok(stamina, "valid") && case_ok(stamina, "missing-attack-cancel"),
          "the stamina tool writes format 2 with the mark, and stops for a fragment with no mark");
    std::ifstream staminaFile(options.workDirectory / "bow-rules-stamina" / "valid.json");
    const json staminaDocument = json::parse(staminaFile, nullptr, false);
    const auto stamina_load = [&](const json& changed, gamedata::StaminaData& data) {
        const auto path = options.workDirectory / "bow-rules-stamina-form.json";
        std::ofstream(path) << changed.dump();
        return data.load(path.string());
    };
    gamedata::StaminaData two;
    const bool twoLoaded = stamina_load(staminaDocument, two);
    json formatOne = staminaDocument;
    formatOne["format"] = 1;
    for (auto& [name, fragment] : formatOne["dodge"]["fragments"].items()) {
        fragment.erase("attackCancel");
    }
    gamedata::StaminaData one;
    const bool oneLoaded = stamina_load(formatOne, one);
    json missing = staminaDocument;
    missing["dodge"]["fragments"]["Dodge_Roll"].erase("attackCancel");
    gamedata::StaminaData broken;
#ifndef SOULWARDEN_B4_BASELINE
    const gamedata::DodgeFragment* fast = twoLoaded ? two.fragment_of(two.tables().dodge.front()) : nullptr;
    check(twoLoaded && fast != nullptr && fast->attackCancel && near(*fast->attackCancel, 0.266667F) && oneLoaded
              && one.fragment_of(one.tables().dodge.front()) != nullptr && !one.fragment_of(one.tables().dodge.front())->attackCancel
              && !stamina_load(missing, broken),
          "the loader reads format 2 (the mark 0.2667 s), format 1 (no mark), and refuses format 2 with a fragment with no mark");
#else
    check(false, "the stamina data of format 2 (the mark CanAttackCancel)");
    (void)twoLoaded;
    (void)oneLoaded;
    (void)broken;
#endif

    // The creature data: a contact with the attack type of its damage row (10 values); a file of an older tool (9 values) loads with none.
    const auto slice = [](const char* path, const char* asset, const char* root) {
        return json{{"path", path}, {"asset", asset}, {"sub", 2}, {"root", root}, {"interestRadius", 60.0}, {"movement", 5}, {"vitals", 9},
                    {"marker", 15}, {"slots", {{5, "ActionList", 11}, {9, "Vitals", 15}, {15, "Marker", 5399}}},
                    {"components", json::array({json::array({root, "0000000000000A01", "Root/Vitals"})})},
                    {"actions", {{"Idle_No_Task", "Idle", 0, false}, {"Death", "r_Death", 4, true}, {"Attack_Bite", "Attack_Bite", 7, false}}}};
    };
    json creatures{{"format", 2},
                   {"levels", {{1, 337.5, 145.52, 50.0}, {18, 1690.875977, 616.41, 180.0}}},
                   {"slices", {slice("slices/test/wolf.dynamicslice", "11111111-1111-1111-1111-111111111111", "1000000000000011")}},
                   {"kinds", {{{"id", "Test_Wolf"}, {"vitals", "Test_Wolf"}, {"category", "Test_Wolf_Category"}, {"vitalsId", 0x11112222},
                               {"categoryId", 0x11112223}, {"name", "Test_Wolf"}, {"vitalsName", "Test_Wolf"}, {"family", "Wildlife"},
                               {"creatureType", "Solo"}, {"health", 1.0}, {"level", 18}, {"bodies", json::array({json::array({0, 100})})},
                               {"points", 0}}}},
                   {"spawnPoints", {{"p-wolf", 0, 18, 1010.0, 1000.0, 50.0, 90.0, 360.0, 360.0, 50.0}}}};
    creatures["slices"][0]["behavior"] = {{"sight", {75.0, 12.0}}, {"combatSight", {180.0, 30.0}}, {"leash", {20.0, 70.0}}, {"speed", {4.75, 9.23}}};
    creatures["slices"][0]["tags"] = "240400000000000000000000";
    creatures["slices"][0]["attacks"] =
        json::array({json::array({2, 1.43333, json::array({json::array({0.566667, 2.18, 0.2, 0.0, "Slash", 4, "Physical", "Attack_01_Bite", 0.0, "Light"})})})});
    const auto creature_load = [&](const json& content, gamedata::CreatureData& data) {
        const auto path = options.workDirectory / "bow-rules-creatures.json";
        std::ofstream(path, std::ios::binary | std::ios::trunc) << content.dump();
        return data.load(path.string());
    };
    gamedata::CreatureData typed;
    const bool typedLoaded = creature_load(creatures, typed);
    json nine = creatures;
    nine["slices"][0]["attacks"][0][2][0].erase(9);
    gamedata::CreatureData old;
    const bool oldLoaded = creature_load(nine, old);
    json wrong = creatures;
    wrong["slices"][0]["attacks"][0][2][0][9] = 5;
    gamedata::CreatureData refused;
#ifndef SOULWARDEN_B4_BASELINE
    check(typedLoaded && typed.slices().front().attacks.front().contacts.front().attackType == "Light" && oldLoaded
              && old.slices().front().attacks.front().contacts.front().attackType.empty() && !creature_load(wrong, refused),
          "a contact of 10 values has the attack type Light; a contact of 9 values loads with none; a tenth value that is not a text is refused");
#else
    check(false, "the creature data with the attack type of a contact");
    (void)typedLoaded;
    (void)oldLoaded;
    (void)refused;
#endif
}

/** BR2: the stat lines of the jewelry. */
void trinket_lines(const Options&) {
    RulesShop shop(true);
    shop.enter_b();
    shop.ready(kCharacterA, {}, {});
    world::Entity& own = *shop.character();
    const float before = own.vitals.healthMax;
    const std::size_t fromA = shop.a.got.size();
    const std::size_t fromB = shop.b.got.size();
    check(shop.wear_on(kCharacterA, "TestAmulet", world::paperdoll::kAmulet, {}, 600)
              && shop.wear_on(kCharacterA, kRing, world::paperdoll::kRing, {}, 725)
              && shop.wear_on(kCharacterA, kEarring, world::paperdoll::kToken, {}, 800),
          "setup: the amulet (600, the largest gear score of the test amulet), the ring (725), and the earring (800)");
    shop.walker.idle(3);
    const world::Entity& after = *shop.character();
    note("the max health " + std::to_string(before) + " to " + std::to_string(after.vitals.healthMax) + ", the base damage part "
         + std::to_string(after.gear.baseDamage) + ", the cooldown line " + std::to_string(shop.cooldown_line()));
    check(near(after.vitals.healthMax, std::round(before * (1.0F + 0.0375F)), 1.0F),
          "the amulet line: the max health is the plain health times 1 + 0.0375 (client 0x3636A50: 600 x 0.625 x 0.01 x 0.01)");
    check(near(after.gear.baseDamage, 0.090625F, 0.0001F), "the ring line: +0.0906 in the base damage part of each hit");
    check(near(shop.cooldown_line(), 0.05F, 0.0001F), "the earring line: a cooldown reduction of 0.05 (800 x 0.625 x 0.01 x 0.01)");
    // The cooldown of a cast: 14 s of Rapid Shot x (1 - 0.05).
    shop.press(2);
    shop.walker.idle(2);
    note("the cooldown of Rapid Shot with the line: " + std::to_string(cooldown_length(shop)) + " s");
    check(near(cooldown_length(shop), kRapidCooldown * 0.95F, 0.01F), "the cooldown of a cast is 14 x 0.95 = 13.3 s");
    // The stat multiplier state of the owner has the key 0x4F (CooldownReduction) with 10500; the observer gets no stat multiplier state.
    bool key = false;
    for (const Fragment& fragment : fragments_of(shop.a, server::rep::kStatMultiplierSlot, server::rep::kTypeStatMultiplierState, fromA)) {
        const auto found = fragment.fields.find({1, 0});
        if (found == fragment.fields.end()) {
            continue;
        }
        std::string hex;
        for (const std::uint8_t byte : found->second) {
            static constexpr char kDigits[] = "0123456789abcdef";
            hex += kDigits[byte >> 4];
            hex += kDigits[byte & 15];
        }
        key = key || hex.find("4f0000290400") != std::string::npos;
    }
    check(key, "A gets the key 0x4F with 10500 (the multiplier 1.05) in its stat multiplier state");
    check(fragments_of(shop.b, server::rep::kStatMultiplierSlot, server::rep::kTypeStatMultiplierState, fromB).empty(),
          "B gets no stat multiplier state of A");
    // The line goes with the item.
    world::Entity& again = *shop.character();
    check(shop.world().inventory().unequip(kCharacterA, world::paperdoll::kToken).ok(), "setup: the earring off");
    shop.world().gear().refresh(again);
    check(near(shop.cooldown_line(), 0.0F, 0.0001F), "with no earring: no cooldown line");
}

/** BR3: the threshold abilities of the attributes. */
void thresholds(const Options&) {
    RulesShop shop;
    shop.ready(kCharacterA, {}, {});
    world::Entity* wolf = shop.wolf_at(6.0F);
    // Dexterity 24: no threshold; 25: Dex_Bonus_25_1 (CritChance 0.05, a fixed stat at each hit).
    shop.attribute(world::attribute::kDexterity, 24);
    const float low = shop.world().passives().before_hit(*shop.character(), *wolf, world::HitContext{}).critChance;
    shop.attribute(world::attribute::kDexterity, 25);
    const float high = shop.world().passives().before_hit(*shop.character(), *wolf, world::HitContext{}).critChance;
    note("the critical chance at dexterity 24: " + std::to_string(low) + ", at 25: " + std::to_string(high));
    check(near(high - low, 0.05F, 0.0001F), "dexterity 25: +0.05 critical chance (Dex_Bonus_25_1)");
    // Dexterity 50 (Dex_Bonus_50_1: +0.05 at full health) and strength 25 (Str_Bonus_25_1: +0.03 of a light hit).
    shop.attribute(world::attribute::kDexterity, 50);
    shop.attribute(world::attribute::kStrength, 25);
    world::Entity& own = *shop.character();
    own.vitals.health = own.vitals.healthMax;
    const float full = part_of(shop, *wolf, "TestBowLight");
    own.vitals.health = own.vitals.healthMax * 0.5F;
    const float hurt = part_of(shop, *wolf, "TestBowLight");
    const float heavy = part_of(shop, *wolf, "TestBowHeavy", "Heavy");
    note("the damage parts: light at full health " + std::to_string(full) + ", light hurt " + std::to_string(hurt) + ", heavy hurt "
         + std::to_string(heavy));
    check(near(full - hurt, 0.05F, 0.002F), "Dex_Bonus_50_1: +0.05 at full health only");
    check(near(hurt - heavy, 0.03F, 0.002F), "Str_Bonus_25_1: +0.03 of a light hit only");
    // The bonus of a food counts (the value with its bonus): dexterity 20 and a bonus of 30 pass the threshold 50.
    shop.attribute(world::attribute::kDexterity, 20);
    own.vitals.health = own.vitals.healthMax;
    const float noFood = part_of(shop, *wolf, "TestBowHeavy", "Heavy");
    // The bonus of a food or a perk (Attributes::bonus, MODDexterity): the value of the rules is the base value plus the bonus.
    shop.character()->attributes.bonus[world::attribute::kDexterity] = 30;
    const float withBonus = part_of(shop, *wolf, "TestBowHeavy", "Heavy");
    check(near(withBonus - noFood, 0.05F, 0.002F), "the value with its bonus passes the threshold 50 (20 + 30): +0.05 at full health");
    // Constitution 100: Con_Bonus_100_1 (MaxHealth 0.03) in the gear values; focus 100: the mana part only one time.
    shop.character()->attributes.bonus[world::attribute::kDexterity] = 0;
    const float healthBefore = shop.character()->vitals.healthMax;
    const float manaBefore = shop.character()->vitals.manaMax;
    shop.attribute(world::attribute::kConstitution, 100);
    shop.attribute(world::attribute::kFocus, 100);
    const world::Entity& end = *shop.character();
    note("the health part " + std::to_string(end.gear.healthPart) + ", the mana " + std::to_string(manaBefore) + " to " + std::to_string(end.vitals.manaMax));
    check(near(end.gear.healthPart, 0.03F, 0.0001F) && end.vitals.healthMax > healthBefore, "constitution 100: +0.03 of the max health (Con_Bonus_100_1)");
    check(near(end.vitals.manaMax, manaBefore * 1.1F, 0.5F), "focus 100: the mana +10 percent one time (the progress system; not the gear values too)");
}

/** BR4: Long Range (DistFromDefender 10, GreaterThanOrEqual). */
void long_range(const Options&) {
    RulesShop shop;
    shop.ready(kCharacterA, {}, {kLongRange});
    world::Entity* nearWolf = shop.wolf_at(9.9F);
    world::Entity* farWolf = shop.wolf_at(0.0F, 10.1F);
    const float nearPart = part_of(shop, *nearWolf);
    const float farPart = part_of(shop, *farWolf);
    note("the damage part at 9.9 m: " + std::to_string(nearPart) + ", at 10.1 m: " + std::to_string(farPart));
    check(near(farPart - nearPart, 0.1F, 0.002F), "Long Range: +0.1 at 10.1 m, not at 9.9 m");
}

/** BR5: Finishing Shot (TargetHealthPercent 50, no event). */
void finishing_shot(const Options&) {
    RulesShop shop;
    shop.ready(kCharacterA, {}, {kFinishing});
    world::Entity* wolf = shop.wolf_at(6.0F);
    wolf->vitals.health = wolf->vitals.healthMax * 0.51F;
    const float high = part_of(shop, *wolf);
    wolf = shop.world().creatures().find(wolf->id);
    wolf->vitals.health = wolf->vitals.healthMax * 0.5F;
    const float low = part_of(shop, *wolf);
    note("the damage part at 51 percent: " + std::to_string(high) + ", at 50 percent: " + std::to_string(low));
    check(near(low - high, 0.15F, 0.002F), "Finishing Shot: +0.15 at 50 percent health of the target, not at 51 percent");
}

/** BR6: the outgoing time factor (Battle Precision, Dex_Bonus_150_1). */
void outgoing(const Options&) {
    RulesShop shop;
    shop.ready(kCharacterA, {}, {kOutgoingOnly});
    world::Entity* wolf = shop.wolf_at(6.0F);
    const auto give = [&](const char* status) {
        world::Entity* target = shop.world().creatures().find(wolf->id);
        (void)shop.world().abilities().apply_status(*target, status, shop.character()->id, std::nullopt);
        return shop.ends_in(wolf->id, status);
    };
    // No debuff on the target: no factor (TargetStatusEffectCategory Debuff; the new DoT counts as present: it is a debuff itself).
    const float alone = give(kTestBurn);
    note("the DoT with Battle Precision: " + std::to_string(alone) + " s");
    check(near(alone, 12.0F, 0.05F), "Battle Precision: a DoT that the character gives lasts 1.2 times its 10 s");
    const float plain = give(kPlain);
    check(near(plain, 10.0F, 0.05F), "a status of a different category: no factor");
    shop.attribute(world::attribute::kDexterity, 150);
    world::Entity* target = shop.world().creatures().find(wolf->id);
    (void)shop.world().creatures().remove_statuses(*target, kTestBurn, {}, "a test");
    const float both = give(kTestBurn);
    note("the DoT with Battle Precision and dexterity 150: " + std::to_string(both) + " s");
    check(near(both, 12.5F, 0.05F), "Battle Precision and Dex_Bonus_150_1 in one sum: 10 x 1.25 = 12.5 s");
    // A status of the character on itself: no outgoing factor (the decision of stage B3).
#ifndef SOULWARDEN_B4_BASELINE
    const gamedata::StatusRow* burn = shop.host.abilities.status(kTestBurn);
    check(burn != nullptr && near(shop.world().passives().outgoing_factor(*shop.character(), *shop.character(), *burn), 1.0F),
          "a status of the character on itself has no outgoing factor");
#endif
}

/** BR7: Enchanted Ward (OnHitTaken, AttackType Light,Heavy, BaseDamageReduction). */
void enchanted_ward(const Options&) {
    RulesShop shop;
    shop.ready(kCharacterA, {kWard, kWardII, "", "", ""}, {});
    check(shop.wear_on(kCharacterA, "TestChest", world::paperdoll::kChest, {kWard}), "setup: a chest with a second plain Enchanted Ward");
    world::Entity* wolf = shop.wolf_at(2.0F);
    shop.world().set_creature_behavior(false);
    const auto taken = [&](const char* type) {
        world::Entity& own = *shop.character();
        own.vitals.health = own.vitals.healthMax;
        const float before = own.vitals.health;
        (void)shop.bite(*shop.world().creatures().find(wolf->id), type);
        return before - shop.character()->vitals.health;
    };
    const float light = taken("Light");
    const float heavy = taken("Heavy");
    const float magic = taken("Magic");
    const float none = taken("");
    note("the damage of a Light hit " + std::to_string(light) + ", Heavy " + std::to_string(heavy) + ", Magic " + std::to_string(magic) + ", none "
         + std::to_string(none));
    // Two plain sources (0.01 x 1 each) and the II source (0.016 x 1): 0.036.
    check(magic > 0.0F && near(light, magic * (1.0F - 0.036F), 0.05F) && near(heavy, light, 0.01F), "a Light and a Heavy hit: 3.6 percent less damage");
    check(near(none, magic, 0.01F), "a Magic hit and a hit with no attack type: no reduction");
    check(shop.taken_reductions() >= 2, "the count of the reductions");
}

/** BR8: Refreshing (a fixed cooldown rule, IsStackableMax 3) with the cooldown line of the jewelry. */
void refreshing(const Options&) {
    const auto length = [](bool earring) {
        RulesShop shop;
        shop.ready(kCharacterA, {kRefreshing, "", "", "", ""}, {});
        for (std::uint32_t slot : {world::paperdoll::kChest, world::paperdoll::kLegs, world::paperdoll::kHead}) {
            const char* item = slot == world::paperdoll::kChest ? "TestChest" : slot == world::paperdoll::kLegs ? "TestLegs" : "TestHead";
            check(shop.wear_on(kCharacterA, item, slot, {kRefreshing}), std::string("setup: Refreshing on ") + item);
        }
        if (earring) {
            check(shop.wear_on(kCharacterA, kEarring, world::paperdoll::kToken, {}, 800), "setup: the earring (800)");
        }
        shop.press(2);
        shop.walker.idle(2);
        return cooldown_length(shop);
    };
    // Four sources, IsStackableMax 3: 3 x 0.02.
    const float three = length(false);
    note("the cooldown of Rapid Shot with four sources of Refreshing: " + std::to_string(three) + " s");
    check(near(three, kRapidCooldown * (1.0F - 0.06F), 0.01F), "Refreshing: 3 of 4 sources (IsStackableMax 3): 14 x 0.94 = 13.16 s");
    // The earring line joins the sum: 14 x (1 - 0.06 - 0.05) (live check L1: the client sums Refreshing and CooldownReduction).
    const float sum = length(true);
    note("with the earring line: " + std::to_string(sum) + " s");
    check(near(sum, kRapidCooldown * (1.0F - 0.06F - 0.05F), 0.01F), "Refreshing and the cooldown line in one sum: 14 x 0.89 = 12.46 s");
}

/** BR9: Vital Synergy (the perk script of the client). */
void vital_synergy(const Options&) {
    RulesShop shop;
    shop.ready(kCharacterA, {kSynergy, "", "", "", ""}, {});
    world::Entity& own = *shop.character();
    (void)shop.world().stamina().change(own, 100.0F, "a test");
    own.vitals.health = own.vitals.healthMax;
    shop.wait(600);
    check(shop.find(kSynergyHeal) == nullptr && shop.find(kSynergyStamina) == nullptr, "full stamina and full health: no status");
    (void)shop.world().stamina().change(*shop.character(), -60.0F, "a test");
    shop.wait(600);
    check(shop.find(kSynergyHeal) != nullptr && shop.find(kSynergyHeal)->permanent && shop.find(kSynergyStamina) == nullptr,
          "stamina below half: the heal status (HealMod 0.1) with no end; no stamina status");
    check(near(shop.character()->gear.healTaken, 0.1F, 0.001F), "the heal status is a received heal part of 0.1");
    shop.character()->vitals.health = shop.character()->vitals.healthMax * 0.4F;
    shop.wait(600);
    check(shop.find(kSynergyStamina) != nullptr, "health below half: the stamina status (StaminaRate 0.1)");
    (void)shop.world().stamina().change(*shop.character(), 100.0F, "a test");
    shop.character()->vitals.health = shop.character()->vitals.healthMax * 0.5F;
    shop.wait(600);
    check(shop.find(kSynergyHeal) == nullptr && shop.find(kSynergyStamina) == nullptr, "half or more of each: the two statuses end");
    check(shop.script_statuses() == 2, "two statuses of the script came");
    // No perk: no status.
    RulesShop other;
    other.ready(kCharacterA, {}, {});
    (void)other.world().stamina().change(*other.character(), -60.0F, "a test");
    other.wait(600);
    check(other.find(kSynergyHeal) == nullptr, "a character with no Vital Synergy gets no status");
}

/** BR10: the consumables (the heal parts of the perk rows, the cooldown of The Thirst, the Toast perks). */
void consumables(const Options&) {
    RulesShop shop;
    shop.ready(kCharacterA, {kAlchemist, kThirst, kToastDamage, kToastFort, ""}, {});
    world::Entity& own = *shop.character();
    note("the received consumable part: " + std::to_string(own.gear.healConsumable));
    check(near(own.gear.healConsumable, 0.02F + 0.33F, 0.001F), "ConsumableHealMod of the perk rows: 0.02 (Alchemist Reprieve) + 0.33 (The Thirst)");
    const auto give = [&](const char* item, std::uint32_t count = 3) {
        world::GiveOptions options;
        const world::ItemResult result = shop.world().inventory().give(kCharacterA, item, count, options);
        return result.ok() && !result.slots.empty() ? result.slots.front() : 0U;
    };
    const std::uint32_t potion = give("TestPotion");
    const std::uint32_t mana = give("TestManaPotion");
    const std::uint32_t food = give(kDotFoodItem);
    shop.character()->vitals.health = 100.0F;
    const float before = shop.character()->vitals.health;
    check(shop.world().consumables().use(kCharacterA, false, potion) == world::UseVerdict::used, "setup: a potion");
    const float healed = shop.character()->vitals.health - before;
    const float expected = (400.0F + 0.1F * shop.character()->vitals.healthMax) * (1.0F + 0.35F);
    note("the potion heals " + std::to_string(healed) + " (expected " + std::to_string(expected) + ")");
    check(near(healed, std::min(expected, shop.character()->vitals.healthMax - before), 1.0F), "the potion heal has the part 1.35 of the perk rows");
    // The cooldown of The Thirst: SurvivalPotion 25 s x 1.25; PotionMana 15 s x 1.25; a food has no such row.
    const auto end_of = [&](const char* item) {
        const std::uint32_t key = gamedata::item_id_crc(item);
        for (const world::ItemCooldown& entry : shop.character()->itemCooldowns) {
            if (entry.item == key) {
                return std::chrono::duration<float>(entry.end - shop.host.clock.now()).count();
            }
        }
        return -1.0F;
    };
    note("the cooldown of the potion: " + std::to_string(end_of("TestPotion")) + " s");
    check(near(end_of("TestPotion"), 25.0F * 1.25F, 0.1F), "The Thirst: the SurvivalPotion cooldown x 1.25 = 31.25 s");
    // The Toast perks: the potion of the list gave the empower and the fortify.
    check(shop.find(kEmpower) != nullptr && shop.find(kFortify) != nullptr && shop.consumable_rules() == 2,
          "the Toast perks: a potion of the list gives the empower and the fortify");
    const float empowerEnd = shop.left(kEmpower);
    shop.wait(1000);
    check(shop.world().consumables().use(kCharacterA, false, mana) == world::UseVerdict::used, "setup: a mana potion 1 s later");
    check(near(end_of("TestManaPotion"), 15.0F * 1.25F, 0.1F), "The Thirst: the PotionMana cooldown x 1.25 (its own row)");
    check(shop.consumable_rules() == 2 && near(shop.left(kEmpower), empowerEnd - 1.0F, 0.05F),
          "a second potion of the list in 8 s gives no Toast (ActivationCooldown 8)");
    shop.wait(8000);
    check(shop.world().consumables().use(kCharacterA, false, food) == world::UseVerdict::used, "setup: a food 9 s after the potion");
    check(shop.consumable_rules() == 2, "a food is not in the list: no Toast");
    shop.wait(12000);
    check(shop.world().consumables().use(kCharacterA, false, mana) == world::UseVerdict::used, "setup: a mana potion after its cooldown");
    check(shop.consumable_rules() == 4, "the mana potion (\"testmanapotion\" in the list, another case) gives the two Toasts after 8 s");
}

/** BR11: Desert Sunrise (the EquipAbility of a food status: a DoT on the character -25 percent). */
void desert_sunrise(const Options&) {
    RulesShop shop;
    shop.ready(kCharacterA, {}, {});
    world::StatusApply food;
    food.source = world::StatusSource{world::StatusSourceKind::consumable, kDotFoodItem, 0, shop.character()->id};
    (void)shop.world().statuses().apply(*shop.character(), kDotFood, food);
    (void)shop.world().statuses().apply(*shop.character(), kPoison, {});
    (void)shop.world().statuses().apply(*shop.character(), kPlain, {});
    note("the poison lasts " + std::to_string(shop.left(kPoison)) + " s, the plain status " + std::to_string(shop.left(kPlain)) + " s");
    check(near(shop.left(kPoison), 15.0F, 0.05F), "the poison (a DoT of the categories of the row) lasts 20 x 0.75 = 15 s");
    check(near(shop.left(kPlain), 10.0F, 0.05F), "a status of a different category keeps its 10 s");
}

/** BR12: two sessions (MP-10). */
void two_sessions(const Options&) {
    RulesShop shop(true);
    shop.enter_b();
    shop.ready(kCharacterA, {kSynergy, kWard, "", "", ""}, {kOutgoingOnly});
    shop.ready(kCharacterB, {}, {});
    Walker walkerB{shop.host, shop.b, at(kPlace.x - 1.0F, kPlace.y + 1.0F)};
    walkerB.input = 400;
    const auto step = [&](int steps) {
        for (int index = 0; index < steps; ++index) {
            shop.walker.idle(1);
            walkerB.idle(1);
        }
    };
    step(3);
    const std::size_t fromA = shop.a.got.size();
    const std::size_t fromB = shop.b.got.size();
    (void)shop.world().stamina().change(*shop.character(), -60.0F, "a test");
    (void)shop.world().stamina().change(*shop.world().find_character(kCharacterB), -60.0F, "a test");
    check(shop.wear_on(kCharacterA, "TestAmulet", world::paperdoll::kAmulet, {}, 600), "setup: the amulet of A");
    step(20);
    const gamedata::StatusRow* heal = shop.host.abilities.status(kSynergyHeal);
    const auto has = [](const std::vector<std::uint16_t>& numbers, const gamedata::StatusRow* row) {
        return row != nullptr && std::find(numbers.begin(), numbers.end(), row->index) != numbers.end();
    };
    check(has(full_map_numbers(shop.a, 1, fromA), heal), "A (the owner) gets its status of Vital Synergy in its full map");
    check(has(full_map_numbers(shop.b, 3, fromB), heal), "B sees the status of A (AlwaysReplicate) in the public map of A");
    check(shop.find(kSynergyHeal, kCharacterB) == nullptr, "B (no perk, stamina below half too) has no status of Vital Synergy");
    // The max health of A (the amulet line) is public in the vitals of A; B has its own.
    const world::Entity& a = *shop.character();
    const world::Entity& b = *shop.world().find_character(kCharacterB);
    check(a.vitals.healthMax > b.vitals.healthMax, "the line of the amulet is a value of A: the max health of A is larger than the one of B");
    // A creature hit on B has no Enchanted Ward of A.
    world::Entity* wolf = shop.wolf_at(2.0F);
    shop.world().find_character(kCharacterA)->vitals.health = shop.character()->vitals.healthMax;
    shop.world().find_character(kCharacterB)->vitals.health = shop.world().find_character(kCharacterB)->vitals.healthMax;
    const float beforeA = shop.character()->vitals.health;
    const float beforeB = shop.world().find_character(kCharacterB)->vitals.health;
    (void)shop.bite(*shop.world().creatures().find(wolf->id), "Light", kCharacterA);
    (void)shop.bite(*shop.world().creatures().find(wolf->id), "Light", kCharacterB);
    const float lostA = beforeA - shop.character()->vitals.health;
    const float lostB = beforeB - shop.world().find_character(kCharacterB)->vitals.health;
    note("a Light hit: A loses " + std::to_string(lostA) + ", B " + std::to_string(lostB));
    check(lostA > 0.0F && lostA < lostB * 0.995F, "Enchanted Ward is a rule of A: A loses 1 percent less than B");
    // The outgoing factor of A is for the statuses that A gives: a DoT of B on the wolf keeps its time.
    world::Entity* target = shop.world().creatures().find(wolf->id);
    (void)shop.world().abilities().apply_status(*target, kTestBurn, shop.world().find_character(kCharacterB)->id, std::nullopt);
    check(near(shop.ends_in(wolf->id, kTestBurn), 10.0F, 0.05F), "a DoT of B (no Battle Precision) keeps its 10 s");
}

/** BR13: the load of the rules of the stage. */
void load(const Options&) {
    const auto run = [](unsigned farCount, double& seconds) {
        RulesShop shop;
        shop.ready(kCharacterA, {kSynergy, kWard, kRefreshing, kToastDamage, kThirst}, {kLongRange, kFinishing, kOutgoingOnly});
        shop.attribute(world::attribute::kDexterity, 200);
        shop.attribute(world::attribute::kStrength, 50);
        for (unsigned index = 0; index < farCount; ++index) {
            (void)shop.host.world.creatures().spawn("Test_Wolf", at(kPlace.x + 600.0F + static_cast<float>(index % 100) * 3.0F,
                                                                   kPlace.y + 600.0F + static_cast<float>(index / 100) * 3.0F), 90.0F);
        }
        world::Entity* wolf = shop.wolf_at(12.0F);
        const auto clock = std::chrono::steady_clock::now();
        for (int round = 0; round < 300; ++round) {
            (void)shop.shot(*shop.world().creatures().find(wolf->id), "TestBowLight");
            (void)shop.bite(*shop.world().creatures().find(wolf->id), "Light");
            shop.character()->vitals.health = shop.character()->vitals.healthMax * (round % 2 == 0 ? 0.4F : 1.0F);
            shop.wait(100);
        }
        seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - clock).count();
        return shop.taken_reductions();
    };
    double small = 0.0;
    double large = 0.0;
    const std::uint64_t smallCount = run(40, small);
    const std::uint64_t largeCount = run(4000, large);
    note("300 rounds: " + std::to_string(small) + " s with 40 far creatures, " + std::to_string(large) + " s with 4,000");
    check(smallCount == 300 && largeCount == 300, "each creature hit of the rounds has its reduction");
    check(large < 3.0, "the rounds with 4,000 far creatures stay below 3 s");
}

/** BR14: the list of the rules of the stage. */
void rule_list(const Options&) {
    RulesShop shop;
    shop.ready(kCharacterA, {kWard, kWardII, kRefreshing, kSynergy, kAlchemist}, {kLongRange, kFinishing, kOutgoingOnly});
    check(shop.wear_on(kCharacterA, "TestChest", world::paperdoll::kChest, {kThirst, kToastDamage}), "setup: the earring perks on a chest");
    shop.attribute(world::attribute::kStrength, 25);
    shop.attribute(world::attribute::kDexterity, 150);
    (void)shop.world().statuses().apply(*shop.character(), kDotFood, {});
    const auto applied = [&](const char* id, const char* text) {
        const world::RuleView view = view_of(shop, id);
        note(std::string(id) + ": " + (view.applied ? "applied" : "NOT APPLIED") + ", " + view.note);
        return view.applied && view.note.find(text) != std::string::npos;
    };
    check(applied(kLongRange, "OnHit"), "Long Range: applied");
    check(applied(kFinishing, "condition of the target"), "Finishing Shot: applied (a rule of each hit)");
    check(applied(kOutgoingOnly, "outgoing time factor"), "Battle Precision: applied (the outgoing time factor)");
    check(applied(kWardRow, "OnHitTaken") && applied(kWardIIRow, "OnHitTaken"), "Enchanted Ward and Enchanted Ward II: applied");
    check(applied(kRefreshingRow, "fixed cooldown reduction"), "Refreshing: applied");
    check(applied(kSynergyRow, "perk script") && applied(kSynergyRow2, "perk script"), "the two rows of Vital Synergy: applied (the perk script)");
    check(applied(kAlchemistRow, "fixed") && applied(kThirstRow, "fixed"), "Alchemist Reprieve and The Thirst (ConsumableHealMod): applied");
    check(applied(kThirstSurvival, "cooldown of a consumable") && applied(kToastDamageRow, "use of a consumable"),
          "the cooldown row of The Thirst and the Toast: applied (OnUsedConsumable)");
    check(applied(kDex25, "fixed") && applied(kDex150, "outgoing") && applied(kStr25, "OnHit"), "the threshold rows: applied");
    check(applied(kStr25Gather, "gathering"), "a non-combat threshold row: the note of the gathering system");
    check(applied(kDotFoodRow, "time of a status"), "the row that the food equips: applied (the time factor)");
}

} // namespace

std::vector<Case> bow_rules_cases() {
    return {
        {"BR1", "stage B4: the game data (item catalog format 4, stamina data format 2, the attack type of a creature contact; tools, older formats)",
         data_formats},
        {"BR2", "stage B4: the stat lines of the jewelry (max health, base damage, cooldown reduction, the stat multiplier key 0x4F)", trinket_lines},
        {"BR3", "stage B4: the threshold abilities of the attributes (dexterity, strength, constitution, focus; the bonus of a food)", thresholds},
        {"BR4", "stage B4: Long Range (DistFromDefender 10 m, 9.9 m and 10.1 m)", long_range},
        {"BR5", "stage B4: Finishing Shot (a target condition with no event, 50 and 51 percent)", finishing_shot},
        {"BR6", "stage B4: the outgoing time factor (Battle Precision, Dex_Bonus_150_1)", outgoing},
        {"BR7", "stage B4: Enchanted Ward (the attack type of a creature hit)", enchanted_ward},
        {"BR8", "stage B4: Refreshing (a fixed cooldown rule, IsStackableMax 3) and the cooldown line of the jewelry", refreshing},
        {"BR9", "stage B4: Vital Synergy (the perk script of the client)", vital_synergy},
        {"BR10", "stage B4: the consumables (ConsumableHealMod of perk rows, The Thirst, the Toast perks)", consumables},
        {"BR11", "stage B4: Desert Sunrise (the EquipAbility of a food status on a DoT)", desert_sunrise},
        {"BR12", "stage B4: two sessions (MP-10)", two_sessions},
        {"BR13", "stage B4: the load of the rules of the stage (40 and 4,000 far creatures)", load},
        {"BR14", "stage B4: the list of the rules of the stage", rule_list},
    };
}

} // namespace soulwarden::backend::tests

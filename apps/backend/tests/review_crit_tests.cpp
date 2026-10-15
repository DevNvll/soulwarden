// The corrections of the code review of stage 3.3b-S5 (the critical hits and the passive rules), made in stage 3.3b-S6. Each case
// fails on the tree of stage 3.3b-S5 (commit adeb08d) and passes after its correction. The C++ cases use only the interfaces of
// that tree, so that the same file proves it there (the proof of the reviewer: RCI1 to RCI10, RCM1, RCT1, RCT2).
//
//   RV1   F1: the mute of a development run: the prefix of the user moves to the development path before the first open (the probe
//         RCM1 of the reviewer), and a hard link through a link in the path of the user (tools/check_dev_mute.py)
//   RV2   F2: a damage row with an explicit CanCrit 0 (format 2 of the combat file) takes no critical hit; a blank cell can crit (the rule of the data)
//   RV3   F3: the CritDamage of a rule with OnCrit (Vicious) joins the additive pool of a critical hit
//   RV4   F4: Crowded Well counts the creatures of the upgraded vortex too
//   RV5   F5: an assignment and a reset of a node with a fixed stat change the live gear values
//   RV6   F6: a rule with OnHitTaken checks MyHealthPercent before it gives its status or spends its cooldown
//   RV7   F7: a projectile that stops at the body from behind is not a headshot; a shot at the head from the front is
//   RV8   F8: the BaseDamage of a rule with NumConsecutiveHits 2 comes only from the second attack
//   RV9   F9: a rule whose tracking spell is a Box (Blood Lust) is listed as not applied
//   RV10  F10: the loader refuses a rule whose target status is not a row; the ability tool keeps the old file
//         (check_ability_tool.py, case unknown-target-status)
//   RV11  F11: a Pylon whose slice has no Vitals component at all is not made and not paid
//   RV12  F12: the creature tool stops for a cut angle field (check_hit_facts_tool.py, case cut-angle)

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "support/cases.h"
#include "support/check.h"
#include "support/passive_fixture.h"
#include "support/projectile_fixture.h"
#include "support/spell_kind_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace passive;

/** Runs a check script of the tools (a path below the repository) and @return its output lines. */
std::vector<std::string> run_check(const Options& options, const std::string& script, const std::string& argument) {
    const std::filesystem::path path = options.goldenDirectory / ".." / ".." / ".." / ".." / script;
    std::filesystem::create_directories(options.workDirectory);
    const auto output = options.workDirectory / (std::filesystem::path(script).stem().string() + "-rv.log");
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + path.lexically_normal().string() + "\"" + (argument.empty() ? "" : " \"" + argument + "\"") + " > \""
                                + output.string() + "\" 2>&1";
    (void)std::system(command.c_str());
    std::ifstream log(output);
    std::vector<std::string> lines;
    for (std::string line; std::getline(log, line);) {
        lines.push_back(line);
    }
    std::filesystem::remove(output);
    return lines;
}

/** @return True when a line of the output starts with "ok" and names the case (its name and a colon), and no line FAILs it. */
bool passed(const std::vector<std::string>& lines, const std::string& name) {
    const bool ok = std::any_of(lines.begin(), lines.end(), [&](const std::string& line) {
        if (line.rfind("ok", 0) != 0) {
            return false;
        }
        const std::size_t start = line.find_first_not_of(' ', 2);
        return start != std::string::npos && line.compare(start, name.size() + 1, name + ":") == 0;
    });
    const bool failed = std::any_of(lines.begin(), lines.end(), [&](const std::string& line) {
        return line.rfind("FAIL", 0) == 0 && line.find(name + ":") != std::string::npos;
    });
    if (!ok || failed) {
        for (const std::string& line : lines) {
            if (line.find(name) != std::string::npos) {
                note(line.substr(0, 200));
            }
        }
    }
    return ok && !failed;
}

/** A made-up perk with one ability row on the axe in the main hand. */
void add_perk(PassiveShop& shop, const char* rule, const gamedata::CellValues& row) {
    auto tables = shop_perks();
    tables.perks.push_back(perk("PerkID_Review", "Generated", "OnActive", "Weapon", "Review", {}, {rule}));
    std::string key(rule);
    std::transform(key.begin(), key.end(), key.begin(), [](char letter) { return static_cast<char>(std::tolower(letter)); });
    tables.abilities[key] = row;
    check(shop.host.perks.set(tables), "setup: the perk table is valid: " + shop.host.perks.error());
    check(shop.wear("TestAxe", world::paperdoll::kMainHand1, {"PerkID_Review"}).ok(), "setup: the perk is equipped");
    shop.character()->combat.selectedWeapon = world::paperdoll::kMainHand1;
    shop.world().gear().refresh(*shop.character());
}

/** RV1 (F1): the mute against a move of the prefix of the user to the development path before the first open. */
void mute_prefix_swap(const Options& options) {
    const std::vector<std::string> lines = run_check(options, "tools/check_dev_mute.py", "");
    check(passed(lines, "prefix swap before the open (RCM1)"),
          "mute: the prefix of the user moves to the development path before the open (RCM1): the original settings file of the user "
          "stays, a message, status 0");
    check(passed(lines, "hard link, user path link"), "mute: a hard link of the settings file of the user, the user path through a link: refused");
    check(passed(lines, "separate prefix with spaces"), "control: the settings file of a separate development prefix gets the volume 0");
}

/** RV2 (F2): an explicit CanCrit 0 of a damage row (format 2 of the combat file). */
void explicit_no_crit(const Options& options) {
    nlohmann::json document = {{"format", 2},
                               {"constants", {{"levelDamage", 0.025}, {"gearScoreRate", 0.0112}, {"gearScoreInterval", 5}, {"gearScoreMinimum", 100},
                                              {"gearScoreLimit", 500}, {"gearScoreSmallerRate", 0.6667}}},
                               {"damageTypes", {{"Slash", 4, "Physical"}}},
                               {"attributes", {{"minimum", 5}, {"strength", {0.0, 1.25}}, {"dexterity", {0.0, 1.25}}, {"intelligence", {0.0, 1.25}},
                                               {"focus", {0.0, 1.25}}}},
                               {"damageRows", {{"Light", "Sword", "Light", "Slash", 1.0, 0.0, 0.0, 0.0},
                                               {"GreatAxe_GravityWell_Burst", "Ability", "Ability", "Slash", 1.0, 0.0, 0.0, 0.0}}},
                               {"weapons", {{"SwordT1", "Combat Melee", "Sword", 64.0, 0.0, 0.0, 0.0, 0.0, "Light", "Light", 0.0, "SwordAbilityTable"}}},
                               {"items", {{"1hswordt1", 0}}},
                               {"defenses", {{0.2, 0.1, nlohmann::json::object(), nlohmann::json::object()}}},
                               {"vitals", {{"wolf", 0}}},
                               // The chance 1: each hit that may be critical is critical.
                               {"criticals", {{"SwordT1", 1.0, 1.3}}},
                               // The light row has CanCrit 1, the burst of Gravity Well an explicit 0 (the cell of the game data).
                               {"hitRules", {{"Light", true, false, false, 1.3, 0.0}, {"GreatAxe_GravityWell_Burst", false, false, false, 1.3, 0.0}}}};
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "review-crit-combat.json";
    const auto load = [&](const nlohmann::json& content, gamedata::CombatTables& tables) {
        std::ofstream(path) << content.dump();
        const bool loaded = tables.load(path.string());
        std::filesystem::remove(path);
        return loaded;
    };
    gamedata::CombatTables tables;
    check(load(document, tables), "setup: a combat file of format 2 loads");
    PassiveShop shop;
    const gamedata::WeaponRow* sword = tables.weapon_of("1hSwordT1");
    if (sword == nullptr) {
        check(false, "setup: the weapon of the file");
        return;
    }
    const world::Critical light = shop.world().combat().critical(*sword, tables.damage_row(*tables.row_index("Light")), 0.0F, 0.0F, false, false);
    const world::Critical burst =
        shop.world().combat().critical(*sword, tables.damage_row(*tables.row_index("GreatAxe_GravityWell_Burst")), 0.0F, 0.0F, true, true);
    note("the light row: critical " + std::to_string(light.critical) + "; the burst, CanCrit 0, from behind and a headshot: critical "
         + std::to_string(burst.critical) + " (" + burst.cause + ")");
    check(light.critical, "control: a row with CanCrit 1 and the chance 1 is critical");
    check(!burst.critical, "a row with an explicit CanCrit 0 takes no critical hit: no roll, and none from behind or of a headshot");
    // Format 2: a blank cell is null; the row may be critical (an ordinary attack row of the game).
    nlohmann::json blank = document;
    blank["hitRules"][0][1] = nullptr;
    gamedata::CombatTables blankTables;
    check(load(blank, blankTables), "a file of format 2 with a blank CanCrit (null) loads");
    if (blankTables.loaded()) {
        const world::Critical blankLight =
            shop.world().combat().critical(*blankTables.weapon_of("1hSwordT1"), blankTables.damage_row(*blankTables.row_index("Light")), 0.0F, 0.0F, false, false);
        check(blankLight.critical, "a blank CanCrit cell: the row may be critical (the chance 1 gives a critical hit)");
    }
    // Format 1 (a tool from before the correction): false was a blank cell or a 0; the row may be critical.
    nlohmann::json old = document;
    old["format"] = 1;
    gamedata::CombatTables oldTables;
    check(load(old, oldTables), "a file of format 1 loads");
    if (oldTables.loaded()) {
        const world::Critical oldBurst =
            shop.world().combat().critical(*oldTables.weapon_of("1hSwordT1"), oldTables.damage_row(*oldTables.row_index("GreatAxe_GravityWell_Burst")), 0.0F,
                                           0.0F, false, false);
        check(oldBurst.critical, "format 1: false is not a prohibition (the file cannot tell a blank cell from a 0)");
    }
}

/** RV3 (F3): Vicious (Global_Weapon_DmgCrit: OnCrit, OnHeadShot, CritDamage, HeadshotDamage) and Keen Edge. */
void crit_damage_rule(const Options&) {
    PassiveShop shop;
    add_perk(shop, "Global_Weapon_DmgCrit", cells({{"OnCrit", 1}, {"OnHeadShot", 1}, {"CritDamage", 0.015F}, {"HeadshotDamage", 0.015F}}));
    shop.learn("Passive_Greataxe_Reaper_Crit");
    shop.give("TSE_CritChance_Inc_10PCT", 60, 10);
    world::Entity* wolf = shop.wolf(3);
    const world::HitResult hit = shop.hit(*wolf, "Test_Light", "TestAxe");
    note("Vicious and Keen Edge: critical " + std::to_string(hit.critical.critical) + ", multiplier " + std::to_string(hit.critical.multiplier));
    check(hit.critical.critical && near(hit.critical.multiplier, 1.465F, 1e-5F),
          "a critical hit: 1.35 (the axe) + 0.10 (Keen Edge) + 0.015 (Vicious, OnCrit): one additive pool, the row one time");
}

/** RV4 (F4): Crowded Well on the upgraded vortex (both vortex rows have the AbilityId of the rule and TrackNumInVolume). */
void crowded_upgrade(const Options&) {
    PassiveShop shop;
    shop.hold("TestAxe");
    shop.learn("Upgrade_Greataxe_GravityWell_DmgPerFoe");
    auto tables = passive_tables();
    auto upgrade = *std::find_if(tables.spells.begin(), tables.spells.end(), [](const auto& row) { return row.id == "Spell_GreatAxe_GravityWell_Vortex"; });
    upgrade.id += "_Upgrade";
    tables.spells.push_back(upgrade);
    check(shop.host.abilities.set(tables), "setup: the two vortex rows with the tracking rule of the game data");
    world::Entity* wolf = shop.wolf(20);
    world::HitContext hit;
    hit.row = "GreatAxe_GravityWell_Burst";
    hit.attackType = "Ability";
    hit.table = kGreatAxe;
    hit.kind = world::HitKind::area;
    hit.areaCount = 3;
    hit.spell = "Spell_GreatAxe_GravityWell_Vortex";
    const world::HitBonus base = shop.world().passives().before_hit(*shop.character(), *wolf, hit);
    hit.spell += "_Upgrade";
    const world::HitBonus improved = shop.world().passives().before_hit(*shop.character(), *wolf, hit);
    note("Crowded Well, three creatures in a vortex 20 m away: the base vortex " + std::to_string(base.damage) + ", the upgraded one "
         + std::to_string(improved.damage));
    check(near(base.damage, 0.3F), "control: the base vortex uses its area count");
    check(near(improved.damage, 0.3F), "the upgraded vortex uses its area count (not the creatures at the caster)");
}

/** RV5 (F5): a fixed stat of a node (PhysicalArmor 0.15 of Passive_Sword_Defender_Armor) after an assignment and a reset. */
void skill_refresh(const Options&) {
    PassiveShop shop;
    auto abilities = passive_tables();
    const char* id = "Passive_Sword_Defender_Armor";
    abilities.abilityRows.push_back({id, cells({{"PhysicalArmor", 0.15F}}, {{"WeaponTag", "Sword"}})});
    check(shop.host.abilities.set(abilities), "setup: the fixed armor rule of the sword");
    auto progression = passive_progression();
    gamedata::AbilityNode node;
    node.id = id;
    node.table = kSword;
    node.column = 100;
    progression.nodes.push_back(node);
    shop.host.progression.set(progression);
    auto catalog = shop_catalog();
    catalog.push_back(definition("TestSword", "Weapon", "Weapon+EquippableMainHand+Sword+Melee", 5, 100, 600));
    shop.host.catalog.set(catalog);
    shop.hold("TestSword");
    check(shop.world().skills().set_rank(kCharacterA, "SwordAbilityTable", gamedata::ProgressionKind::weapon, 5).ok(), "setup: the points");
    const float before = shop.character()->gear.physicalArmor;
    check(shop.world().skills().assign(kCharacterA, kSword, gamedata::progression_id(id), 1).ok(), "the assignment of the node");
    const float assigned = shop.character()->gear.physicalArmor;
    check(shop.world().skills().reset_tree(kCharacterA, kSword).ok(), "the reset of the tree");
    const float reset = shop.character()->gear.physicalArmor;
    note("the armor part: before " + std::to_string(before) + ", after the assignment " + std::to_string(assigned) + ", after the reset "
         + std::to_string(reset));
    check(near(assigned, before + 0.15F), "the assignment of the node gives the fixed armor at once");
    check(near(reset, before), "the reset of the tree takes it away at once");
}

/** RV6 (F6): Fortified Recovery (Global_Amulet_FortifyOnLow: OnHitTaken, MyHealthPercent 50 LessThan, ActivationCooldown 30). */
void taken_condition(const Options&) {
    PassiveShop shop;
    const char* rule = "Global_Amulet_FortifyOnLow";
    auto abilities = passive_tables();
    abilities.statusEffects.push_back(status("Status_Perk_Amulet_FortifyOnLow", 5, cells({{"PhysicalArmor", 0.1F}})));
    abilities.statusEffects = numbered(abilities.statusEffects);
    check(shop.host.abilities.set(abilities), "setup: the fortify status");
    add_perk(shop, rule,
             cells({{"OnHitTaken", 1}, {"RequireReaction", 1}, {"MyHealthPercent", 50}, {"ActivationCooldown", 30}, {"DoNotUnequipSelfAppliedSE", 1}},
                   {{"MyComparisonType", "LessThan"}, {"SelfApplyStatusEffect", "Status_Perk_Amulet_FortifyOnLow"}}));
    world::Entity* wolf = shop.wolf(2);
    const gamedata::CreatureContact& contact = wolf->creature.body->attacks[0].contacts[0];
    shop.character()->vitals.health = shop.character()->vitals.healthMax;
    shop.world().health().creature_hit(*wolf, contact, *shop.character());
    const float high = shop.character()->vitals.health / shop.character()->vitals.healthMax * 100.0F;
    const bool atHigh = shop.find("Status_Perk_Amulet_FortifyOnLow") != nullptr;
    // Below the threshold the rule acts: its cooldown was not spent by the hit at the high health.
    shop.character()->vitals.health = shop.character()->vitals.healthMax * 0.4F;
    shop.world().health().creature_hit(*wolf, contact, *shop.character());
    const bool atLow = shop.find("Status_Perk_Amulet_FortifyOnLow") != nullptr;
    note("Fortified Recovery: at " + std::to_string(high) + " percent: status " + std::to_string(atHigh) + "; below 50 percent: status "
         + std::to_string(atLow));
    check(high > 50.0F, "control: the hit leaves the health above the threshold");
    check(!atHigh, "a hit above 50 percent health gives no status (MyHealthPercent)");
    check(atLow, "a hit below 50 percent gives the status: the hit above did not spend the cooldown");
}

/** RV7 (F7): the order of the body and the head on the line of a projectile. */
void head_after_body(const Options&) {
    PassiveShop shop;
    world::Entity* wolf = shop.wolf(3);
    // The wolf looks north (yaw 0, crit_wolf: the head at its front). A shot from the south (behind) at the height of the neck.
    wolf->creature.yaw = 0.0F;
    const state::WorldPosition p = wolf->position;
    const state::WorldPosition from{p.x, p.y - 3.0F, p.z + 1.1F};
    const state::WorldPosition end{p.x, p.y + 3.0F, p.z + 1.1F};
    const world::WorldCapsule body = world::capsule_of(*wolf);
    const std::optional<world::WorldCapsule> head = world::head_of(*wolf);
    const std::optional<float> bodyAt = world::sweep(from, end, body, 0.05F);
    const std::optional<float> headAt = head ? world::sweep(from, end, *head, 0.05F) : std::nullopt;
    check(bodyAt && headAt && *bodyAt < *headAt, "control: the body stops this projectile before it reaches the head");
    if (!bodyAt || !headAt) {
        return;
    }
    const state::WorldPosition point{from.x, from.y + (end.y - from.y) * *bodyAt, from.z};
    const bool behind = world::head_contact(*wolf, point, {0.0F, 1.0F, 0.0F}, 0.05F);
    // A shot from the north (in front of the wolf) at the head: its first contact with the body is at the head.
    const state::WorldPosition front{p.x, p.y + 3.0F, p.z + 1.15F};
    const state::WorldPosition frontEnd{p.x, p.y - 3.0F, p.z + 1.15F};
    const std::optional<float> frontAt = world::sweep(front, frontEnd, body, 0.05F);
    const bool fromFront = frontAt && world::head_contact(*wolf, {front.x, front.y + (frontEnd.y - front.y) * *frontAt, front.z}, {0.0F, -1.0F, 0.0F}, 0.05F);
    note("from behind: the body at " + std::to_string(*bodyAt * 6.0F) + " m, the head at " + std::to_string(*headAt * 6.0F) + " m, headshot "
         + std::to_string(behind) + "; from the front: headshot " + std::to_string(fromFront));
    check(!behind, "a projectile that stops at the body from behind does not reach the head through the body");
    check(fromFront, "control: a shot at the head from the front is a headshot");
}

/** RV8 (F8): Ability_Spear_BonusHvyDmgConsec (NumConsecutiveHits 2, BaseDamage 0.1) before the damage. */
void consecutive_damage(const Options&) {
    PassiveShop shop;
    auto abilities = passive_tables();
    const char* rule = "Ability_Spear_BonusHvyDmgConsec";
    abilities.abilityRows.push_back({rule, cells({{"OnHit", 1}, {"OnBlockedHit", 1}, {"RequireReaction", 1}, {"NumConsecutiveHits", 2},
                                                  {"MaxConsecutiveHits", 3}, {"BaseDamage", 0.1F}},
                                                 {{"AttackType", "Heavy"}, {"WeaponTag", "Spear"}})});
    check(shop.host.abilities.set(abilities), "setup: the cells of the spear rule");
    auto progression = passive_progression();
    gamedata::AbilityNode node;
    node.id = rule;
    node.table = kGreatAxe; // A made-up place in the axe tree: the evaluator alone, with no spear plan.
    node.column = 100;
    progression.nodes.push_back(node);
    shop.host.progression.set(progression);
    shop.hold("TestAxe");
    shop.learn(rule);
    world::Entity* wolf = shop.wolf(3);
    world::HitContext hit;
    hit.table = kGreatAxe;
    hit.row = "Test_Heavy";
    hit.attackType = "Heavy";
    hit.attack = world::passive_rules::kAttackPress | 10;
    const world::HitBonus first = shop.world().passives().before_hit(*shop.character(), *wolf, hit);
    hit.dealt = 100.0F;
    shop.world().passives().after_hit(*shop.character(), *wolf, hit);
    hit.attack = world::passive_rules::kAttackPress | 11;
    const world::HitBonus second = shop.world().passives().before_hit(*shop.character(), *wolf, hit);
    note("the first heavy attack: damage part " + std::to_string(first.damage) + "; the second: " + std::to_string(second.damage));
    check(near(first.damage, 0.0F), "the first heavy attack has not met NumConsecutiveHits 2: no BaseDamage");
    check(near(second.damage, 0.1F), "the second heavy attack meets it: BaseDamage 0.1 (the first hit counted)");
}

/** RV9 (F9): Blood Lust (Ultimate_Greataxe_Reaper) with its tracking spell of the shape Box (Length 7). */
void box_tracking(const Options&) {
    PassiveShop shop;
    auto tables = passive_tables();
    const char* rule = "Ultimate_Greataxe_Reaper";
    const char* buff = "Status_Ultimate_Greataxe_Reaper";
    tables.abilityRows.push_back({rule, cells({{"NumAroundMe", 1}}, {{"WeaponTag", "GreatAxe"}, {"NumAroundComparisonType", "GreaterThanOrEqual"},
                                                                    {"SelfApplyStatusEffect", buff}})});
    tables.statusEffects.push_back(status(buff, -1, cells({{"DMGSlash", 0.15F}, {"SprintSpeedMod", 0.2F}, {"RemoveOnDeath", 1}})));
    tables.statusEffects = numbered(tables.statusEffects);
    tables.spells.push_back({"Spell_Ultimate_Greataxe_Reaper", 1.25F, 5, 0, 0, 0, -1,
                             cells({{"AttachToOwner", 1}, {"TrackNumInVolume", 1}, {"ClearOnExit", 1}, {"ClearOnEnd", 1}, {"Length", 7}},
                                   {{"TargetTypes", "Enemy"}, {"SpellTypes", "Ability"}, {"CastingTypes", "AoE"}, {"ShapeTypes", "Box"}, {"AbilityId", rule}})});
    check(shop.host.abilities.set(tables), "setup: the rule of Blood Lust and the dimensions of its box");
    auto progression = passive_progression();
    gamedata::AbilityNode node;
    node.id = rule;
    node.table = kGreatAxe;
    node.column = 100;
    progression.nodes.push_back(node);
    shop.host.progression.set(progression);
    shop.hold("TestAxe");
    shop.learn(rule);
    (void)shop.wolf(0, 2);
    shop.pass(1);
    const world::RuleView view = shop.world().passives().describe_row(rule, &shop.host.abilities.ability_row(rule)->cells, world::RuleOrigin::node);
    const bool applied = shop.find(buff) != nullptr;
    note("Blood Lust: listed applied " + std::to_string(view.applied) + " (" + view.note + "), status " + std::to_string(applied));
    check(!view.applied || applied, "the rule with a box volume is listed as not applied, or it gives the status for the creature in the box");
    check(!view.applied && view.note.find("Box") != std::string::npos, "the list names the shape Box as the cause");
}

/** RV10 (F10): a rule whose target status (OtherApplyStatusEffect) is not a row of the data. */
void missing_target_status(const Options& options) {
    PassiveShop shop;
    auto tables = passive_tables();
    tables.statusEffects.erase(std::remove_if(tables.statusEffects.begin(), tables.statusEffects.end(),
                                              [](const auto& row) { return row.id == "Warhammer_Pass_ExhaustOnHit"; }),
                               tables.statusEffects.end());
    tables.statusEffects = numbered(tables.statusEffects);
    gamedata::AbilityData data;
    check(!data.set(tables), "the loader refuses Exhaustive Attacks whose target status Warhammer_Pass_ExhaustOnHit is not a row (" + data.error() + ")");
    check(data.set(passive_tables()), "control: the complete data loads");
    const std::vector<std::string> lines = run_check(options, "tools/gamedata/check_ability_tool.py", (options.workDirectory / "review-crit-ability").string());
    check(passed(lines, "unknown-target-status"), "the ability tool keeps the old file for a target status that no table has");
}

/** RV11 (F11): a Pylon whose slice has no Vitals component (no slot and no metadata). */
void no_vitals_component(const Options&) {
    using namespace spell_kind;
    KindShop shop;
    auto tables = kind_tables();
    auto& slice = tables.slices[0];
    slice.vitals.reset();
    slice.slots.erase(std::remove_if(slice.slots.begin(), slice.slots.end(), [](const auto& slot) { return slot.type == 15; }), slice.slots.end());
    check(shop.host.spells.set(tables), "setup: the spell data with a Pylon slice with no Vitals component loads");
    shop.learn(*shop.character(), "TestGauntlet", kIceMagic, kPylonNode);
    const float before = shop.character()->vitals.mana;
    world::Entity* pylon = shop.cast(30);
    const float paid = before - shop.character()->vitals.mana;
    note("a Pylon with no Vitals component: made " + std::to_string(pylon != nullptr) + ", mana paid " + std::to_string(paid));
    check(pylon == nullptr && near(paid, 0.0F, 0.1F), "a summon with no damage path is not made and not paid");
}

/** RV12 (F12): a cut angle field of a DamageReceiver. */
void cut_angle(const Options& options) {
    const std::vector<std::string> lines = run_check(options, "tools/gamedata/check_hit_facts_tool.py", "");
    check(passed(lines, "cut-angle"), "the creature tool stops for an angle field of 2 bytes (not the angle 0)");
    check(passed(lines, "valid"), "control: the valid slice gives its angles and its head");
}

} // namespace

std::vector<Case> review_crit_cases() {
    return {{"RV1", "review of 3.3b-S5, F1: the mute against a move of the prefix of the user before the first open (check_dev_mute.py)", mute_prefix_swap},
            {"RV2", "review of 3.3b-S5, F2: a damage row with an explicit CanCrit 0 takes no critical hit", explicit_no_crit},
            {"RV3", "review of 3.3b-S5, F3: the CritDamage of an OnCrit rule (Vicious) joins the critical pool", crit_damage_rule},
            {"RV4", "review of 3.3b-S5, F4: Crowded Well counts the upgraded vortex", crowded_upgrade},
            {"RV5", "review of 3.3b-S5, F5: an assignment and a reset change the fixed stats at once", skill_refresh},
            {"RV6", "review of 3.3b-S5, F6: OnHitTaken checks MyHealthPercent before its status and its cooldown", taken_condition},
            {"RV7", "review of 3.3b-S5, F7: a projectile that stops at the body is not a headshot", head_after_body},
            {"RV8", "review of 3.3b-S5, F8: the damage of NumConsecutiveHits comes from the second attack", consecutive_damage},
            {"RV9", "review of 3.3b-S5, F9: a tracking spell of the shape Box is listed as not applied", box_tracking},
            {"RV10", "review of 3.3b-S5, F10: a missing target-status row is refused (loader and ability tool)", missing_target_status},
            {"RV11", "review of 3.3b-S5, F11: a Pylon with no Vitals component is not made and not paid", no_vitals_component},
            {"RV12", "review of 3.3b-S5, F12: the creature tool stops for a cut angle field (check_hit_facts_tool.py)", cut_angle}};
}

} // namespace soulwarden::backend::tests

// Cases of stage B1: the three bow abilities of the build of the user (world/abilities.h, world/projectiles.h): the ability actions of the
// projectile data (format 2), the shot items of the client for the clips and the casts of the fragments, the volley of Rapid Shot with Rapid
// Accuracy and Final Blow, the aim of Penetrating Shot and its confirm by the shot item, the pierce, the explosion of Explosive Arrow with
// Contained Detonation and the burn of Flame Arrow, the windows of a bow fragment for a different key, two sessions, the load. The data
// has the structure and the values of the game data with made-up profiles, weapons, slices, and damage types (support/bow_fixture.h). The
// proof on the tree of stage B0 builds this file with SOULWARDEN_B1_BASELINE (support/bow_fixture.h).
//
//   BW1   the projectile data of format 2: the ability actions, the clip shots, the refusals; the hit rates of the combat tables
//   BW2   Rapid Shot: three shot items at their times are three arrows of one volley; Final Blow on the third; Rapid Accuracy halves the
//         cooldown when the three arrows of one cast hit one target, not when one misses (decision D11)
//   BW3   the shot items of a volley: too early, a second item of one input step, a different attack or row, a fourth item: not a shot;
//         a repeated datagram gives no second hit
//   BW4   Penetrating Shot: the key is the aim (no cast, no cooldown); a click alone starts nothing; the shot item (or a second key) is the
//         confirm: the cooldown of the data, one arrow that pierces three wolves in the order of the line, each one time
//   BW5   the pierce: a wall stops it (no hit behind it), the range of the profile (a wolf at 45 m takes a hit, at 55 m none), Blood Soaked
//         Arrow (+0.1 for each earlier hit of the projectile, the row A1)
//   BW6   Explosive Arrow: the shot item 0.4667 s after the key, the direct hit and the explosion entity of the slice with its area (2.5 m;
//         1.25 m with Contained Detonation), a wall or the ground explodes, the end of the range does not; the burn of Flame Arrow only on
//         the direct target, with its icon and its ticks
//   BW7   the windows of a bow fragment: a key of a different ability before the buffered window is refused (the aim stays), in the buffered
//         window it starts at the instant transition (its cooldown and its shot later), after it at once
//   BW8   a character with no client: the server shoots the volley at its times; Penetrating Shot casts at once
//   BW9   two sessions and two casters (MP-10): each volley is its own; the observer gets the health, the record of the explosion, and the
//         icon of the burn, and no number and no cooldown of the other
//   BW10  the load: a pierce through 40 near creatures and 20 explosions with 4,000 far creatures look only at the near ones

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "support/bow_fixture.h"
#include "support/cases.h"
#include "support/check.h"

namespace soulwarden::backend::tests {
namespace {

using namespace bow;
using nlohmann::json;
using std::chrono::milliseconds;

/** @return The status numbers of the compact map of a status fragment of a creature (the form of case AB9). */
std::vector<std::uint16_t> compact_statuses(const Fragment& fragment) {
    std::vector<std::uint16_t> entries;
    const auto found = fragment.fields.find({3, 1});
    if (found == fragment.fields.end()) {
        return entries;
    }
    server::hub::Reader reader(found->second);
    const auto count = progress_wire::read_full_form(reader);
    for (std::uint32_t index = 0; count && index < *count; ++index) {
        entries.push_back(reader.u16());
        reader.u8();
    }
    return entries;
}

/** @return True when a client got a compact status map of a creature with this status number from the message `from` on. */
bool icon_sent(const FakeClient& client, std::uint16_t status, std::size_t from) {
    for (const Fragment& fragment : fragments_of(client, kStatusSlot, 4236, from)) {
        const std::vector<std::uint16_t> entries = compact_statuses(fragment);
        if (std::find(entries.begin(), entries.end(), status) != entries.end()) {
            return true;
        }
    }
    return false;
}

std::size_t damage_messages(const FakeClient& client, std::size_t from) {
    std::size_t count = 0;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        count += client.got[index].type == server::rep::kTypeDamageDealt ? 1 : 0;
    }
    return count;
}

/** @return The index of the first log line of the current case from `from` on with the text, or the count of the lines. */
std::size_t line_of(const std::string& text, std::size_t from = 0) {
    const std::vector<std::string>& lines = log_lines();
    for (std::size_t index = from; index < lines.size(); ++index) {
        if (lines[index].find(text) != std::string::npos) {
            return index;
        }
    }
    return lines.size();
}

std::string hex_of(world::EntityId id) {
    char text[32];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(id));
    return text;
}

/** BW1: the projectile data of format 2 and the hit rates of the combat tables. */
void bow_data(const Options& options) {
#ifndef SOULWARDEN_B1_BASELINE
    gamedata::ProjectileData data;
    check(data.set(bow_projectiles()) && data.loaded(), "the data with the ability actions is valid");
    const gamedata::AbilityActionData* rapid = data.action("bow_rapidshot");
    check(rapid != nullptr && rapid->ability == kRapid && rapid->clips.size() == 3 && near(rapid->clips[2].time, 1.7333F)
              && rapid->clips[2].row == "Bow_RapidAttack_KnockBack" && near(rapid->canTransition, 2.6F),
          "an ability action by its name (the case of the letters is ignored): its clips and its CanTransition");
    const gamedata::ShotRule* clip = data.ability_clip("TestBowProfile", crc("tripleshot"), crc("bow_rapidattack_knockback"));
    check(clip != nullptr && clip->ability && clip->spell.empty() && clip->slice == -1 && data.shots("TestBowProfile", crc("tripleshot")).empty()
              && data.ability_clip("TestBowProfile", crc("tripleshot"), crc("bowattack1")) == nullptr,
          "a clip shot of an ability by its attack and its row; the shots of a basic attack do not have it");
    gamedata::ProjectileTables noAction = bow_projectiles();
    noAction.actions.pop_back();
    gamedata::ProjectileTables notShot = bow_projectiles();
    notShot.actions.back().clips.front().row = "Unknown_Row";
    gamedata::ProjectileTables twice = bow_projectiles();
    twice.actions.push_back(twice.actions.front());
    gamedata::ProjectileData wrong;
    check(!wrong.set(noAction) && !wrong.set(notShot) && !wrong.set(twice) && !wrong.loaded(),
          "a clip shot that no action names, an action clip that is not a shot, and two actions with one name are refused (as a whole)");
#endif
    // The file of the tool: format 2 has the list "abilityActions"; a file of format 1 loads with none.
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "bow-projectile-data.json";
    json document{{"format", 2},
                  {"counts", {{"attacks", 1}, {"slices", 1}, {"ammo", 1}, {"weapons", 1}, {"shots", 2}, {"abilityActions", 1}}},
                  {"attacks", {{"P", "TripleShot", false, 100.0, 0.275, 2.0, 0.2, 20.0, 30.0, 80.0, 1.0, 0, true, true, {0.0, 0.0, -9.8}, 0}}},
                  {"slices", {{"slices/test/arrow.dynamicslice", 100.0, 25.0, 1.0, {0.0, 0.0, -9.8}, 10.0, 8.0}}},
                  {"ammo", {{"DefaultArrow", "Arrow", 1.2, 0}}},
                  {"weapons", {{"W", "P", "Arrow"}}},
                  {"shots", {{"P", "TripleShot", "Row_A", 0.0, -1, "", "", false, 12, true, true},
                             {"P", "TripleShot", "Row_B", 0.0, -1, "", "", false, 12, false, true}}},
                  {"abilityActions", {{"Test_Rapid", "Test_Ability", "Test_Fragment", "Bow", 2.6,
                                       {{0.3333, "TripleShot", "Row_A", 0.0, true}, {1.7333, "TripleShot", "Row_B", 0.0, false}},
                                       json::array({json::array({0.4667, "Test_Spell", {"Node_A"}, json::array()})}),
                                       {{"Test_Other", "buffered", 1.8681}, {"Test_Other", "instant", 2.6}}}}}};
    const auto load = [&path](const json& content, gamedata::ProjectileData& out) {
        std::ofstream(path, std::ios::binary | std::ios::trunc) << content.dump();
        return out.load(path.string());
    };
    gamedata::ProjectileData file;
#ifndef SOULWARDEN_B1_BASELINE
    check(load(document, file) && file.action("Test_Rapid") != nullptr && file.action("Test_Rapid")->transitions.size() == 2
              && file.action("Test_Rapid")->transitions[0].buffered && file.action("Test_Rapid")->casts.front().needs.front() == "Node_A",
          "a file of format 2 of the form of the tool loads");
#else
    check(load(document, file), "a file of format 2 of the form of the tool loads");
#endif
    json noList = document;
    noList.erase("abilityActions");
    json badKind = document;
    badKind["abilityActions"][0][7][0][1] = "later";
    json formatOne = document;
    formatOne["format"] = 1;
    formatOne.erase("abilityActions");
    formatOne["counts"].erase("abilityActions");
    gamedata::ProjectileData other;
    check(!load(noList, other) && !load(badKind, other) && !load(formatOne, other),
          "format 2 with no list of actions and a transition of a different kind are refused; format 1 with a clip shot of an ability "
          "is refused (no action names it)");
    formatOne["shots"] = {{"P", "TripleShot", "Row_A", 0.0, -1, "", "", false, 12, true}};
    formatOne["counts"]["shots"] = 1;
    // The member hitRates of the combat file (the form of the tool): a file with no member loads (each row has the part 0).
    {
        const auto combatPath = options.workDirectory / "bow-combat-tables.json";
        json combat{{"format", 1},
                    {"constants", {{"levelDamage", 0.025}, {"gearScoreRate", 0.0112}, {"gearScoreSmallerRate", 0.6667}, {"gearScoreInterval", 5},
                                   {"gearScoreMinimum", 100}, {"gearScoreLimit", 500}}},
                    {"damageTypes", {{"Slash", 1, "Physical"}}},
                    {"attributes", {{"minimum", 0}, {"strength", {0.0}}, {"dexterity", {0.0}}, {"intelligence", {0.0}}, {"focus", {0.0}}}},
                    {"damageRows", {{"Test_Light", "Bow", "Light", "Slash", 1.0, 0.0}, {"Test_Heavy", "Bow", "Heavy", "Slash", 1.2, 0.0},
                                    {"Test_Pierce_A1", "Bow", "Ability", "Slash", 1.5, 0.0}, {"Test_Pierce_A2", "Bow", "Ability", "Slash", 1.5, 0.0}}},
                    {"weapons", {{"TestBow", "Combat Ranged", "Bow", 64.0, 0.01, 0.0, 0.0, 0.0, "Test_Light", "Test_Heavy", 0.0}}},
                    {"items", {{"testbow", 0}}},
                    {"defenses", json::array()},
                    {"vitals", json::object()}};
        const auto loadCombat = [&combatPath](const json& content, gamedata::CombatTables& tables) {
            std::ofstream(combatPath, std::ios::binary | std::ios::trunc) << content.dump();
            return tables.load(combatPath.string());
        };
        gamedata::CombatTables none;
        check(loadCombat(combat, none), "a combat file with no member hitRates loads (a tool from before the stage)");
        combat["hitRates"] = {{"Test_Pierce_A1", 0.1, 0.5}, {"Test_Pierce_A2", -0.1, -0.5}};
        gamedata::CombatTables rates;
        bool values = loadCombat(combat, rates);
#ifndef SOULWARDEN_B1_BASELINE
        values = values && near(rates.damage_row(*rates.row_index("Test_Pierce_A1")).hitRate, 0.1F)
                 && near(rates.damage_row(*rates.row_index("Test_Pierce_A1")).hitRateMost, 0.5F)
                 && near(rates.damage_row(*rates.row_index("Test_Pierce_A2")).hitRateMost, -0.5F)
                 && rates.damage_row(*rates.row_index("Test_Light")).hitRate == 0.0F;
#endif
        check(values, "the member hitRates loads each pair of its rows (a negative pair too); a row with no pair has 0");
        json unknown = combat;
        unknown["hitRates"].push_back({"Test_Not_A_Row", 0.1, 0.5});
        json twice = combat;
        twice["hitRates"].push_back({"Test_Pierce_A1", 0.2, 0.5});
        json text = combat;
        text["hitRates"][0] = {"Test_Pierce_A1", "0.1", 0.5};
        gamedata::CombatTables wrong;
        check(!loadCombat(unknown, wrong) && !loadCombat(twice, wrong) && !loadCombat(text, wrong),
              "a hit rate of a row that the file does not have, of a row two times, or with a text value: the file does not load");
    }
    gamedata::ProjectileData old;
#ifndef SOULWARDEN_B1_BASELINE
    check(load(formatOne, old) && old.tables().actions.empty() && old.action("Test_Rapid") == nullptr,
          "a file of format 1 (an older tool) loads with no ability action: the press of such an ability is refused (no rule)");
    // The hit rates of the combat tables (the member hitRates of the tool).
    BowShop shop;
    const gamedata::DamageRow& a1 = shop.host.combat.damage_row(*shop.host.combat.row_index("Bow_Penetrating_AttackA1"));
    check(near(a1.hitRate, 0.1F) && near(a1.hitRateMost, 0.5F), "the hit rate cells of a damage row");
#else
    check(load(formatOne, old), "a file of format 1 (an older tool) loads");
    check(false, "the hit rate cells of a damage row (the tree before the stage has no hit rate)");
    BowShop shop;
#endif
    shop.learn(*shop.character());
    // The list of the passive rules (tools/world passives): Contained Detonation selects the cast of the upgrade spell (not the base cast
    // that its absence selects); the node rows of Rapid Shot are rules of its arrows.
    {
        const world::Passives& passives = shop.host.world.passives();
        const gamedata::AbilityRow* contained = shop.host.abilities.ability_row(kContained);
        const world::RuleView view = passives.describe_row(kContained, contained != nullptr ? &contained->cells : nullptr, world::RuleOrigin::node);
        check(view.applied && view.note.find("Spell_Ability_Bow_Explosive_Upgrade with the node") != std::string::npos,
              "the rule list: Contained Detonation casts the upgrade spell: " + view.note);
        const gamedata::AbilityRow* accuracy = shop.host.abilities.ability_row(kRapidAccuracy);
        const world::RuleView rapid = passives.describe_row(kRapidAccuracy, accuracy != nullptr ? &accuracy->cells : nullptr, world::RuleOrigin::node);
        check(rapid.applied, "the rule list: Rapid Accuracy is a rule of the arrows of Rapid Shot: " + rapid.note);
    }
    BowShop noData(false, true, false);
    noData.learn(*noData.character());
    noData.press(2);
    noData.walker.idle(3);
    check(noData.presses(world::CastVerdict::noRule) == 1 && noData.cooldown(kRapid) == nullptr,
          "with no ability action in the data the press of Rapid Shot is refused (no rule): no cast, no cooldown");
}

/** BW2: Rapid Shot, its volley, Final Blow, Rapid Accuracy. */
void rapid_shot(const Options&) {
    const auto volley = [](bool finalBlow, bool accuracy, bool missSecond) {
        BowShop shop;
        std::vector<const char*> nodes;
        if (finalBlow) {
            nodes.push_back(kFinalBlow);
        }
        if (accuracy) {
            nodes.push_back(kRapidAccuracy);
        }
        shop.learn(*shop.character(), nodes);
        const world::EntityId wolf = shop.wolf_at(10.0F)->id;
        std::vector<float> lost;
        shop.press(2);
        shop.item_at(9, BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard"));
        shop.walker.idle(8);
        lost.push_back(shop.lost(wolf));
        shop.item_at(25, BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard", missSecond ? std::array<float, 3>{0.0F, 1.0F, 0.0F}
                                                                                                 : std::array<float, 3>{1.0F, 0.0F, 0.0F}));
        shop.walker.idle(8);
        lost.push_back(shop.lost(wolf) - lost[0]);
        shop.until(52);
        const float before = shop.remaining(kRapid);
        shop.send_item(BowShop::bow_item("TripleShot", "Bow_RapidAttack_KnockBack"));
        shop.walker.idle(8);
        lost.push_back(shop.lost(wolf) - lost[0] - lost[1]);
        const float after = shop.remaining(kRapid);
        struct Result {
            std::vector<float> lost;
            float before{0.0F};
            float after{0.0F};
            std::uint64_t shots{0};
        };
        return Result{lost, before, after, shop.volley_shots()};
    };
    const auto plain = volley(false, false, false);
    check(plain.shots == 3 && plain.lost[0] > 0.0F && near(plain.lost[1], plain.lost[0], 1.0F) && plain.lost[2] > plain.lost[0] * 1.2F
              && plain.lost[2] < plain.lost[0] * 1.3F,
          "three shot items at 9, 25, 52 input steps fly as three arrows: the rows Standard, Standard, KnockBack (1.0, 1.0, 1.25)");
    note("arrows " + std::to_string(plain.lost[0]) + " " + std::to_string(plain.lost[1]) + " " + std::to_string(plain.lost[2]));
    check(plain.before > 12.0F && plain.before < kRapidCooldown && std::fabs(plain.after - (plain.before - 0.3F)) < 0.1F,
          "the cooldown of the data (14 s) runs from the press");
    const auto blow = volley(true, false, false);
    check(near(blow.lost[0], plain.lost[0], 1.0F) && near(blow.lost[2], plain.lost[2] * 1.25F, 1.0F),
          "Final Blow: the third arrow (the row KnockBack) has BaseDamage 0.25 more; the first two arrows do not");
    const auto accurate = volley(false, true, false);
    check(std::fabs(accurate.after - (accurate.before - 0.3F - 0.5F * kRapidCooldown)) < 0.15F,
          "Rapid Accuracy: the three arrows of one cast hit one target: the cooldown is 50 percent of the data (7 s) shorter");
    const auto missed = volley(false, true, true);
    check(missed.lost[1] == 0.0F && std::fabs(missed.after - (missed.before - 0.3F)) < 0.1F,
          "Rapid Accuracy: the second arrow misses (it flies north): two hits of the cast on the target, no change of the cooldown");
    // Two casts: two hits of one cast and one hit of the next cast are not three arrows of one cast (decision D11).
    {
        BowShop shop;
        shop.learn(*shop.character(), {kRapidAccuracy});
        shop.wolf_at(10.0F);
        shop.press(2);
        shop.item_at(9, BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard"));
        shop.item_at(25, BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard"));
        shop.item_at(52, BowShop::bow_item("TripleShot", "Bow_RapidAttack_KnockBack", {0.0F, 1.0F, 0.0F}));
        shop.walker.idle(40);
        shop.host.world.abilities().reset_cooldowns(kCharacterA, true);
        shop.walker.idle(5);
        shop.press(2);
        shop.item_at(9, BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard"));
        shop.walker.idle(8);
        const float remaining = shop.remaining(kRapid);
        check(remaining > kRapidCooldown - 1.0F && shop.host.world.passives().counts().cooldownReductions == 0,
              "the hits of two casts (2 and 1) do not give Rapid Accuracy: the hits of one cast count");
    }
}

/** BW3: the shot items of a volley. */
void volley_items(const Options&) {
    BowShop shop;
    shop.learn(*shop.character());
    const world::EntityId wolf = shop.wolf_at(10.0F)->id;
    // The input step of the press has the item: the clip is at 0.3333 s, the tolerance 0.25 s: too early.
    {
        InputEntry entry = shop.walker.entry({kAbilityBits[2], kVirtualBit});
        entry.shotItems = {BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard")};
        shop.walker.send(entry);
        shop.pressInput = shop.walker.input;
        shop.walker.bits({kHoldBit});
        shop.walker.bits({kReleaseBit});
    }
    shop.until(9);
    check(shop.volley_shots() == 0 && shop.lost(wolf) == 0.0F,
          "a shot item before the time of its clip (less 0.25 s) is not a shot of the volley");
    // At its time, two items in one input step: one arrow.
    {
        InputEntry entry = shop.walker.entry();
        entry.shotItems = {BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard"), BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard")};
        shop.walker.send(entry);
    }
    shop.walker.idle(6);
    const float one = shop.lost(wolf);
    check(shop.volley_shots() == 1 && one > 0.0F,
          "two items in one input step give one arrow (a second item of the step is not a new shot)");
    // A different row (KnockBack before the second Standard) and a different attack are not the next shot.
    shop.item_at(24, BowShop::bow_item("TripleShot", "Bow_RapidAttack_KnockBack"));
    shop.send_item(BowShop::bow_item("PenetratingShot", "Bow_Penetrating_Attack"));
    shop.walker.idle(1);
    check(shop.volley_shots() == 1 && near(shop.lost(wolf), one, 0.5F),
          "an item with the row of a later clip or a different attack is not the next shot of the volley");
    shop.item_at(27, BowShop::bow_item("TripleShot", "Bow_RapidAttack_Standard"));
    shop.item_at(52, BowShop::bow_item("TripleShot", "Bow_RapidAttack_KnockBack"));
    shop.item_at(58, BowShop::bow_item("TripleShot", "Bow_RapidAttack_KnockBack"));
    shop.walker.idle(10);
    check(shop.volley_shots() == 3, "the volley takes three shots; a fourth item is not a shot");
    // A repeated datagram: the steps of the reports are newest first and each step counts one time (the session), thus the input step of a
    // shot that comes again gives no second hit.
    const float three = shop.lost(wolf);
    shop.walker.send(shop.walker.history.front());
    shop.walker.idle(10);
    check(near(shop.lost(wolf), three, 0.5F) && shop.volley_shots() == 3,
          "a report that comes again with the shot item gives no second shot and no second hit");
}


/** BW4: Penetrating Shot: the aim, the confirm by the shot item, the pierce in the order of the line. */
void penetrating_shot(const Options&) {
    const InputShotItem item = BowShop::bow_item("PenetratingShot", "Bow_Penetrating_Attack");
    {
        BowShop shop;
        shop.learn(*shop.character());
        std::vector<world::EntityId> wolves;
        for (const float east : {5.0F, 10.0F, 15.0F}) {
            wolves.push_back(shop.wolf_at(east)->id);
        }
        shop.press(0);
        shop.walker.idle(5);
        const world::AbilityCounts& counts = shop.host.world.abilities().counts();
        check(counts.aims == 1 && shop.cooldown(kPenetrating) == nullptr && shop.volley_shots() == 0,
              "the key Q starts the aim of the client: no cast, no cooldown, no shot (live check B1-I1: the actions Ready and Aim)");
        // A click alone in the aim starts nothing: the shot item of the shoot action of the client is the confirm.
        shop.walker.bits({kActionAttack});
        shop.walker.idle(3);
        check(shop.cooldown(kPenetrating) == nullptr && counts.confirms == 0 && shop.shot_confirms() == 0,
              "a click with no shot item does not confirm the aim (no cast, no cooldown)");
        const std::size_t lines = log_lines().size();
        shop.send_item(item, {kActionAttack});
        shop.walker.idle(10);
        const float remaining = shop.remaining(kPenetrating);
        check(shop.shot_confirms() == 1 && shop.volley_shots() == 1 && remaining > kPenetratingCooldown - 0.5F && remaining <= kPenetratingCooldown,
              "the shot item confirms the aim: the cast, its cooldown of the data (18 s) from the shot, and one arrow");
        const float first = shop.lost(wolves[0]);
        check(first > 0.0F && near(shop.lost(wolves[1]), first, 1.0F) && near(shop.lost(wolves[2]), first, 1.0F),
              "the arrow passes through the three wolves on its line: each one takes one hit of the row (1.5)");
        const std::size_t a = line_of("pierces creature " + hex_of(wolves[0]), lines);
        const std::size_t b = line_of("pierces creature " + hex_of(wolves[1]), lines);
        const std::size_t c = line_of("pierces creature " + hex_of(wolves[2]), lines);
        check(a < b && b < c && c < log_lines().size() && shop.pierce_hits() == 3,
              "the hits come in the order of the line (5 m, 10 m, 15 m), one time for each wolf");
        shop.walker.idle(30);
        check(near(shop.lost(wolves[2]), first, 1.0F) && shop.pierce_hits() == 3,
              "no second hit of one wolf by the same arrow (a later segment touches the same capsule)");
    }
    {
        // A second key Q in the aim: the shot item comes in the step of the second key (live check B1-I1).
        BowShop shop;
        shop.learn(*shop.character());
        const world::EntityId wolf = shop.wolf_at(8.0F)->id;
        shop.press(0);
        shop.walker.idle(10);
        InputEntry entry = shop.walker.entry({kAbilityBits[0], kVirtualBit});
        entry.shotItems = {item};
        shop.walker.send(entry);
        shop.walker.bits({kHoldBit});
        shop.walker.bits({kReleaseBit});
        shop.walker.idle(5);
        check(shop.shot_confirms() == 1 && shop.lost(wolf) > 0.0F && shop.cooldown(kPenetrating) != nullptr,
              "a second key in the aim with the shot item confirms the aim");
    }
    {
        // A key of a different ability in its own cooldown of the client: the client keeps its aim (live check), and the server too.
        BowShop shop;
        shop.learn(*shop.character());
        const world::EntityId wolf = shop.wolf_at(8.0F)->id;
        shop.press(1);
        shop.walker.idle(60);
        shop.press(0);
        shop.walker.idle(5);
        shop.press(1);
        shop.walker.idle(3);
        check(shop.presses(world::CastVerdict::cooldown) == 1, "setup: the key R in the cooldown of Explosive Arrow is refused");
        shop.send_item(item, {kActionAttack});
        shop.walker.idle(5);
        check(shop.shot_confirms() == 1 && shop.lost(wolf) > 0.0F,
              "the aim stays after a refused key that the client does not play: its shot item confirms it");
    }
    {
        // A key of a different ability that the client plays ends the aim: a later Penetrating item is no confirm.
        BowShop shop;
        shop.learn(*shop.character());
        shop.press(0);
        shop.walker.idle(5);
        shop.press(1);
        shop.walker.idle(60);
        shop.send_item(item, {kActionAttack});
        shop.walker.idle(5);
        check(shop.shot_confirms() == 0 && shop.cooldown(kPenetrating) == nullptr,
              "a key that the client plays (Explosive Arrow) ends the aim: no confirm after it");
    }
}

/** BW5: the pierce: a wall, the range of the profile, Blood Soaked Arrow. */
void pierce_rules(const Options& options) {
    const InputShotItem item = BowShop::bow_item("PenetratingShot", "Bow_Penetrating_Attack");
    const auto shoot = [&](BowShop& shop) {
        shop.press(0);
        shop.walker.idle(5);
        shop.send_item(item, {kActionAttack});
        shop.walker.idle(25);
    };
    {
        BowShop shop;
        shop.wall_east(options.workDirectory / "bow-wall", 7.0F, 7.5F);
        shop.learn(*shop.character());
        const world::EntityId before = shop.wolf_at(5.0F)->id;
        const world::EntityId behind = shop.wolf_at(10.0F)->id;
        shoot(shop);
        check(shop.lost(before) > 0.0F && shop.lost(behind) == 0.0F && shop.host.world.projectiles().counts().ends[static_cast<std::size_t>(world::ProjectileEnd::wall)] == 1,
              "a wall stops the pierce: the wolf before it takes the hit, the wolf behind it none");
    }
    {
        BowShop shop;
        shop.learn(*shop.character());
        const world::EntityId inside = shop.wolf_at(45.0F)->id;
        const world::EntityId outside = shop.wolf_at(55.0F)->id;
        shoot(shop);
        check(shop.lost(inside) > 0.0F && shop.lost(outside) == 0.0F
                  && shop.host.world.projectiles().counts().ends[static_cast<std::size_t>(world::ProjectileEnd::range)] == 1,
              "the range of the profile PenetratingShot (50 m, game data; the text says 100 m): a wolf at 45 m takes a hit, at 55 m none");
    }
    {
        BowShop shop;
        shop.learn(*shop.character(), {kBloodSoaked});
        std::vector<world::EntityId> wolves;
        for (int index = 1; index <= 7; ++index) {
            wolves.push_back(shop.wolf_at(3.0F * static_cast<float>(index))->id);
        }
        shoot(shop);
        const float first = shop.lost(wolves[0]);
        bool rising = first > 0.0F;
        for (std::size_t index = 0; index < wolves.size(); ++index) {
            const float part = std::min(0.1F * static_cast<float>(index), 0.5F);
            rising = rising && near(shop.lost(wolves[index]), first * (1.0F + part), 1.0F);
        }
        check(rising, "Blood Soaked Arrow: the row Bow_Penetrating_AttackA1, each earlier hit of the arrow adds 0.1 to its damage, at most 0.5");
    }
}

/** BW6: Explosive Arrow, its explosion, Contained Detonation, the burn of Flame Arrow. */
void explosive_arrow(const Options& options) {
    const InputShotItem east = BowShop::bow_item("ExplosionShot", nullptr, {1.0F, 0.0F, 0.0F}, nullptr);
    {
        BowShop shop;
        shop.learn(*shop.character(), {kFlameArrow});
        const world::EntityId direct = shop.wolf_at(10.0F)->id;
        // The impact on the capsule of the direct wolf is about 8.5 m east of A (the radius of the capsule, of the projectile, and the
        // tolerance); a wolf 2 m to its side is in the explosion (2.5 m) and not on the line of the arrow.
        const world::EntityId side = shop.wolf_at(8.5F, 2.0F)->id;
        const std::size_t from = shop.a.got.size();
        // The item in the input step of the key is too early (the cast at 0.4667 s, the tolerance 0.25 s).
        InputEntry early = shop.walker.entry({kAbilityBits[1], kVirtualBit});
        early.shotItems = {east};
        shop.walker.send(early);
        shop.pressInput = shop.walker.input;
        shop.walker.bits({kHoldBit});
        shop.walker.bits({kReleaseBit});
        check(shop.volley_shots() == 0, "a shot item in the step of the key is too early for the cast at 0.4667 s");
        check(shop.remaining(kExplosive) > kExplosiveCooldown - 0.5F, "the cooldown of the data (22 s) from the key");
        shop.item_at(13, east);
        shop.walker.idle(6);
        const world::AbilityCounts& counts = shop.host.world.abilities().counts();
        check(shop.volley_shots() == 1 && shop.explosions() == 1, "the shot item 13 input steps after the key (live check) flies, and its hit explodes");
        const float sideLost = shop.lost(side);
        const float directLost = shop.lost(direct);
        check(sideLost > 0.0F && directLost > sideLost && counts.areaHits == 2,
              "the direct target takes the direct hit and the explosion (the impact is on the surface of its capsule: the area takes a body that "
              "reaches into its sphere), the wolf in the area the explosion");
        check(shop.has_effect(direct, kBurn) && !shop.has_effect(side, kBurn),
              "Flame Arrow: the burn comes only from the direct row (Bow_ExplosionShot_Projectile), not from the explosion");
        const gamedata::StatusRow* burn = shop.host.abilities.status(kBurn);
        check(burn != nullptr && icon_sent(shop.a, burn->index, from), "the client of A gets the icon of the burn at the name plate (the compact map)");
        const std::vector<world::Entity*> spells = shop.spells();
        check(spells.size() == 1 && spells.front()->spell.spellId == kExplosion
                  && !fragments_of(shop.a, kExplosionSpellSlot, server::rep::kTypeSpellState, from).empty(),
              "the explosion is a spell entity of its slice (the record that the client shows: live check B1-I1)");
        shop.pass(2.1F);
        check(shop.lost(direct) > directLost && shop.damage_ticks() >= 2 && shop.lost(side) == sideLost,
              "the burn ticks each 1 s on the direct target with the damage of its row; the wolf in the area takes no tick");
        check(shop.spells().empty(), "the explosion entity ends after the Duration of its spell row (1 s)");
    }
    const auto ground = [&](bool contained) {
        BowShop shop;
        shop.wall_east(options.workDirectory / (contained ? "bow-ground-small" : "bow-ground"), 0.0F, 0.0F);
        shop.learn(*shop.character(), contained ? std::vector<const char*>{kContained} : std::vector<const char*>{});
        std::vector<world::EntityId> wolves;
        for (const float north : {1.0F, -2.0F, 3.2F}) {
            wolves.push_back(shop.wolf_at(8.0F, north)->id);
        }
        shop.press(1);
        shop.item_at(13, BowShop::bow_item("ExplosionShot", nullptr, {7.5F, 0.0F, -0.6F}, nullptr));
        shop.walker.idle(8);
        std::vector<float> lost;
        for (const world::EntityId id : wolves) {
            lost.push_back(shop.lost(id));
        }
        lost.push_back(static_cast<float>(shop.explosions()));
        return lost;
    };
    const std::vector<float> base = ground(false);
    check(base[3] == 1.0F && base[0] > 0.0F && base[1] > 0.0F && base[2] == 0.0F,
          "a collision with the ground explodes: the wolves at 1.0 m and 2.0 m of the impact take the explosion (2.5 m), the wolf at 3.2 m none");
    const std::vector<float> small = ground(true);
    check(small[3] == 1.0F && small[0] > base[0] && small[1] == 0.0F && small[2] == 0.0F,
          "Contained Detonation: the small explosion (1.25 m, the row 1.7): the wolf at 1.0 m takes more, the wolf at 2.0 m none");
    {
        BowShop shop;
        shop.learn(*shop.character());
        shop.press(1);
        shop.item_at(13, BowShop::bow_item("ExplosionShot", nullptr, {0.0F, 0.0F, 1.0F}, nullptr));
        shop.pass(1.5F);
        check(shop.volley_shots() == 1 && shop.explosions() == 0,
              "the end of the range (up into the sky) is no collision: no explosion (SpawnSecondaryOnCollision)");
    }
}

/** BW7: the windows of a bow fragment for a key of a different ability. */
void bow_windows(const Options&) {
    {
        BowShop shop;
        shop.learn(*shop.character());
        shop.press(2);
        shop.until(20);
        shop.press(1);
        shop.walker.idle(3);
        check(shop.presses(world::CastVerdict::casting) == 1 && shop.cooldown(kExplosive) == nullptr,
              "the key R at 0.67 s of Rapid Shot (before its buffered window at 1.8681 s) is refused: no cast, no cooldown (live check)");
    }
    {
        BowShop shop;
        shop.learn(*shop.character());
        const world::EntityId wolf = shop.wolf_at(10.0F)->id;
        const std::uint16_t rapid = static_cast<std::uint16_t>(shop.walker.input + 1);
        shop.press(2);
        shop.until(60);
        shop.press(1);
        const float remaining = shop.remaining(kExplosive);
        check(shop.window_delays() == 1 && remaining > kExplosiveCooldown + 0.4F && remaining < kExplosiveCooldown + 0.7F,
              "the key R at 2.0 s (in the buffered window) starts at the instant transition (2.6 s): its cooldown starts there");
        const float before = shop.lost(wolf);
        shop.item_at(13, BowShop::bow_item("ExplosionShot", nullptr, {1.0F, 0.0F, 0.0F}, nullptr));
        shop.walker.idle(2);
        check(shop.volley_shots() == 0 && shop.lost(wolf) == before,
              "its shot item 13 steps after the key is too early: the cast is 0.6 s later");
        shop.item_at(31, BowShop::bow_item("ExplosionShot", nullptr, {1.0F, 0.0F, 0.0F}, nullptr));
        shop.walker.idle(5);
        check(shop.volley_shots() == 1 && shop.lost(wolf) > before,
              "the shot item at the time of the cast after the instant transition flies (live check: 13 steps after 2.6 s)");
        (void)rapid;
    }
    {
        BowShop shop;
        shop.learn(*shop.character());
        shop.press(2);
        shop.until(80);
        shop.press(1);
        const float remaining = shop.remaining(kExplosive);
        check(shop.window_delays() == 0 && remaining > kExplosiveCooldown - 0.2F && remaining <= kExplosiveCooldown,
              "the key R after the instant transition (2.67 s) starts at the press");
    }
}

/** BW8: a character with no client. */
void server_volleys(const Options&) {
    BowShop shop;
    const auto beta = add_player(shop.host.store, "76561202255233019", "Tester B", kCharacterB, "Beta", at(kPlace.x, kPlace.y + 10.0F));
    check(shop.world().spawn_character(kCharacterB, at(kPlace.x, kPlace.y + 10.0F)) == world::SpawnError::none, "setup: B with no client");
    shop.learn(*shop.world().find_character(kCharacterB));
    std::vector<world::EntityId> wolves;
    for (const float east : {6.0F, 12.0F}) {
        wolves.push_back(shop.host.world.creatures().spawn("Test_Wolf", at(kPlace.x + east, kPlace.y + 10.0F), 90.0F).entity->id);
    }
    world::AbilityPress press;
    press.place = 0;
    press.yaw = -1.5708F; // East (the yaw from north, positive to the left).
    check(shop.world().abilities().press_character(kCharacterB, press) == world::CastVerdict::accepted, "Penetrating Shot of a character with no client");
    shop.walker.idle(10);
    check(shop.lost(wolves[0]) > 0.0F && shop.lost(wolves[1]) > 0.0F && shop.cooldown(kPenetrating, kCharacterB) != nullptr,
          "the cast at once (no aim with no client), and the server shoots the arrow: it pierces both wolves");
    press.place = 2;
    const float before = shop.lost(wolves[0]);
    check(shop.world().abilities().press_character(kCharacterB, press) == world::CastVerdict::accepted, "Rapid Shot of a character with no client");
    shop.walker.idle(70);
    check(shop.volley_shots() == 4 && shop.lost(wolves[0]) > before,
          "the server shoots the three arrows of the volley at their times");
}

/** BW9: two sessions and two casters (MP-10). */
void two_bows(const Options&) {
    BowShop shop(true);
    shop.enter_b();
    shop.learn(*shop.character(), {kRapidAccuracy, kFlameArrow});
    shop.learn(*shop.world().find_character(kCharacterB), {kRapidAccuracy});
    Walker walkerB{shop.host, shop.b, at(kPlace.x - 1.0F, kPlace.y + 1.0F)};
    walkerB.input = 400;
    const world::EntityId wolfA = shop.wolf_at(10.0F)->id;
    const world::EntityId wolfB = shop.wolf_at(9.0F, 1.0F + 6.0F)->id;
    walkerB.idle(5);
    const std::size_t fromA = shop.a.got.size();
    const std::size_t fromB = shop.b.got.size();
    // A and B each cast Rapid Shot; each one sends its items in its own input steps. The items of B do not fit the cast of A.
    shop.press(2);
    walkerB.press(2);
    const std::uint16_t pressB = static_cast<std::uint16_t>(walkerB.input - 2);
    const auto itemB = [&](const char* row) {
        InputShotItem shot = BowShop::bow_item("TripleShot", row, {1.0F, 0.6F, 0.0F});
        shot.origin = {kPlace.x - 0.5F, kPlace.y + 1.0F, kHeight + 0.6F};
        InputEntry entry = walkerB.entry();
        entry.shotItems = {shot};
        walkerB.send(entry);
    };
    const char* rows[] = {"Bow_RapidAttack_Standard", "Bow_RapidAttack_Standard", "Bow_RapidAttack_KnockBack"};
    const int times[] = {9, 25, 52};
    for (int index = 0; index < 3; ++index) {
        while (static_cast<int>(static_cast<std::uint16_t>(walkerB.input + 1 - pressB)) < times[index]) {
            walkerB.idle(1);
            shop.walker.idle(1);
        }
        itemB(rows[index]);
        shop.item_at(times[index], BowShop::bow_item("TripleShot", rows[index]));
    }
    shop.walker.idle(10);
    walkerB.idle(10);
    check(shop.lost(wolfA) > 0.0F && shop.lost(wolfB) > 0.0F && shop.volley_shots() == 6,
          "two volleys: each caster shoots its own three arrows at its own wolf");
    check(shop.remaining(kRapid) < kRapidCooldown * 0.5F && shop.remaining(kRapid, kCharacterB) < kRapidCooldown * 0.5F,
          "Rapid Accuracy for each caster: the three arrows of each cast hit its own target (one count for each volley)");
    check(fragments_of(shop.b, 45, 2932, fromB).size() >= 1 && damage_messages(shop.b, fromB) == 3 && damage_messages(shop.a, fromA) == 3,
          "each client gets its own cooldown state and the damage numbers of its own three arrows only");
    // A casts Explosive Arrow at its wolf: B observes the explosion entity and the icon of the burn, with the same bytes.
    const std::size_t spellA = shop.a.got.size();
    const std::size_t spellB = shop.b.got.size();
    shop.press(1);
    for (int step = 0; step < 14; ++step) {
        walkerB.idle(1);
        if (step < 10) {
            shop.walker.idle(1);
        }
    }
    shop.item_at(13, BowShop::bow_item("ExplosionShot", nullptr, {1.0F, 0.0F, 0.0F}, nullptr));
    for (int step = 0; step < 6; ++step) {
        shop.walker.idle(1);
        walkerB.idle(1);
    }
    const auto recordA = fragments_of(shop.a, kExplosionSpellSlot, server::rep::kTypeSpellState, spellA);
    const auto recordB = fragments_of(shop.b, kExplosionSpellSlot, server::rep::kTypeSpellState, spellB);
    check(!recordA.empty() && !recordB.empty() && recordA.front().fields == recordB.front().fields,
          "each session near the explosion gets its record with the same bytes (one encoder)");
    const gamedata::StatusRow* burn = shop.host.abilities.status(kBurn);
    check(burn != nullptr && icon_sent(shop.a, burn->index, spellA) && icon_sent(shop.b, burn->index, spellB),
          "each observer of the wolf gets the icon of the burn at its name plate");
}

/** BW10: the load. */
void bow_load(const Options&) {
    const auto run = [](unsigned farCount, std::uint64_t& looked, double& seconds) {
        BowShop shop;
        shop.learn(*shop.character());
        for (unsigned index = 0; index < 40; ++index) {
            (void)shop.host.world.creatures().spawn("Test_Wolf", at(kPlace.x + 3.0F + static_cast<float>(index), kPlace.y), 90.0F);
        }
        for (unsigned index = 0; index < farCount; ++index) {
            (void)shop.host.world.creatures().spawn("Test_Wolf", at(kPlace.x + 600.0F + static_cast<float>(index % 100) * 3.0F,
                                                                   kPlace.y + 600.0F + static_cast<float>(index / 100) * 3.0F), 90.0F);
        }
        shop.walker.idle(3);
        const std::uint64_t startLooked = shop.host.world.projectiles().counts().looked + shop.host.world.abilities().counts().areaLooked;
        const auto clock = std::chrono::steady_clock::now();
        for (int round = 0; round < 10; ++round) {
            shop.press(0);
            shop.walker.idle(2);
            shop.send_item(BowShop::bow_item("PenetratingShot", "Bow_Penetrating_Attack"), {kActionAttack});
            shop.walker.idle(20);
            shop.host.world.abilities().reset_cooldowns(kCharacterA, true);
            shop.press(1);
            shop.item_at(13, BowShop::bow_item("ExplosionShot", nullptr, {1.0F, 0.0F, 0.0F}, nullptr));
            shop.walker.idle(30);
            shop.host.world.abilities().reset_cooldowns(kCharacterA, true);
            shop.press(1);
            shop.item_at(13, BowShop::bow_item("ExplosionShot", nullptr, {1.0F, 0.0F, 0.0F}, nullptr));
            shop.walker.idle(30);
            shop.host.world.abilities().reset_cooldowns(kCharacterA, true);
        }
        seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - clock).count();
        looked = shop.host.world.projectiles().counts().looked + shop.host.world.abilities().counts().areaLooked - startLooked;
        return std::make_pair(shop.pierce_hits(), shop.explosions());
    };
    std::uint64_t smallLooked = 0;
    std::uint64_t largeLooked = 0;
    double smallSeconds = 0.0;
    double largeSeconds = 0.0;
    const auto small = run(40, smallLooked, smallSeconds);
    const auto large = run(4000, largeLooked, largeSeconds);
    note("10 pierces and 20 explosions: 40 near and 40 far creatures: " + std::to_string(smallLooked) + " looks, " + std::to_string(smallSeconds)
         + " s; 40 near and 4,000 far: " + std::to_string(largeLooked) + " looks, " + std::to_string(largeSeconds) + " s; pierce hits "
         + std::to_string(large.first) + ", explosions " + std::to_string(large.second));
    check(small.first >= 300 && large.first == small.first && large.second == 20 && small.second == 20,
          "10 pierces through the 40 near creatures (each one hit one time for each arrow) and 20 explosions in each world");
    check(largeLooked == smallLooked && largeLooked > 0, "the pierces and the explosions look at the same creatures: 4,000 far creatures add no look");
    check(largeSeconds < 3.0, "the time limit: 10 pierces and 20 explosions with their flights in less than 3 s in a world of 4,040 creatures");
}

} // namespace

std::vector<Case> bow_cases() {
    return {
        {"BW1", "bow abilities: the projectile data of format 2 (the ability actions, the clip shots) and the hit rates", bow_data},
        {"BW2", "bow abilities: Rapid Shot, its volley, Final Blow, Rapid Accuracy (decision D11)", rapid_shot},
        {"BW3", "bow abilities: the shot items of a volley (too early, one input step, a different row, a fourth, a repeated report)", volley_items},
        {"BW4", "bow abilities: Penetrating Shot, the aim and its confirm by the shot item, the pierce in the order of the line", penetrating_shot},
        {"BW5", "bow abilities: the pierce and a wall, the range of the profile (50 m), Blood Soaked Arrow", pierce_rules},
        {"BW6", "bow abilities: Explosive Arrow, its explosion entity and area, Contained Detonation, the burn of Flame Arrow", explosive_arrow},
        {"BW7", "bow abilities: the windows of a bow fragment for a key of a different ability", bow_windows},
        {"BW8", "bow abilities: a character with no client (the shots of the server)", server_volleys},
        {"BW9", "bow abilities: two sessions and two casters (MP-10)", two_bows},
        {"BW10", "bow abilities: the load of the pierce and the explosion (40 near and 4,000 far creatures)", bow_load},
    };
}

} // namespace soulwarden::backend::tests

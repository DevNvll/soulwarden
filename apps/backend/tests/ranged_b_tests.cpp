// Tests of stage 3.4f-B: the casters, the dodge of a player, and the stamina model (research creature-ranged.md part B and task D; live
// checks LS1 to LS9 and L6 of the stage).
//
//   SM1   the stamina data: the loader (a file in the form of the tool, the refusals) and the check of the data tool (check_stamina_tool.py)
//   SM2   the stamina path: a payment and its delay, the regeneration of the data, the empty stamina (winded, exhausted to full), a gain,
//         a dead character, a revival; the Stamina state of the owner (none for an observer)
//   SM3   the statuses and the payments: the cell Stamina, StaminaRate (the regeneration and the key 0x0B of the speed table of the
//         owner), a stamina payment of a fragment of a basic attack through the one path
//   SM4   the passive rules with the stamina and the dodge: a rule with MyStaminaPercent (the cells of Gambled Energy), a stamina gain of a
//         hit (a runeglass gem), OnHitTaken with the health and the stamina (Stamina Recovery), the hit-taken action of the data (Mauler's
//         Stamina), OnHitTakenWhileInvulnerable (Shirking Fortification), a rule of a hit between players in a dodge (not applied)
//   SM5   the tag Dodging of a status action: Guarded Sprint goes in a dodge and comes back after it
//   CRB1  the caster: the cast Cast_One_Projectile, its release at 0.5 s, the record (index 125, no ammunition, the slow slice of 20 m/s),
//         the repeat time 2.5 s, the band of a caster (6 m to 16 m)
//   CRG10 two attackers (an archer, a mage) and two targets with two sessions: each observer gets each record, each target its damage
//   CRG11 the dodge: the input of the session, the payment at its mark, the immune window (499 ms and 500 ms), a duplicate, a buffered
//         second dodge, a shot through the dodge, the refusals (exhausted, overburdened, locked), a cancel before the payment
//   CRG12 no immunity of one character for a different character (two sessions)
//   CRG13 the shield of Maelstrom takes a creature shot before a later target and before a wall
//
// The accounts, the characters, the creature kinds, the rows, and the places are made up; the stamina values are the values of the
// retail data (support/stamina_fixture.h).

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "server/rep/states/state_codecs.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/gawh_fixture.h"
#include "support/passive_fixture.h"
#include "support/range_fixture.h"
#include "support/stamina_fixture.h"
#include "support/test_navigation.h"
#include "world/stamina.h"

namespace soulwarden::backend::tests {
namespace {

using namespace range;
using nlohmann::json;
namespace sf = stamina;

constexpr unsigned kDodgeBit = 322;

/** The world of a case: the archer and the mage, their projectiles, the stamina data, A with a client (and B with enter_both). */
struct Battle : Range {
    explicit Battle(bool observe = true) : Range(observe) {
        set_battle(host);
        sf::set_stamina(host);
    }
    world::Entity* mage(float east, float north = 0.0F) {
        return host.world.creatures().spawn("Test_Mage", at(kPlace.x + east, kPlace.y + north), 90.0F).entity;
    }
    world::Stamina& stamina() {
        return host.world.stamina();
    }
    /** One step of A with the dodge input (bit 322). */
    void dodge() {
        walker.bits({kDodgeBit});
        if (withB) {
            b.exchange(host);
        }
    }
    /** Flat navigation data (a free floor in each cell): the creatures can walk. */
    void flat(const Options& options, const char* name) {
        TestNavRegion region;
        const auto folder = options.workDirectory / name;
        region.write(folder);
        if (!host.navigation.set_directory(folder.string())) {
            throw std::runtime_error("ranged test: the flat navigation data does not load");
        }
    }
};

/** @return The six values of the newest Stamina state of a client from the message `from` on (no value: none). */
std::optional<std::array<float, 6>> stamina_state(const FakeClient& client, std::size_t from) {
    const std::vector<Fragment> states = fragments_of(client, server::rep::kStaminaSlot, server::rep::kTypeStaminaState, from);
    if (states.empty()) {
        return std::nullopt;
    }
    std::array<float, 6> values{};
    for (unsigned field = 0; field < 6; ++field) {
        const auto found = states.back().fields.find({0, field});
        if (found == states.back().fields.end()) {
            return std::nullopt;
        }
        server::hub::Reader reader(found->second);
        values[field] = reader.f32();
    }
    return values;
}

/** @return The output and the status of a Python check of a data tool. */
std::pair<int, std::string> run_tool(const Options& options, const char* tool, const char* work) {
    const std::filesystem::path path = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / tool;
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::filesystem::path folder = options.workDirectory / work;
    std::filesystem::create_directories(folder);
    const std::string command = std::string(python != nullptr ? python : "python3") + " -B \"" + path.string() + "\" \"" + folder.string() + "\"";
    FILE* pipe = popen(command.c_str(), "r");
    std::string output;
    if (pipe != nullptr) {
        char buffer[512];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            output += buffer;
        }
    }
    return {pipe != nullptr ? pclose(pipe) : -1, output};
}

float elapsed(world::WorldClock::time_point from, world::WorldClock::time_point to) {
    return std::chrono::duration<float>(to - from).count();
}

std::uint64_t verdicts(world::Stamina& stamina, world::DodgeVerdict verdict) {
    return stamina.counts().dodges[static_cast<std::size_t>(verdict)];
}

/** A made-up hit of A for the evaluator (no strike): its row, its attack type, and its table. */
world::HitContext hit_of(const char* row, const char* attackType, std::uint32_t table) {
    world::HitContext hit;
    hit.row = row;
    hit.attackType = attackType;
    hit.table = table;
    return hit;
}

/** SM1: the stamina data. */
void stamina_data(const Options& options) {
    // A file in the form of the tool (the values of the retail data).
    const json fragment{{"pay", 0.1}, {"immune", {0.0, 0.5}}, {"canTransition", 0.5}, {"cancel", {{0.266667, 0.5, true}, {0.5, nullptr, false}}}};
    const json load{{"light", 1.0}, {"medium", 1.0}, {"heavy", 1.0}, {"overburdened", 1.0}};
    const json good{
        {"format", 1},
        {"component", {{"min", 0.0}, {"max", 100.0}, {"regenRate", 33.330002}, {"blockRegenRate", 10.0}, {"maxWindedTime", 2.0}}},
        {"vitals", {{"regenDelay", 1.0}, {"regenDelayOnEmpty", 1.0}, {"fullyDepletedDelay", 1.0}}},
        {"load", {{"costMult", load}, {"regenMult", load}}},
        {"dodge",
         {{"action", "Dodge"},
          {"regenDelayOnExit", 1.0},
          {"classes",
           {{"light", {{"fragment", "Dodge_Roll_Fast"}, {"costId", "Dodge_Roll_Fast"}, {"cost", 45.0}}},
            {"medium", {{"fragment", "Dodge_Roll"}, {"costId", "Dodge_Roll"}, {"cost", 45.0}}},
            {"heavy", {{"fragment", "Dodge_Roll_Slow"}, {"costId", "Dodge_Roll_Slow"}, {"cost", 50.0}}}}},
          {"fragments", {{"Dodge_Roll_Fast", fragment}, {"Dodge_Roll", fragment}, {"Dodge_Roll_Slow", fragment}}},
          {"costsNotApplied", json::array({json::array({"FastRoll+SpearReducedDodgeStaminaActive", "Dodge_Roll_Fast", 0.8})})},
          {"variantsNotUsed", json::array()}}},
        {"exhausted", {{"action", "Exhausted"}, {"enter", 0.0}, {"exit", 1.0}}}};
    const std::filesystem::path path = options.workDirectory / "stamina-data.json";
    const auto load_file = [&](const json& document, gamedata::StaminaData& data) {
        std::ofstream(path) << document.dump();
        return data.load(path.string());
    };
    gamedata::StaminaData data;
    const bool loaded = load_file(good, data);
    check(loaded && near(data.tables().maximum, 100.0F) && near(data.tables().regenRate, 33.33F) && near(data.tables().regenDelay, 1.0F)
              && near(data.tables().maxWindedTime, 2.0F) && data.tables().dodgeAction == "Dodge" && data.dodge_of(gamedata::LoadClass::heavy) != nullptr
              && near(data.dodge_of(gamedata::LoadClass::heavy)->cost, 50.0F) && data.dodge_of(gamedata::LoadClass::overburdened) == nullptr
              && data.tables().costsNotApplied == 1,
          "a file of the tool loads: the maximum 100, the rate 33.33, the delay 1 s, the winded time 2 s, the classes (45, 45, 50; no dodge for "
          "an overburdened character), one cost of a weapon rule that is not applied" + (data.error().empty() ? std::string() : ": " + data.error()));
    const gamedata::DodgeClass* medium = data.dodge_of(gamedata::LoadClass::medium);
    const gamedata::DodgeFragment* roll = medium != nullptr ? data.fragment_of(*medium) : nullptr;
    check(roll != nullptr && near(roll->pay, 0.1F) && roll->immuneStart == 0.0F && near(roll->immuneEnd, 0.5F) && near(roll->canTransition, 0.5F)
              && roll->cancel.size() == 2 && roll->cancel[0].buffered && roll->cancel[0].end.has_value() && !roll->cancel[1].end.has_value(),
          "the dodge fragment: the payment at 0.1 s, immune 0 to 0.5 s, CanTransition at 0.5 s, a buffered and an instant window");
    const auto refused = [&](const char* what, const std::function<void(json&)>& change) {
        json bad = good;
        change(bad);
        gamedata::StaminaData other;
        const bool accepted = load_file(bad, other);
        check(!accepted && !other.loaded() && !other.error().empty(), std::string("the loader refuses ") + what + ": " + other.error());
    };
    refused("a file of a different format", [](json& d) { d["format"] = 2; });
    refused("a maximum of 0", [](json& d) { d["component"]["max"] = 0.0; });
    refused("a value that is out of its range", [](json& d) { d["vitals"]["regenDelay"] = 1.0e9; });
    refused("a class whose fragment is not in the file", [](json& d) { d["dodge"]["classes"]["heavy"]["fragment"] = "Not_A_Fragment"; });
    refused("a buffered window with no end", [](json& d) { d["dodge"]["fragments"]["Dodge_Roll"]["cancel"][0][1] = nullptr; });
    refused("a payment after the mark CanTransition", [](json& d) { d["dodge"]["fragments"]["Dodge_Roll"]["pay"] = 0.7; });
    refused("an exhausted state that ends above the maximum", [](json& d) { d["exhausted"]["exit"] = 2.0; });
    refused("no action of the dodge", [](json& d) { d["dodge"].erase("action"); });
    // The check of the data tool (made-up sources).
    const auto [status, output] = run_tool(options, "check_stamina_tool.py", "check-stamina");
    bool all = true;
    for (const char* name : {"valid", "nonfinite-time", "malformed-time", "missing-cost", "other-variant", "no-component", "exhausted-part"}) {
        all = all && output.find(std::string("ok    ") + name) != std::string::npos;
    }
    if (!all) {
        note(output);
    }
    check(status == 0 && all, "the check of the data tool passes: the retail form, a time that is not a number (nan, 0.1x), a cost with no "
                              "row, two standing variants with different times, no Stamina component, an exhausted state with a different end");
}

/** SM2: the stamina path and the Stamina state. */
void stamina_path(const Options&) {
    Battle battle(true);
    battle.enter_both();
    world::Entity& own = *battle.character();
    world::Stamina& stamina = battle.stamina();
    check(stamina.has_data() && own.vitals.stamina == 100.0F && own.vitals.staminaMax == 100.0F && !stamina.exhausted(own),
          "a world entry: the full stamina 100 of 100 (m_initMax of the player slice), not exhausted");
    std::size_t fromA = battle.a.got.size();
    const std::size_t fromB = battle.b.got.size();
    const auto paid = battle.now();
    (void)stamina.change(own, -45.0F, "a test payment");
    check(near(own.vitals.stamina, 55.0F) && own.vitals.staminaRegenFrom && near(elapsed(paid, *own.vitals.staminaRegenFrom), 1.0F, 0.001F),
          "a payment: 45 less, and the regeneration starts 1 s later (StaminaRegenDelay; live check LS5)");
    battle.step();
    const auto state = stamina_state(battle.a, fromA);
    check(state && near((*state)[0], 55.0F) && near((*state)[1], 100.0F) && near((*state)[2], 0.0F) && near((*state)[3], 0.967F, 0.01F)
              && near((*state)[4], 1.0F) && near((*state)[5], 1.0F),
          "the owner gets the Stamina state (slot 42, type 4297): 55 of 100, winded 0, the delay that is left (0.97 s), the multipliers 1");
    check(!stamina_state(battle.b, fromB).has_value(), "an observer gets no Stamina state of A (owner only)");
    battle.to(paid + seconds(0.95F));
    check(near(own.vitals.stamina, 55.0F), "no regeneration in the delay");
    battle.to(paid + seconds(1.6F));
    const float expected = 55.0F + sf::kRegenRate * (elapsed(paid, battle.now()) - 1.0F);
    note("regeneration: " + std::to_string(own.vitals.stamina) + " (expected " + std::to_string(expected) + ")");
    check(near(own.vitals.stamina, expected, 1.2F), "after the delay the stamina comes back at 33.33 in a second (m_regenRate; live check LS5)");
    battle.to(paid + seconds(2.5F));
    check(own.vitals.stamina == 100.0F && !own.vitals.staminaRegenFrom, "the stamina is full again (45 in 1.35 s), and the regeneration stops");
    // The empty stamina (live check LS6): no debt, winded for m_maxWindedTime, exhausted to the full stamina.
    const auto emptied = battle.now();
    (void)stamina.change(own, -60.0F, "a test payment");
    (void)stamina.change(own, -45.0F, "a test payment");
    check(own.vitals.stamina == 0.0F && stamina.exhausted(own) && near(elapsed(emptied, own.vitals.windedUntil), 2.0F, 0.001F)
              && own.vitals.staminaRegenFrom && near(elapsed(emptied, *own.vitals.staminaRegenFrom), 1.0F, 0.001F),
          "a payment above the stamina stops at 0 (no debt); at 0 the character is winded for 2 s and exhausted (the action Exhausted)");
    check(stamina.dodge(own, battle.now(), false) == world::DodgeVerdict::exhausted, "an exhausted character gets no dodge");
    battle.to(emptied + seconds(1.5F));
    check(own.vitals.stamina > 0.0F && stamina.exhausted(own) && battle.now() < own.vitals.windedUntil,
          "the regeneration starts 1 s after the payment, also while the character is winded (live check LS6); it stays exhausted");
    battle.to(emptied + seconds(3.9F));
    check(own.vitals.stamina < 100.0F && stamina.exhausted(own) && stamina.dodge(own, battle.now(), false) == world::DodgeVerdict::exhausted,
          "at 97 of 100 the character is still exhausted: no dodge until its stamina is full");
    battle.to(emptied + seconds(4.2F));
    check(own.vitals.stamina == 100.0F && !stamina.exhausted(own), "the full stamina ends the exhausted state (the action Exhausted ends at 100 percent)");
    // A gain stops at the maximum; a dead character gets no change; a revival gives the full stamina.
    (void)stamina.change(own, -30.0F, "a test payment");
    (void)stamina.change(own, 20.0F, "a test gain");
    const float afterGain = own.vitals.stamina;
    (void)stamina.change(own, 50.0F, "a test gain");
    check(near(afterGain, 90.0F) && own.vitals.stamina == 100.0F, "a gain adds to the stamina and stops at the maximum");
    (void)stamina.change(own, -50.0F, "a test payment");
    (void)battle.host.world.health().hurt(kCharacterA, own.vitals.health);
    const float dead = own.vitals.stamina;
    check(own.life.dead && stamina.change(own, -20.0F, "a payment of a dead character") == 0.0F && own.vitals.stamina == dead,
          "a dead character gets no change of its stamina");
    (void)battle.host.world.health().heal(kCharacterA);
    check(!own.life.dead && own.vitals.stamina == 100.0F && !stamina.exhausted(own), "a revival gives the full stamina");
    battle.step(2);
    fromA = battle.a.got.size();
    battle.step(3);
    check(!stamina_state(battle.a, fromA).has_value(), "a full stamina with no change sends no new Stamina state");
}

/** SM3: the statuses with stamina values, the key StaminaRate of the speed table, and a payment of a fragment. */
void stamina_statuses(const Options&) {
    Battle battle(false);
    gamedata::AbilityTables tables = shop_abilities();
    // The rows of the data: Status_Perk_Amulet_StamOnLow (Stamina 20) and a status with StaminaRate 0.25 (Holy Ground).
    tables.statusEffects.push_back(status("Test_Stamina_Gain", 0.1F, cells({{"TickRate", 0.25F}, {"Stamina", 20.0F}, {"StackMax", 1.0F}})));
    tables.statusEffects.push_back(status("Test_Stamina_Rate", 30.0F, cells({{"StaminaRate", 0.25F}, {"StackMax", 1.0F}}, {{"EffectCategories", "Buff"}})));
    tables.statusEffects = numbered(tables.statusEffects);
    tables.staminaCosts.push_back({"Test_Fragment_Cost", 10.0F});
    tables.staminaCosts.push_back({"Test_Zero_Cost", 0.0F});
    check(battle.host.abilities.set(tables), "setup: the ability data with two stamina statuses and two stamina costs" + battle.host.abilities.error());
    world::Entity& own = *battle.character();
    world::Stamina& stamina = battle.stamina();
    (void)stamina.change(own, -50.0F, "a test payment");
    world::StatusApply how;
    how.source = world::StatusSource{world::StatusSourceKind::command, "test", 0, own.id};
    const float beforeGain = own.vitals.stamina;
    (void)battle.host.world.statuses().apply(own, "Test_Stamina_Gain", how);
    battle.step();
    check(near(own.vitals.stamina, beforeGain + 20.0F),
          "a status with the cell Stamina (Stamina Recovery: 20) gives its stamina at its first tick, through the stamina path");
    const std::size_t from = battle.a.got.size();
    (void)battle.host.world.statuses().apply(own, "Test_Stamina_Rate", how);
    battle.step();
    check(near(own.gear.staminaRate, 0.25F) && near(world::Stamina::regeneration_multiplier(own), 1.25F)
              && near(stamina.regeneration(own), sf::kRegenRate * 1.25F, 0.05F),
          "StaminaRate of a status (Holy Ground: +0.25) makes the regeneration 1.25 times faster");
    // The entry of the key 0x0B: key u8, the value 12500 (1.25 times 10000) as a big-endian i32, m_syncVitals u8.
    const std::array<std::uint8_t, 6> entry{0x0B, 0x00, 0x00, 0x30, 0xD4, 0x00};
    bool key = false;
    for (const Fragment& fragment : fragments_of(battle.a, server::rep::kStatMultiplierSlot, server::rep::kTypeStatMultiplierState, from)) {
        const auto table = fragment.fields.find({1, 0});
        key = key || (table != fragment.fields.end()
                      && std::search(table->second.begin(), table->second.end(), entry.begin(), entry.end()) != table->second.end());
    }
    const auto state = stamina_state(battle.a, from);
    check(key && state && near((*state)[5], 1.25F),
          "the owner gets the key 0x0B (StaminaRate) of its speed table with 12500 (1.25), and the Stamina state with multRegen 1.25");
    // A payment of a fragment of a basic attack (PayStaminaCost): the cost id of the stamina costs through the one path; a cost of 0 changes
    // nothing (live check LS7).
    const float before = own.vitals.stamina;
    const auto regen = own.vitals.staminaRegenFrom;
    check(stamina.pay_cost(own, "Test_Zero_Cost", 1.0F, "a test fragment") == 0.0F && own.vitals.stamina == before && own.vitals.staminaRegenFrom == regen,
          "a payment of cost 0 (the basic attacks of the reachable graphs) changes nothing, also not the delay of the regeneration");
    check(near(stamina.pay_cost(own, "Test_Fragment_Cost", 1.0F, "a test fragment"), 10.0F) && near(own.vitals.stamina, before - 10.0F),
          "a payment of a fragment with a cost of the stamina costs (Greatsword_Offense_Attack: 10) goes through the stamina path");
    check(stamina.pay_cost(own, "Not_A_Cost", 1.0F, "a test fragment") == 0.0F, "a cost id with no row costs nothing");
}

/** SM4: the passive rules with the stamina and the dodge. */
void stamina_rules(const Options&) {
    passive::PassiveShop shop;
    sf::set_stamina(shop.host);
    gamedata::CombatData combat = passive::crit_combat();
    combat.weapons[0].tags = "2H_Melee,GreatAxe";
    check(shop.host.combat.set(combat), "setup: the combat tables with the tags of the great axe");
    // The cells of the perk rows of the data (Global_Ring_DmgLowStam, Global_Amulet_StamOnLow, Global_Armor_DodgeSuccess_Fortify) on
    // made-up nodes of the great axe, and the row of the node Mauler's Stamina.
    gamedata::AbilityTables tables = passive::passive_tables();
    for (gamedata::AbilityRow& row : tables.abilityRows) {
        if (row.id == "Passive_Greataxe_Reaper_ArmorPenVsLowHP") {
            row.cells = cells({{"OnHit", 1.0F}, {"MyStaminaPercent", 100.0F}, {"BaseDamage", 0.025F}},
                              {{"MyStaminaComparisonType", "LessThan"}, {"WeaponTag", "GreatAxe"}});
        } else if (row.id == "Passive_Greataxe_Mauler_HeavyAddGrit") {
            row.cells = cells({{"OnHitTaken", 1.0F}, {"RequireReaction", 1.0F}, {"NumSuccessfulHits", 1.0F}, {"MyHealthPercent", 50.0F},
                               {"MyStaminaPercent", 100.0F}, {"ActivationCooldown", 60.0F}},
                              {{"MyComparisonType", "LessThan"}, {"MyStaminaComparisonType", "LessThan"},
                               {"SelfApplyStatusEffect", "Status_Perk_Amulet_StamOnLow"}, {"WeaponTag", "GreatAxe"}});
        } else if (row.id == "Passive_Greataxe_Reaper_HasteOnLight") { // A runeglass gem: Runeglass_Just_Stamina_Weapon.
            row.cells = cells({{"IsStackableAbility", 1.0F}, {"OnHit", 1.0F}, {"RequireReaction", 1.0F}, {"Stamina", 4.0F}, {"ActivationCooldown", 3.0F}},
                              {{"WeaponTag", "GreatAxe"}});
        } else if (row.id == "Passive_Greataxe_Mauler_DmgOnBlock") {
            row.cells = cells({{"IsStackableAbility", 1.0F}, {"OnHitTakenWhileInvulnerable", 1.0F}, {"RequireReaction", 1.0F},
                               {"InActionTime", 0.001F}, {"InSequenceTime", 0.001F}},
                              {{"InAction", "Dodge"}, {"InSequence", "Dodge_Roll,Dodge_Roll_Fast,Dodge_Roll_Slow"},
                               {"SelfApplyStatusEffect", "Status_Perk_Armor_DodgeSuccess_Fortify"}, {"WeaponTag", "GreatAxe"}});
        }
    }
    tables.abilityRows.push_back({"Passive_Greataxe_Mauler_StamVsLowHp", cells({}, {{"WeaponTag", "GreatAxe"}})});
    std::vector<gamedata::StatusRow> statuses = tables.statusEffects;
    statuses.push_back(status("Status_Perk_Amulet_StamOnLow", 0.1F, cells({{"TickRate", 0.25F}, {"Stamina", 20.0F}, {"StackMax", 1.0F}})));
    statuses.push_back(status("Status_Perk_Armor_DodgeSuccess_Fortify", 4.0F,
                              cells({{"PhysicalArmor", 0.02F}, {"ElementalArmor", 0.02F}, {"StackMax", 1.0F}}, {{"EffectCategories", "Buff+ArmorFortify"}})));
    statuses.push_back(status("Status_Passive_Greataxe_Mauler_StamVsLowHp", 0.1F,
                              cells({{"TickRate", 0.25F}, {"Stamina", 40.0F}, {"StackMax", 1.0F}},
                                    {{"OnEndStatusEffect", "Status_Passive_Greataxe_Mauler_StamVsLowHp_Timer"}})));
    statuses.push_back(status("Status_Passive_Greataxe_Mauler_StamVsLowHp_Timer", 20.0F, cells({{"StackMax", 1.0F}}, {{"EffectCategories", "Timer"}})));
    tables.statusEffects = numbered(statuses);
    gamedata::ActionHitStatus mauler;
    mauler.list = "playeractions_ability_greataxe";
    mauler.action = "GreatAxe_Passive_StamWhenLowHP_Detect";
    mauler.nodes = {"Passive_Greataxe_Mauler_StamVsLowHp"};
    mauler.tags = {"GreatAxe"};
    mauler.health = gamedata::ActionHitStatus::Compare{"LessThan", 0.5F};
    mauler.stamina = gamedata::ActionHitStatus::Compare{"LessThan", 1.0F};
    mauler.without = {"Status_Passive_Greataxe_Mauler_StamVsLowHp_Timer"};
    mauler.power = {"GreaterThanOrEqual", 0.0F};
    mauler.reaction = true;
    mauler.statuses = {"Status_Passive_Greataxe_Mauler_StamVsLowHp", "Status_Passive_Greataxe_Mauler_StamVsLowHp_Timer"};
    tables.actionHitStatuses = {mauler};
    check(shop.host.abilities.set(tables), "setup: the ability data with the rows, the statuses, and the hit-taken action" + shop.host.abilities.error());
    gamedata::ProgressionData progression = passive::passive_progression();
    gamedata::AbilityNode node;
    node.id = "Passive_Greataxe_Mauler_StamVsLowHp";
    node.table = kGreatAxe;
    node.column = 90;
    progression.nodes.push_back(node);
    shop.host.progression.set(progression);
    shop.hold("TestAxe");
    world::Entity& own = *shop.character();
    world::Stamina& stamina = shop.world().stamina();
    world::Entity* wolf = shop.wolf(2);
    // Gambled Energy: MyStaminaPercent below 100.
    shop.learn("Passive_Greataxe_Reaper_ArmorPenVsLowHP");
    const world::HitBonus full = shop.world().passives().before_hit(own, *wolf, hit_of("Test_Light", "Light", kGreatAxe));
    (void)stamina.change(own, -45.0F, "a test dodge");
    const world::HitBonus spent = shop.world().passives().before_hit(own, *wolf, hit_of("Test_Light", "Light", kGreatAxe));
    check(near(full.damage, 0.0F) && near(spent.damage, 0.025F),
          "a rule with MyStaminaPercent 100 LessThan (Gambled Energy): no part at the full stamina, +0.025 after a payment");
    // A runeglass gem: 4 stamina at a hit with damage, one time in 3 s (the cell Stamina of the rule).
    shop.learn("Passive_Greataxe_Reaper_HasteOnLight");
    const float beforeGem = own.vitals.stamina;
    (void)shop.hit(*wolf, "Test_Light", "TestAxe");
    const float afterGem = own.vitals.stamina;
    (void)shop.hit(*wolf, "Test_Light", "TestAxe");
    check(near(afterGem, beforeGem + 4.0F, 0.1F) && near(own.vitals.stamina, afterGem, 0.1F),
          "a hit rule with the cell Stamina (Runeglass_Just_Stamina_Weapon) gives 4 stamina through the stamina path, one time in 3 s");
    // Stamina Recovery: OnHitTaken at less than 50 percent health and less than 100 percent stamina gives 20 stamina.
    shop.learn("Passive_Greataxe_Mauler_HeavyAddGrit");
    own.vitals.health = own.vitals.healthMax * 0.4F;
    const float beforeRecovery = own.vitals.stamina;
    shop.world().passives().hit_taken(own, 100.0F);
    shop.walker.idle(1);
    check(near(own.vitals.stamina, beforeRecovery + 20.0F, 0.1F),
          "OnHitTaken with the health below 50 percent and the stamina below 100 percent (Stamina Recovery) gives its status: 20 stamina");
    // Mauler's Stamina: the hit-taken action of the data (the node, the great axe, the health below 50 percent, the stamina below 100
    // percent, no timer).
    shop.learn("Passive_Greataxe_Mauler_StamVsLowHp");
    (void)stamina.change(own, -50.0F, "a test dodge");
    const float beforeMauler = own.vitals.stamina;
    shop.world().passives().hit_taken(own, 100.0F);
    shop.walker.idle(1);
    const bool timer = shop.find("Status_Passive_Greataxe_Mauler_StamVsLowHp_Timer") != nullptr;
    check(timer && near(own.vitals.stamina, beforeMauler + 40.0F, 0.1F),
          "the hit-taken action GreatAxe_Passive_StamWhenLowHP_Detect gives its two statuses: 40 stamina and the timer of 20 s");
    const float afterMauler = own.vitals.stamina;
    shop.world().passives().hit_taken(own, 100.0F);
    shop.walker.idle(1);
    check(near(own.vitals.stamina, afterMauler, 0.1F), "with the timer on the character the action gives nothing");
    // Shirking Fortification: a hit in the immune window of a dodge.
    shop.learn("Passive_Greataxe_Mauler_DmgOnBlock");
    shop.world().passives().hit_taken(own, 100.0F);
    check(shop.find("Status_Perk_Armor_DodgeSuccess_Fortify") == nullptr, "a hit with no dodge: no fortify");
    check(stamina.dodge(own, shop.host.clock.now(), false) == world::DodgeVerdict::accepted, "setup: a dodge");
    stamina.evaded(own, *wolf, 100.0F, "a test hit");
    check(shop.find("Status_Perk_Armor_DodgeSuccess_Fortify") != nullptr,
          "a creature hit in the immune window (OnHitTakenWhileInvulnerable, InAction Dodge, InSequence of the dodge fragment) gives the "
          "fortify of Shirking Fortification");
    const std::vector<world::RuleView> views = shop.world().passives().describe(own);
    const auto applied = [&](const char* id) {
        return std::any_of(views.begin(), views.end(), [&](const world::RuleView& view) { return view.id == id && view.applied; });
    };
    for (const world::RuleView& view : views) {
        if (view.id == "Passive_Greataxe_Mauler_StamVsLowHp" || view.id == "Passive_Greataxe_Mauler_DmgOnBlock") {
            note(view.id + (view.applied ? ": applied, " : ": not applied, ") + view.note);
        }
    }
    check(applied("Passive_Greataxe_Mauler_StamVsLowHp") && applied("Passive_Greataxe_Mauler_DmgOnBlock") && applied("Passive_Greataxe_Reaper_HasteOnLight"),
          "the list of the rules names Mauler's Stamina (the hit-taken action), the rule of a hit in a dodge, and the stamina gain as applied");
    // A rule of a hit between players in a dodge (Global_PvP_Amulet_DodgeSuccess_DMG): the attacker of a hit in a dodge is a creature.
    const gamedata::CellValues pvp = cells({{"OnHitTakenWhileInvulnerable", 1.0F}, {"RequireReaction", 1.0F}},
                                           {{"AttackerVitalsCategory", "Player"}, {"TargetVitalsCategory", "Player"},
                                            {"SelfApplyStatusEffect", "Status_Perk_Armor_DodgeSuccess_Fortify"}});
    const world::RuleView pvpView = shop.world().passives().describe_row("Test_PvP_DodgeSuccess", &pvp, world::RuleOrigin::perk);
    check(!pvpView.applied && pvpView.note.find("between players") != std::string::npos,
          "a rule of a hit between players in a dodge is listed as not applied (the attacker is a creature): " + pvpView.note);
}

/** SM5: the tag Dodging of a status action (Guarded Sprint: moving, not Dodging, the war hammer in the hand). */
void dodging_tag(const Options&) {
    gawh::GawhShop shop;
    sf::set_stamina(shop.host);
    shop.hold("TestHammer");
    shop.learn({gawh::kGuardedSprint});
    shop.pass(1.0F);
    const auto walk = [&](int steps) {
        for (int step = 0; step < steps; ++step) {
            shop.walker.position.y += 0.12F;
            shop.walker.idle(1);
        }
    };
    walk(30);
    check(shop.status_of(gawh::kSprintStatus) != nullptr, "the character moves with the hammer in its hand: the status of Guarded Sprint");
    shop.walker.position.y += 0.12F;
    shop.walker.bits({kDodgeBit});
    const std::optional<world::Stamina::DodgeView> dodge = shop.world().stamina().dodge_of(kCharacterA);
    check(dodge && shop.status_of(gawh::kSprintStatus) == nullptr,
          "a dodge in the walk: the condition Not Dodging fails, and the status goes in the tick of the dodge");
    walk(4);
    check(shop.status_of(gawh::kSprintStatus) == nullptr, "no status while the dodge plays (to its mark CanTransition, 0.5 s)");
    walk(14);
    check(shop.status_of(gawh::kSprintStatus) != nullptr, "after the mark CanTransition the character moves and does not dodge: the status comes back");
}

/** CRB1: the caster. */
void caster(const Options& options) {
    Battle battle(false);
    world::Entity* mage = battle.mage(10.0F);
    const world::EntityId id = mage->id;
    const gamedata::CreatureSlice& body = *mage->creature.body;
    check(body.caster && body.basic_shot() != nullptr && body.basic_shot()->action == kCast
              && body.basic_shot()->releases.front().attackIndex == kMageIndex,
          "the slice with Cast_One_Projectile and no ranged FireBow: its basic shot is the cast (the band of a caster)");
    check(battle.until([&] { return battle.behavior().counts().shots == 1; }, 90), "the mage sees A at 10 m and starts its cast");
    const auto started = battle.now();
    world::Entity* creature = battle.host.world.creatures().find(id);
    check(creature->creature.action && creature->creature.action->action == kCast, "the action of the cast plays (Cast_One_Projectile)");
    battle.to(started + seconds(kCastRelease - 0.05F));
    check(battle.shots().launches == 0, "no projectile before the release of the data (0.5 s)");
    battle.until([&] { return battle.shots().launches == 1; }, 5);
    check(battle.shots().launches == 1 && elapsed(started, battle.now()) < kCastRelease + 0.07F, "the release at 0.5 s");
    const world::EntityId projectile = newest_projectile(battle);
    battle.step();
    const std::vector<Fragment> states = projectile_states(battle.a, interest_in(battle.host, 1, projectile));
    bool fields = false;
    if (!states.empty() && states[0].fields.count({0, 11}) && states[0].fields.count({0, 13}) && states[0].fields.count({0, 14})
        && states[0].fields.count({0, 15})) {
        server::hub::Reader index(states[0].fields.at({0, 11}));
        server::hub::Reader ammo(states[0].fields.at({0, 13}));
        server::hub::Reader table(states[0].fields.at({0, 14}));
        server::hub::Reader row(states[0].fields.at({0, 15}));
        fields = index.size() == kMageIndex && ammo.size() == 0 && table.u32() == crc_of("TestMageDamageTable") && row.u16() == 0;
    }
    check(fields, "the record of the cast: the attack index 125, no ammunition (0), the key of the damage table, the row 0");
    const world::Entity* ball = battle.host.world.find(projectile);
    check(ball != nullptr && near(std::hypot(ball->shot.velocity[0], ball->shot.velocity[1], ball->shot.velocity[2]), kSlowSpeed, 0.05F),
          "the projectile flies with the speed of the slow slice (20 m/s)");
    const float health = battle.character()->vitals.health;
    battle.until([&] { return battle.shots().damage == 1; }, 40);
    check(battle.character()->vitals.health < health, "the ball hits A at its contact");
    // The repeat time of a caster: 2.5 s from the start of its last cast.
    check(battle.until([&] { return battle.behavior().counts().shots == 2; }, 120), "a second cast");
    note("the second cast " + std::to_string(elapsed(started, battle.now())) + " s after the first");
    check(near(elapsed(started, battle.now()), 2.5F, 0.07F), "the next cast 2.5 s after the start of the last one (an archer: 3 s)");
    // The band of a caster: farther than 16 m the mage walks to 14 m (an archer shoots to 20 m); at 7 m it stays (an archer walks back
    // from 8 m); nearer than 6 m it walks back.
    {
        Battle far(false);
        far.flat(options, "caster-far");
        world::Entity* distant = far.mage(18.0F);
        const world::EntityId distantId = distant->id;
        check(far.behavior().command_shot(*distant, far.character()->id), "setup: a command shot of a mage at 18 m");
        far.step(200);
        const float away = world::horizontal_distance(far.host.world.creatures().find(distantId)->position, far.character()->position);
        note("the mage at 18 m: " + std::to_string(away) + " m after 6.7 s, " + std::to_string(far.behavior().counts().shots) + " casts");
        check(near(away, 14.0F, 0.1F) && far.behavior().counts().shots >= 2,
              "a target farther than 16 m: the mage walks into its band, on to 14 m (kCastHold), and casts again from there");
        Battle bow(false);
        bow.flat(options, "caster-far-archer");
        world::Entity* archer = bow.archer(18.0F);
        const world::EntityId archerId = archer->id;
        check(bow.behavior().command_shot(*archer, bow.character()->id), "setup: a command shot of an archer at 18 m");
        bow.step(200);
        const float stays = world::horizontal_distance(bow.host.world.creatures().find(archerId)->position, bow.character()->position);
        check(near(stays, 18.0F, 0.05F) && bow.behavior().counts().shots >= 2, "the control: an archer at 18 m (in its band of 20 m) shoots from its place");
    }
    for (const float start : {7.0F, 4.0F}) {
        Battle close(false);
        close.flat(options, "caster-near");
        const world::EntityId closeId = close.mage(start)->id;
        close.step(60);
        const std::uint64_t retreats = close.behavior().counts().retreats;
        const float away = world::horizontal_distance(close.host.world.creatures().find(closeId)->position, close.character()->position);
        note("the mage at " + std::to_string(start) + " m: " + std::to_string(retreats) + " walks back, " + std::to_string(away) + " m after 2 s");
        if (start > 6.0F) {
            check(retreats == 0 && close.behavior().counts().shots >= 1, "a mage at 7 m (in its band) casts from its place: no walk back");
        } else {
            check(retreats == 1 && away > start + 0.5F, "a mage at 4 m (nearer than 6 m) walks back to the middle of its band");
        }
    }
}

/** CRG10: two attackers and two targets with two sessions. */
void two_attackers(const Options&) {
    Battle battle(true);
    battle.enter_both();
    battle.host.world.set_creature_behavior(false);
    world::Entity& a = *battle.character();
    world::Entity& b = *battle.host.world.find_character(kCharacterB);
    world::Entity* archer = battle.archer(15.0F, 0.0F);
    world::Entity* mage = battle.mage(-12.0F, 1.0F);
    battle.step(5);
    const float healthA = a.vitals.health;
    const float healthB = b.vitals.health;
    const std::size_t fromA = battle.a.got.size();
    const std::size_t fromB = battle.b.got.size();
    const gamedata::RangedRelease& arrow = archer->creature.body->ranged[0].releases[0];
    const gamedata::RangedRelease& ball = mage->creature.body->ranged[0].releases[0];
    const auto line = [](const state::WorldPosition& from, const state::WorldPosition& to) {
        const float size = std::hypot(to.x - from.x, to.y - from.y, to.z - from.z);
        return std::array<float, 3>{(to.x - from.x) / size, (to.y - from.y) / size, (to.z - from.z) / size};
    };
    const state::WorldPosition fromArcher{archer->position.x - 0.6F, archer->position.y, a.position.z + 0.81F};
    const state::WorldPosition fromMage{mage->position.x + 0.6F, mage->position.y, b.position.z + 0.81F};
    const state::WorldPosition atA{a.position.x, a.position.y, a.position.z + 0.81F};
    const state::WorldPosition atB{b.position.x, b.position.y, b.position.z + 0.81F};
    check(battle.projectiles().launch_creature({archer->id, &arrow, fromArcher, line(fromArcher, atA), a.id}).has_value()
              && battle.projectiles().launch_creature({mage->id, &ball, fromMage, line(fromMage, atB), b.id}).has_value(),
          "setup: the archer shoots at A, the mage casts at B");
    battle.step(2);
    std::size_t recordsA = 0;
    std::size_t recordsB = 0;
    for (const auto& [key, entity] : battle.host.world.entities()) {
        if (entity.kind != world::EntityKind::projectile) {
            continue;
        }
        recordsA += projectile_states(battle.a, interest_in(battle.host, 1, key), fromA).empty() ? 0 : 1;
        recordsB += projectile_states(battle.b, interest_in(battle.host, 2, key), fromB).empty() ? 0 : 1;
    }
    check(recordsA == 2 && recordsB == 2, "each observer gets the record of each shot (two attackers, two sessions)");
    battle.until([&] { return battle.shots().damage == 2; }, 60);
    check(a.vitals.health < healthA && b.vitals.health < healthB && battle.shots().damage == 2 && battle.ends(world::CreatureShotEnd::hit) == 2,
          "the arrow hurts A and the ball hurts B, each one time");
    check(messages_of(battle.a, server::rep::kTypeDamageTaken, fromA).size() == 1 && messages_of(battle.b, server::rep::kTypeDamageTaken, fromB).size() == 1,
          "each owner gets one damage mark, only for its own damage");
    // A late observer of a ball (a flight of 3 s): B is far at the launch, comes near in the flight, and gets one launch with the time
    // anchor of the release (its client flies the rest of the flight), and no second launch.
    (void)battle.host.world.travel(kCharacterB, at(kPlace.x - 500.0F, kPlace.y), world::TravelScreen::off);
    battle.step(30);
    const state::WorldPosition fromSouth{mage->position.x, mage->position.y - 1.0F, mage->position.z + 1.5F};
    const std::size_t fromLaunch = battle.a.got.size();
    check(battle.projectiles().launch_creature({mage->id, &ball, fromSouth, {0.0F, -1.0F, 0.0F}, a.id}).has_value(),
          "setup: a ball of the mage to the south (no character on its line)");
    const world::EntityId late = newest_projectile(battle);
    battle.step(2);
    const std::uint32_t lateA = interest_in(battle.host, 1, late);
    const std::size_t fromLate = battle.b.got.size();
    (void)battle.host.world.travel(kCharacterB, at(kPlace.x - 6.0F, kPlace.y - 8.0F), world::TravelScreen::off);
    battle.step(6);
    const std::uint32_t lateB = interest_in(battle.host, 2, late);
    const std::vector<Fragment> launchA = projectile_states(battle.a, lateA, fromLaunch);
    const std::vector<Fragment> launchB = projectile_states(battle.b, lateB, fromLate);
    check(lateA != 0 && lateB != 0 && launchA.size() == 1 && launchB.size() == 1 && field_u64(launchB[0], 4) == field_u64(launchA[0], 4)
              && launchB[0].fields.at({0, 0}) == launchA[0].fields.at({0, 0}),
          "a session that comes near in the flight of a ball gets one launch with the time anchor of the release, the same as the first "
          "observer");
    battle.until([&] { return battle.ends(world::CreatureShotEnd::lifetime) + battle.ends(world::CreatureShotEnd::range) == 1; }, 120);
    battle.step(3);
    note("the end of the ball: " + std::to_string(battle.ends(world::CreatureShotEnd::lifetime)) + " life time, "
         + std::to_string(battle.ends(world::CreatureShotEnd::range)) + " range; B: "
         + std::to_string(projectile_states(battle.b, lateB, fromLate).size()) + " states, " + std::to_string(stops_of(battle.b, lateB, fromLate))
         + " stops; A: " + std::to_string(stops_of(battle.a, lateA, fromLaunch)) + " stops");
    check(projectile_states(battle.b, lateB, fromLate).size() == 1 && stops_of(battle.b, lateB, fromLate) == 1 && stops_of(battle.a, lateA, fromLaunch) == 1,
          "no second launch for the late observer; the record stops for each observer at the end of the flight");
}

/** CRG11: the dodge. */
void dodge(const Options&) {
    Battle battle(false);
    battle.host.world.set_creature_behavior(false);
    world::Entity& own = *battle.character();
    world::Stamina& stamina = battle.stamina();
    world::Entity* archer = battle.archer(10.0F);
    const gamedata::RangedRelease& arrow = archer->creature.body->ranged[0].releases[0];
    battle.step(3);
    // An accepted dodge from rest: the dodge input of the session.
    battle.dodge();
    const std::optional<world::Stamina::DodgeView> first = stamina.dodge_of(kCharacterA);
    check(first && first->fragment == "Dodge_Roll_Fast" && near(first->cost, 45.0F) && verdicts(stamina, world::DodgeVerdict::accepted) == 1,
          "the dodge input (bit 322) at rest: an accepted dodge of the fragment of the light load (Dodge_Roll_Fast, cost 45)");
    if (!first) {
        return;
    }
    const auto start = first->start;
    battle.to(start + seconds(0.05F));
    check(own.vitals.stamina == 100.0F, "no payment before the mark PayStaminaCost (0.1 s)");
    // A duplicate input in the same dodge, before its windows.
    battle.dodge();
    check(verdicts(stamina, world::DodgeVerdict::dodging) == 1 && stamina.dodge_of(kCharacterA)->generation == first->generation,
          "a second dodge input at 0.1 s (before the window CanDodgeCancel at 0.267 s): no second dodge");
    battle.to(start + seconds(0.14F));
    check(near(own.vitals.stamina, 55.0F) && stamina.counts().payments == 1, "the payment at the mark: 45, one time");
    // The immune window: 499 ms and 500 ms (the end not included).
    check(stamina.immune(own, start) && stamina.immune(own, start + std::chrono::milliseconds(499))
              && !stamina.immune(own, start + std::chrono::milliseconds(500)) && !stamina.immune(own, start - std::chrono::milliseconds(1)),
          "immune from the start of the dodge to 0.5 s (the Invulnerability clip; 499 ms immune, 500 ms not)");
    const float health = own.vitals.health;
    const world::CreatureHitOutcome inside = battle.host.world.health().creature_hit(*archer, arrow.damage, own, start + std::chrono::milliseconds(499));
    const float afterInside = own.vitals.health;
    const world::CreatureHitOutcome outside = battle.host.world.health().creature_hit(*archer, arrow.damage, own, start + std::chrono::milliseconds(500));
    check(inside == world::CreatureHitOutcome::evaded && afterInside == health && outside == world::CreatureHitOutcome::hurt
              && own.vitals.health < health && stamina.counts().evaded == 1,
          "a creature hit at 499 ms does not hurt the character; a hit at 500 ms does");
    (void)battle.host.world.health().heal(kCharacterA);
    // A buffered second dodge: a press in the buffered window (0.267 s to 0.5 s) starts the next dodge at 0.5 s.
    (void)stamina.change(own, 100.0F, "a test gain");
    battle.to(start + seconds(0.3F));
    battle.dodge();
    check(verdicts(stamina, world::DodgeVerdict::buffered) == 1, "a press in the buffered window (0.37 s): the next dodge waits");
    battle.to(start + seconds(0.55F));
    const std::optional<world::Stamina::DodgeView> second = stamina.dodge_of(kCharacterA);
    check(second && second->generation != first->generation && near(elapsed(start, second->start), 0.5F, 0.001F),
          "the buffered dodge starts at the end of the window (0.5 s)");
    if (!second) {
        return;
    }
    battle.to(second->start + seconds(0.14F));
    check(near(own.vitals.stamina, 55.0F) && stamina.counts().payments == 2, "the second dodge pays its cost at its mark");
    // A shot through a dodge: the shot crosses the body in the immune window and flies on.
    battle.to(second->start + seconds(1.0F));
    battle.dodge();
    const std::optional<world::Stamina::DodgeView> third = stamina.dodge_of(kCharacterA);
    const float before = own.vitals.health;
    const state::WorldPosition from{own.position.x + 3.0F, own.position.y, own.position.z + 0.81F};
    check(third && third->generation != second->generation
              && battle.projectiles().launch_creature({archer->id, &arrow, from, {-1.0F, 0.0F, 0.0F}, own.id}).has_value(),
          "setup: a third dodge, and an arrow 3 m from A");
    battle.step(10);
    check(own.vitals.health == before && battle.shots().evaded == 1 && battle.ends(world::CreatureShotEnd::hit) == 0,
          "an arrow that crosses the body in the immune window: no damage, and the shot flies on (no hit at A)");
    // The refusals: exhausted, overburdened, locked.
    battle.step(45);
    (void)stamina.change(own, -own.vitals.stamina, "a test payment");
    check(stamina.dodge(own, battle.now(), false) == world::DodgeVerdict::exhausted, "the stamina 0 (exhausted): no dodge");
    stamina.refilled(own);
    const std::uint8_t load = own.gear.loadClass;
    own.gear.loadClass = 3;
    check(stamina.dodge(own, battle.now(), false) == world::DodgeVerdict::overburdened, "an overburdened character: no dodge");
    own.gear.loadClass = load;
    check(stamina.dodge(own, battle.now(), true) == world::DodgeVerdict::locked, "a locked input (the loading screen of a travel): no dodge");
    // A cancel before the payment: a travel in the first step of a dodge.
    battle.step(30);
    battle.dodge();
    const std::uint64_t payments = stamina.counts().payments;
    const std::uint64_t cancelled = stamina.counts().paymentsCancelled;
    (void)battle.host.world.travel(kCharacterA, at(kPlace.x, kPlace.y + 1.0F), world::TravelScreen::off);
    battle.step(10);
    check(stamina.counts().payments == payments && stamina.counts().paymentsCancelled == cancelled + 1 && own.vitals.stamina == 100.0F,
          "a travel before the mark of the payment ends the dodge: no payment");
}

/** CRG12: no immunity of one character for a different character. */
void no_transfer(const Options&) {
    Battle battle(true);
    battle.enter_both();
    battle.host.world.set_creature_behavior(false);
    world::Entity& a = *battle.character();
    world::Entity& b = *battle.host.world.find_character(kCharacterB);
    world::Stamina& stamina = battle.stamina();
    world::Entity* archer = battle.archer(10.0F);
    const gamedata::RangedRelease& arrow = archer->creature.body->ranged[0].releases[0];
    battle.step(3);
    battle.dodge();
    check(stamina.immune(a, battle.now()) && !stamina.immune(b, battle.now()), "A dodges; B does not");
    const float healthB = b.vitals.health;
    check(battle.host.world.health().creature_hit(*archer, arrow.damage, b, battle.now()) == world::CreatureHitOutcome::hurt && b.vitals.health < healthB,
          "a creature hit on B in the dodge of A hurts B");
    // A shot through the dodge of A hits B 2 m behind A on the line.
    (void)battle.host.world.health().heal(kCharacterB);
    battle.step(30);
    battle.host.world.set_position(b.id, at(kPlace.x - 2.0F, kPlace.y), false);
    battle.step(3);
    battle.dodge();
    const float healthA = a.vitals.health;
    const float behind = b.vitals.health;
    const state::WorldPosition from{a.position.x + 3.0F, a.position.y, a.position.z + 0.81F};
    check(stamina.immune(a, battle.now()) && battle.projectiles().launch_creature({archer->id, &arrow, from, {-1.0F, 0.0F, 0.0F}, a.id}).has_value(),
          "setup: A dodges again, and an arrow at A");
    battle.step(10);
    check(a.vitals.health == healthA && b.vitals.health < behind && battle.shots().evaded == 1 && battle.ends(world::CreatureShotEnd::hit) == 1,
          "the arrow passes A in its immune window and hits B 2 m behind A");
}

/** CRG13: the shield of Maelstrom takes a creature shot before a later target and before a wall. */
void shield(const Options& options) {
    for (const bool wall : {false, true}) {
        gawh::GawhShop shop(true, !wall);
        set_battle(shop.host);
        sf::set_stamina(shop.host);
        if (wall) {
            // A wall of the navigation data 1 m to 1.5 m east of A (in the sphere of 2 m of the shield).
            TestNavRegion region;
            region.tiles[{31, 31}] = [](int x, int, std::vector<TestFloor>& floors) {
                const float px = 1984.0F + (static_cast<float>(x) + 0.5F) * 0.25F;
                if (px >= 2001.0F && px < 2001.5F) {
                    floors = {{kHeight + 3.0F, gamedata::NavFloor::kBlocked}};
                }
            };
            const auto folder = options.workDirectory / "shield-wall";
            region.write(folder);
            check(shop.host.navigation.set_directory(folder.string()), "setup: the navigation data with a wall 1 m east of A");
        } else {
            // B 1 m east of A, between the archer and A (in the sphere of the shield).
            shop.enter_b();
            shop.world().set_position(shop.world().find_character(kCharacterB)->id, at(kPlace.x + 1.0F, kPlace.y), false);
        }
        shop.hold("TestAxe");
        shop.bar(kGreatAxe, {nullptr, "Ability_Greataxe_Maelstorm", nullptr});
        shop.learn({gawh::kAbsorb});
        world::Entity* archer = shop.host.world.creatures().spawn("Test_Archer", at(kPlace.x + 10.0F, kPlace.y), 90.0F).entity;
        shop.walker.idle(3);
        world::Entity& own = *shop.character();
        const gamedata::RangedRelease& arrow = archer->creature.body->ranged[0].releases[0];
        const world::CreatureShotCounts& counts = shop.world().projectiles().creature_counts();
        const auto end_count = [&](world::CreatureShotEnd end) { return counts.ends[static_cast<std::size_t>(end)]; };
        const auto shoot = [&] {
            const state::WorldPosition from{own.position.x + 6.0F, own.position.y, own.position.z + 0.81F};
            return shop.world().projectiles().launch_creature({archer->id, &arrow, from, {-1.0F, 0.0F, 0.0F}, own.id}).has_value();
        };
        const auto health_of = [&](const char* id) { return shop.world().find_character(id)->vitals.health; };
        shop.press(1);
        const float healthA = own.vitals.health;
        const float healthB = wall ? 0.0F : health_of(kCharacterB);
        check(shoot(), "setup: the shield of Maelstrom at A, and an arrow of the archer at A");
        shop.walker.idle(10);
        if (wall) {
            check(end_count(world::CreatureShotEnd::absorbed) == 1 && end_count(world::CreatureShotEnd::wall) == 0 && own.vitals.health == healthA,
                  "the shield takes the arrow before the wall in its sphere: the end absorbed, not wall");
        } else {
            check(end_count(world::CreatureShotEnd::absorbed) == 1 && end_count(world::CreatureShotEnd::hit) == 0 && own.vitals.health == healthA
                      && health_of(kCharacterB) == healthB,
                  "the shield takes the arrow before B (a later target in its sphere): the end absorbed, no damage");
        }
        // The control: the same shot after the end of the shield (1125 ms).
        shop.pass(1.3F);
        check(shoot(), "setup: the same arrow after the end of the shield");
        shop.walker.idle(10);
        if (wall) {
            check(end_count(world::CreatureShotEnd::wall) == 1 && own.vitals.health == healthA, "with no shield the wall stops the arrow");
        } else {
            check(end_count(world::CreatureShotEnd::hit) == 1 && health_of(kCharacterB) < healthB, "with no shield the arrow hits B");
        }
    }
}

} // namespace

std::vector<Case> ranged_b_cases() {
    return {{"SM1", "stamina (3.4f-B): the stamina data, the loader and the check of the data tool (runs check_stamina_tool.py)", stamina_data},
            {"SM2", "stamina (3.4f-B): the stamina path, the delay, the regeneration, the empty stamina, the Stamina state of the owner", stamina_path},
            {"SM3", "stamina (3.4f-B): the statuses with Stamina and StaminaRate, the key 0x0B, a payment of a fragment", stamina_statuses},
            {"SM4", "stamina (3.4f-B): the passive rules with the stamina and the dodge (Gambled Energy, a runeglass gem, Stamina Recovery, "
                    "Mauler's Stamina, Shirking Fortification)",
             stamina_rules},
            {"SM5", "stamina (3.4f-B): the tag Dodging of a status action (Guarded Sprint goes in a dodge)", dodging_tag},
            {"CRB1", "casters (3.4f-B): the cast of a mage, its release, its record, its repeat time, its band", caster},
            {"CRG10", "casters (3.4f-B): two attackers and two targets with two sessions", two_attackers},
            {"CRG11", "dodge (3.4f-B): the input, the payment, the immune window, a duplicate, a buffered dodge, a shot through the dodge, the "
                      "refusals, a cancel",
             dodge},
            {"CRG12", "dodge (3.4f-B): no immunity of one character for a different character (two sessions)", no_transfer},
            {"CRG13", "dodge (3.4f-B): the shield of Maelstrom takes a creature shot before a later target and before a wall", shield}};
}

} // namespace soulwarden::backend::tests

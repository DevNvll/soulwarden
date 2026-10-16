// The corrections of the code review of stage 3.4f-B (findings CS-R1 to CS-R6; review file
// ~/Games/soulwarden/work/evidence/creature-ranged-b/code-review/FINDINGS.md). Each case fails on the tree of stage 3.4f-B (commit
// 7e70ceb) and passes after the correction of stage 3.4f-C. The cases come from the proof cases of the reviewer (CS1, CS2, CS3, CS5, CS6,
// CS7), with made-up ids only.
#include <chrono>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "support/attack_fixture.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/gawh_fixture.h"
#include "support/range_fixture.h"
#include "support/stamina_fixture.h"
#include "support/station_fixture.h"
#include "world/stamina.h"

namespace soulwarden::backend::tests {
namespace rc_review {

void old_dodge_contact(const Options&) {
    for (const bool buffered : {false, true}) {
        range::Range battle(false);
        range::set_battle(battle.host);
        stamina::set_stamina(battle.host);
        battle.host.world.set_creature_behavior(false);
        auto& own = *battle.character();
        auto& service = battle.host.world.stamina();
        auto* mage = battle.host.world.creatures().spawn("Test_Mage", range::at(2015.0F, 2000.0F), 90.0F).entity;
        const auto& ball = mage->creature.body->ranged[0].releases[0];
        const auto start = battle.now();
        check(service.dodge(own, start, false) == world::DodgeVerdict::accepted, "setup: first dodge accepted");
        const state::WorldPosition origin{own.position.x + 10.21F, own.position.y, own.position.z + 0.81F};
        check(battle.projectiles().launch_creature({mage->id, &ball, origin, {-1.0F, 0.0F, 0.0F}, own.id}).has_value(),
              "setup: a slow ball contacts the capsule at about 490 ms");
        const float health = own.vitals.health;
        for (int tick = 1; tick <= 48; ++tick) {
            battle.host.clock.advance(std::chrono::milliseconds(10));
            battle.host.tick();
            if (buffered && tick == 30) {
                check(service.dodge(own, battle.now(), false) == world::DodgeVerdict::buffered, "setup: second dodge buffered at 300 ms");
            }
        }
        check(own.vitals.health == health && battle.shots().damage == 0 && battle.shots().evaded == 0,
              "setup: the ball has not reached the capsule at 480 ms");
        battle.host.clock.advance(std::chrono::milliseconds(40));
        battle.host.tick();
        note(std::string("RZ1 buffered=") + (buffered ? "1" : "0") + " health_loss=" + std::to_string(health - own.vitals.health)
             + " evaded=" + std::to_string(battle.shots().evaded) + " damage=" + std::to_string(battle.shots().damage));
        check(own.vitals.health == health && battle.shots().evaded == 1 && battle.shots().damage == 0,
              "a contact at 490 ms remains immune when the next dodge starts at 500 ms");
    }
}

void refused_dodge_keeps_attack(const Options&) {
    namespace af = attack_fixture;
    af::Range battle;
    stamina::set_stamina(battle.host);
    af::Attacker input{battle.host, battle.a, af::kPlace};
    auto& own = *battle.host.world.find_character(af::kCharacterA);
    (void)battle.host.world.stamina().change(own, -100.0F, "review payment");
    input.click();
    input.idle(11);
    const auto* before = battle.state();
    check(before && !before->visits.empty() && !before->visits.back().ended, "setup: an attack fragment is playing while exhausted");
    input.step({af::kActionDodge});
    const auto* after = battle.state();
    note("RZ2 exhausted=" + std::to_string(own.vitals.exhausted) + " dodge_present="
         + std::to_string(battle.host.world.stamina().dodge_of(af::kCharacterA).has_value()) + " attack_ended="
         + std::to_string(after && after->visits.back().ended));
    check(!battle.host.world.stamina().dodge_of(af::kCharacterA), "the dodge is refused while exhausted");
    check(after && !after->visits.back().ended, "a refused dodge leaves the playing attack intact");
    for (const bool request : {false, true}) {
        gawh::GawhShop shop;
        stamina::set_stamina(shop.host);
        auto actions = gawh::gawh_actions();
        // The windows of this fragment in the retail data. The common fixture omits dodge windows.
        for (auto& weapon : actions.weapons) {
            for (auto& fragment : weapon.fragments) {
                if (fragment.name == "Ability_GreatAxe_Whirlwind") {
                    fragment.dodge = {{0.5F, 0.866667F, true}, {0.866667F, -1.0F, false}};
                }
            }
        }
        check(shop.host.weaponActions.set(actions), "setup: Whirlwind has its buffered and instant dodge windows");
        shop.hold("TestAxe");
        shop.bar(shop::kGreatAxe, {"Ability_Greataxe_Whirlwind", nullptr, nullptr});
        auto* wolf = shop.wolf(-1.5F);
        auto& player = *shop.character();
        (void)shop.host.world.stamina().change(player, -100.0F, "review payment");
        const auto pressed = shop.press(0);
        check(shop.contact_at(static_cast<std::uint16_t>(pressed + 15), *wolf) == 1, "setup: the first spin hits while exhausted");
        shop.until(static_cast<std::uint16_t>(pressed + 20));
        shop.step(request ? std::vector<unsigned>{322} : std::vector<unsigned>{});
        const auto hits = shop.contact_at(static_cast<std::uint16_t>(pressed + 27), *wolf);
        note(std::string("RZ2 Whirlwind refused_dodge_request=") + (request ? "1" : "0") + " total_spin_hits=" + std::to_string(hits));
        check(!shop.host.world.stamina().dodge_of(shop::kCharacterA), "no dodge is accepted while exhausted");
        check(hits == 2, "a refused buffered dodge does not remove the second spin hit");
    }
}

void cancel_second_attempt(const Options&) {
    using namespace stations;
    Stations shop(false);
    shop.enter(false);
    auto* keg = shop.fixture("powderKeg", 1.0F);
    const InteractionBlock block = child_block(shop.host.interactions, *keg, kTrigger);
    auto first = report_entry(shop.sequenceA++, shop.placeA.x, shop.placeA.y, shop.placeA.z);
    first.interaction = block;
    shop.host.datagram(shop.clientA.peer, shop.clientA.input_report(first));
    shop.host.steps(2, {&shop.clientA});
    check(shop.entity(interaction::kCharacterA)->interaction.has_value(), "setup: first use starts");
    send_cancel(shop, shop.clientA, interaction::kCharacterA, block);
    check(!shop.entity(interaction::kCharacterA)->interaction, "setup: first use is cancelled");
    auto empty = report_entry(shop.sequenceA++, shop.placeA.x, shop.placeA.y, shop.placeA.z);
    shop.host.datagram(shop.clientA.peer, shop.clientA.input_report(empty));
    auto second = report_entry(shop.sequenceA++, shop.placeA.x, shop.placeA.y, shop.placeA.z);
    second.interaction = block;
    const Bytes delayed = shop.clientA.input_report(second);
    send_cancel(shop, shop.clientA, interaction::kCharacterA, block);
    shop.host.datagram(shop.clientA.peer, delayed);
    shop.host.steps(520, {&shop.clientA});
    const auto completions = shop.interactions().completions_of(keg->id);
    note("RZ3 second_attempt_completions=" + std::to_string(completions));
    check(completions == 0, "an early cancel of a second attempt on the same source blocks its delayed press");
}

int action_of(const Fragment& fragment) {
    const auto field = fragment.fields.find({0, 33});
    if (field == fragment.fields.end()) return -2;
    server::hub::Reader reader(field->second);
    if (reader.size() == 0) return -1;
    reader.u16();
    reader.size();
    return static_cast<int>(reader.size()) - 1;
}

void late_spawn_windup(const Options&) {
    using namespace range;
    Range battle(true);
    battle.enter_both();
    battle.behavior().set_ignored(kCharacterA, true);
    (void)battle.host.world.travel(kCharacterB, at(kPlace.x - 500.0F, kPlace.y), world::TravelScreen::off);
    battle.step(30);
    auto body = archer_slice();
    body.actions.push_back({"Spawn", "Spawn", 99, false});
    body.spawnActionName = "Spawn";
    check(battle.host.creatures.set_full({body}, {archer_kind()}, {}, archer_levels(), {}, {arrow_slice()},
          gamedata::CreatureCapsule{kPlayerRadius, kPlayerSegment, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 0.0F}}),
          "setup: the archer needs Spawn in the first record of each observer");
    auto* archer = battle.archer(10.0F);
    battle.step(100);
    check(battle.shots().launches == 0 && interest_in(battle.host, 2, archer->id) == 0, "setup: an old archer, no shot and no interest for B");
    battle.behavior().set_ignored(kCharacterB, true);
    check(battle.host.world.creatures().start_action(*archer, 4), "setup: the archer already has its walk action at B's entry");
    const auto fromB = battle.b.got.size();
    (void)battle.host.world.travel(kCharacterB, at(kPlace.x - 1.0F, kPlace.y + 1.0F), world::TravelScreen::off);
    battle.host.datagram(battle.b.peer, battle.b.report_at(300, kPlace.x - 1.0F, kPlace.y + 1.0F, kHeight));
    battle.host.tick();
    battle.b.exchange(battle.host);
    check(interest_in(battle.host, 2, archer->id) != 0 && battle.behavior().counts().shots == 0,
          "B receives the first creature record before any shot starts");
    battle.behavior().set_ignored(kCharacterB, false);
    battle.behavior().hurt_by(*archer, battle.host.world.find_character(kCharacterB)->id);
    check(battle.until([&] { return battle.shots().launches == 1; }), "setup: the normal behavior releases its first arrow at B");
    const auto interest = interest_in(battle.host, 2, archer->id);
    unsigned spawn = 0, bow = 0;
    for (const auto& fragment : fragments_for(battle.b, interest, fromB)) {
        if (fragment.slot != 5) continue;
        spawn += action_of(fragment) == 6;
        bow += action_of(fragment) == static_cast<int>(kFireBow);
    }
    const auto projectile = newest_projectile(battle);
    const auto arrow = projectile_states(battle.b, interest_in(battle.host, 2, projectile), fromB).size();
    note("RZ4 late_observer_spawn=" + std::to_string(spawn) + " bow=" + std::to_string(bow) + " arrow=" + std::to_string(arrow));
    check(interest != 0 && spawn == 1 && arrow > 0, "B has the creature Spawn record and the released arrow");
    check(bow > 0, "a late observer gets the bow windup before the released arrow");
}

void early_cancel_window_file(const Options& options) {
    using nlohmann::json;
    range::Range battle(false);
    battle.host.world.set_creature_behavior(false);
    const json fragment{{"pay", 0.1}, {"immune", {0.0, 0.5}}, {"canTransition", 0.5},
                        {"cancel", {{0.01, 0.05, true}, {0.5, nullptr, false}}}};
    const json loads{{"light", 1.0}, {"medium", 1.0}, {"heavy", 1.0}, {"overburdened", 1.0}};
    json document{{"format", 1},
                  {"component", {{"min", 0.0}, {"max", 100.0}, {"regenRate", 33.33}, {"blockRegenRate", 10.0}, {"maxWindedTime", 2.0}}},
                  {"vitals", {{"regenDelay", 1.0}, {"regenDelayOnEmpty", 1.0}, {"fullyDepletedDelay", 1.0}}},
                  {"load", {{"costMult", loads}, {"regenMult", loads}}},
                  {"dodge", {{"action", "Dodge"}, {"regenDelayOnExit", 1.0}, {"classes", json::object()}, {"fragments", json::object()}}},
                  {"exhausted", {{"action", "Exhausted"}, {"enter", 0.0}, {"exit", 1.0}}}};
    for (const char* klass : {"light", "medium", "heavy"}) {
        document["dodge"]["classes"][klass] = {{"fragment", klass}, {"costId", klass}, {"cost", 45.0}};
        document["dodge"]["fragments"][klass] = fragment;
    }
    const auto path = options.workDirectory / "rc-early-cancel-window.json";
    std::ofstream(path) << document.dump();
    const bool accepted = battle.host.stamina.load(path.string());
    note("RZ5 loader_accepted=" + std::to_string(accepted));
    check(!accepted, "the loader refuses a window that replaces a dodge before its payment");
    if (!accepted) return;
    auto& own = *battle.character();
    auto& service = battle.host.world.stamina();
    check(service.dodge(own, battle.now(), false) == world::DodgeVerdict::accepted, "setup: first dodge accepted with the loaded file");
    for (int index = 0; index < 10; ++index) {
        battle.host.clock.advance(std::chrono::milliseconds(25));
        battle.host.tick();
        check(service.dodge(own, battle.now(), false) == world::DodgeVerdict::buffered, "setup: the next dodge buffers before the payment");
        battle.host.clock.advance(std::chrono::milliseconds(25));
        battle.host.tick();
    }
    const auto generation = service.dodge_of(range::kCharacterA)->generation;
    note("RZ5 dodge_generation=" + std::to_string(generation) + " payments=" + std::to_string(service.counts().payments)
         + " stamina=" + std::to_string(own.vitals.stamina) + " immune=" + std::to_string(service.immune(own, battle.now())));
    check(service.counts().payments > 0, "a chain of accepted dodges does not discard every payment");
}

void fixed_load_condition(const Options&) {
    shop::Shop battle;
    const char* ruleId = "GlobalPerk_Artifact_Set2_HeavyLegs";
    const auto row = shop::cells({{"MaxHealth", 0.2F}}, {{"EquipLoadCategory", "Fast,Normal"}});
    auto perks = shop::shop_perks();
    perks.perks.push_back(shop::perk("PerkID_Review_Load", "Generated", "OnEquip", "Armor", "ReviewLoad", {}, {ruleId}));
    perks.abilities["globalperk_artifact_set2_heavylegs"] = row;
    check(battle.host.perks.set(perks), "setup: a fixed perk with the exact load condition and value of the applied-list row");
    check(battle.wear("TestLegs", world::paperdoll::kLegs, {"PerkID_Review_Load"}).ok(), "setup: the perk is on the equipped legs");
    auto& own = *battle.character();
    check(own.gear.loadClass == 0 && shop::near(own.gear.healthPart, 0.2F), "control: light load gets the 20 percent health part");
    const auto view = battle.world().passives().describe_row(ruleId, &row, world::RuleOrigin::perk);
    check(view.applied, "the stamina table describes the row as applied");
    check(battle.wear("TestChest", world::paperdoll::kChest).ok() && own.gear.loadClass == 2, "setup: the chest changes the load to Heavy");
    note("RZ6 listed_applied=" + std::to_string(view.applied) + " load=" + std::to_string(own.gear.loadClass)
         + " health_part=" + std::to_string(own.gear.healthPart));
    check(shop::near(own.gear.healthPart, 0.0F), "Heavy load gets no health part from a row limited to Fast and Normal");
}

} // namespace rc_review

std::vector<Case> review_casters_cases() {
    return {{"RZ1", "review CS-R1: a buffered next dodge keeps the immune window of an earlier contact", rc_review::old_dodge_contact},
            {"RZ2", "review CS-R2: a refused dodge keeps the attack that plays (the second spin of Whirlwind)", rc_review::refused_dodge_keeps_attack},
            {"RZ3", "review CS-R3: an early cancel of a second attempt on the same source stops its delayed press", rc_review::cancel_second_attempt},
            {"RZ4", "review CS-R4: a new observer of an old archer gets the windup before the first arrow", rc_review::late_spawn_windup},
            {"RZ5", "review CS-R5: the loader refuses a dodge window that starts the next dodge before the payment", rc_review::early_cancel_window_file},
            {"RZ6", "review CS-R6: a fixed perk with EquipLoadCategory counts only in its load classes", rc_review::fixed_load_condition}};
}
} // namespace soulwarden::backend::tests

// The cases of the code review of stage 3.4f-D (findings SU-R1 to SU-R7), corrected in stage 3.5b.1. Each case fails on the tree of the
// commit of stage 3.4f-D and passes with the correction. The cases come from the proof tests of the review; RSU4 runs the cases
// review-support-* of the check of the creature tool (tools/gamedata/check_creature_tool.py). Made-up accounts, ids, names, and slices only.
//
//   RSU1  SU-R1: the same heal again after its last tick: the tick that the old end stopped comes at its time
//   RSU2  SU-R2: a late world step and an earlier end of a different effect: the heal tick before the end of the heal comes
//   RSU3  SU-R3: the loader takes the time of a volume only from an event of its entity or of a parent (spell data of format 5)
//   RSU4  SU-R4: a cast of a listed area spell with no spell row, no slice asset, or no slice stops the creature tool
//   RSU5  SU-R5: after the echo (2473) of a use that the server ended, the next press of the same object starts a use
//   RSU6  SU-R6: a creature that fights a Pylon uses its self heal and its self buff
//   RSU7  SU-R7: no poison tick after the end of the life of its area, also with a late world step

#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "support/cases.h"
#include "support/check.h"
#include "support/spell_kind_fixture.h"
#include "support/station_fixture.h"
#include "support/support_fixture.h"

namespace soulwarden::backend::tests {
namespace review_areas {
nlohmann::json mushroom_document(float active, unsigned format);
void start_poison(area::Areas& battle);
} // namespace review_areas

namespace review_support {

using namespace support;
using nlohmann::json;
using std::chrono::milliseconds;

void advance(Support& battle, milliseconds time) {
    battle.host.clock.advance(time);
    battle.host.world.tick();
}

/** @return The last health of the creature in the Vitals states that a client got (-1: none). */
float last_health(Support& battle, const FakeClient& client, unsigned session, const world::Entity& creature) {
    float health = -1.0F;
    const auto interest = interest_in(battle.host, session, creature.id);
    for (const auto& fragment : fragments_for(client, interest)) {
        if (fragment.slot != creature.creature.body->vitalsSlot || fragment.type != 15) {
            continue;
        }
        const auto field = fragment.fields.find({0, 0});
        if (field == fragment.fields.end() || field->second.size() != 4) {
            continue;
        }
        const auto& bytes = field->second;
        const std::uint32_t bits = (std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16) | (std::uint32_t(bytes[2]) << 8) | bytes[3];
        health = std::bit_cast<float>(bits);
    }
    return health;
}

/** RSU1 (SU-R1): the same row again at 7.1 s, after the last tick (7 s) of the 8 s heal. The tick of 8 s comes, and both observers see it. */
void refresh_after_last_tick(const Options&) {
    Support battle(true);
    battle.enter_both();
    battle.host.world.set_creature_behavior(false);
    auto* cleric = battle.make("Test_Cleric", 6.0F);
    auto* other = battle.make("Test_Cleric", -6.0F);
    const float maximum = cleric->vitals.healthMax;
    (void)battle.host.world.creatures().set_health(cleric->id, maximum * 0.1F);
    const auto* row = battle.host.abilities.status(kRekindle);
    check(battle.host.world.creatures().give_status(*cleric, *row, cleric->id), "setup: the eight-second heal starts");
    advance(battle, milliseconds(0));
    advance(battle, milliseconds(7100));
    const auto* running = Support::effect(*cleric, kRekindle);
    check(running && (!running->nextTick || *running->nextTick >= running->ends) && battle.counts().effectTicks == 8,
          "setup: the last tick before the end ran, and no tick is due before the end (0.9 s left)");
    check(battle.host.world.creatures().give_status(*cleric, *row, cleric->id), "setup: the same row again before the end");
    advance(battle, milliseconds(1000));
    running = Support::effect(*cleric, kRekindle);
    note("RSU1 running=" + std::to_string(running != nullptr) + " ticks=" + std::to_string(battle.counts().effectTicks)
         + " health_fraction=" + std::to_string(cleric->vitals.health / maximum));
    check(running && running->ends > battle.now(), "control: the same row again gives the later end");
    check(battle.counts().effectTicks == 9 && std::fabs(cleric->vitals.health / maximum - 0.505F) < 0.0001F,
          "the heal continues with its tick of 8 s (nine ticks, 50.5 percent)");
    battle.step(4);
    const float a = last_health(battle, battle.a, 1, *cleric);
    const float b = last_health(battle, battle.b, 2, *cleric);
    note("RSU1 observer_A=" + std::to_string(a / maximum) + " observer_B=" + std::to_string(b / maximum));
    check(std::fabs(a / maximum - 0.505F) < 0.0001F && std::fabs(b / maximum - 0.505F) < 0.0001F, "both observers get the health of that tick");
    advance(battle, milliseconds(1000));
    check(battle.counts().effectTicks == 10, "the ticks keep their cadence of 1 s: no second tick at one time");
    check(other->vitals.health == other->vitals.healthMax && other->creature.effects.empty(), "control: the other creature has no heal");
}

/** RSU2 (SU-R2): the heal tick of 7 s comes in a world step at 8.1 s, also when a 0.1 s dispel of 6.5 s ends before it. */
void late_sweep_keeps_tick(const Options&) {
    for (const bool extra : {false, true}) {
        Support battle(true);
        battle.enter_both();
        battle.host.world.set_creature_behavior(false);
        auto* cleric = battle.make("Test_Cleric", 6.0F);
        const float maximum = cleric->vitals.healthMax;
        (void)battle.host.world.creatures().set_health(cleric->id, maximum * 0.1F);
        (void)battle.host.world.creatures().give_status(*cleric, *battle.host.abilities.status(kRekindle), cleric->id);
        advance(battle, milliseconds(0));
        advance(battle, milliseconds(6500));
        check(battle.counts().effectTicks == 7, "setup: seven heal ticks; the last tick is due at 7 s");
        if (extra) {
            (void)battle.host.world.creatures().give_status(*cleric, *battle.host.abilities.status(kNullify), battle.character()->id);
            check(Support::effect(*cleric, kRekindle) != nullptr, "control: the dispel does not end the heal");
        }
        advance(battle, milliseconds(1600));
        note("RSU2 other_expiry=" + std::to_string(extra) + " ticks=" + std::to_string(battle.counts().effectTicks)
             + " health_fraction=" + std::to_string(cleric->vitals.health / maximum));
        check(battle.counts().effectTicks == 8 && std::fabs(cleric->vitals.health / maximum - 0.46F) < 0.0001F,
              "the tick before the end of the heal comes in a late world step, also after an earlier end of a different effect");
        check(Support::effect(*cleric, kRekindle) == nullptr, "control: the heal ends at 8 s");
    }
}

/** RSU3 (SU-R3): an activation event of a different entity does not prove the time of the cloud; an event of a parent does. */
void unrelated_activation(const Options& options) {
    enum class Kind { valid, unrelated, parent, oldFormat };
    for (const Kind kind : {Kind::valid, Kind::unrelated, Kind::parent, Kind::oldFormat}) {
        area::Areas battle(true);
        battle.enter_both();
        battle.host.world.set_creature_behavior(false);
        const bool early = kind != Kind::valid;
        json document = review_areas::mushroom_document(early ? 0.25F : 1.0F, 4);
        json& slice = document["slices"][0];
        slice["slots"].push_back(json::array({6U, "DelayedEvent", 3139U}));
        // The event of 0.25 s: of an entity that is not a parent of the cloud, or (the case parent) of its parent "CloudRoot".
        const char* target = kind == Kind::parent ? "CloudRoot" : "Unrelated";
        slice["events"].push_back(json::array({6U, 0.25, 2U, target, "", false, "OtherRoot", true, true}));
        if (kind != Kind::oldFormat) {
            document["format"] = 5U;
            for (json& volume : slice["volumes"]) {
                volume.push_back(kind == Kind::parent ? json::array({"CloudRoot", "Mushroom"}) : json::array({"Mushroom"}));
            }
        }
        const char* names[] = {"valid", "unrelated", "parent", "old-format"};
        const auto path = options.workDirectory / ("rsu3-" + std::string(names[static_cast<int>(kind)]) + ".json");
        std::filesystem::create_directories(options.workDirectory);
        std::ofstream(path) << document.dump();
        const bool loaded = battle.host.spells.load(path.string());
        note(std::string("RSU3 ") + names[static_cast<int>(kind)] + " accepted=" + std::to_string(loaded) + " " + battle.host.spells.error());
        if (kind == Kind::unrelated) {
            check(!loaded, "an activation of an unrelated entity does not prove an earlier time of the cloud (format 5)");
            continue;
        }
        check(loaded, kind == Kind::valid    ? "control: the cloud with the time of its own event loads (format 5)"
                      : kind == Kind::parent ? "an earlier activation of a parent of the cloud proves its time (format 5)"
                                             : "a file of format 4 (no parents) loads as before: the old common file stays usable");
        if (!loaded || kind != Kind::valid) {
            continue;
        }
        auto* shaman = battle.shaman(10.0F);
        check(battle.area_at(*shaman, area::data_cast(battle, 0), battle.character()->position) != nullptr, "setup: make the cloud");
        battle.host.clock.advance(milliseconds(250));
        battle.host.world.tick();
        battle.host.clock.advance(milliseconds(501));
        battle.host.world.tick();
        check(!battle.has_poison(kCharacterA), "control: no poison before the own activation of the cloud at 1 s");
    }
}

/** RSU4 (SU-R4): the cases review-support-* of the check of the creature tool. */
void listed_spell_join(const Options& options) {
    const std::filesystem::path tool = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_creature_tool.py";
    check(std::filesystem::is_regular_file(tool), "the check of the creature tool is in the tree");
    const std::filesystem::path work = options.workDirectory / "check-creature-support";
    std::filesystem::create_directories(work);
    const auto output = options.workDirectory / "check-creature-support.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + tool.lexically_normal().string() + "\" \"" + work.string() + "\" > \"" + output.string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream log(output);
    std::set<std::string> passed;
    for (std::string line; std::getline(log, line);) {
        if (line.rfind("ok    review-support-", 0) == 0) {
            passed.insert(line.substr(6, line.find(':') - 6));
            note(line.substr(0, 120));
        } else if (line.rfind("FAIL", 0) == 0) {
            note(line.substr(0, 200));
        }
    }
    check(status == 0, "each case of the check of the creature tool passes");
    for (const char* name : {"review-support-valid", "review-support-missing-row", "review-support-missing-asset", "review-support-missing-slice"}) {
        check(passed.contains(name), std::string("the case ") + name + " passes (a broken join of a listed spell stops the tool)");
    }
}

/** RSU5 (SU-R5): the server ends the timed use, the echo 2473 comes, then the same block in the next input report starts a new use. */
void next_press_after_server_echo(const Options&) {
    using namespace stations;
    for (const bool serverEnd : {false, true}) {
        Stations shop(false);
        shop.enter(false);
        auto* keg = shop.fixture("powderKeg", 1.0F);
        const auto block = child_block(shop.host.interactions, *keg, kTrigger);
        InputEntry first = report_entry(shop.sequenceA++, shop.placeA.x, shop.placeA.y, shop.placeA.z);
        first.interaction = block;
        shop.host.datagram(shop.clientA.peer, shop.clientA.input_report(first));
        shop.host.steps(2, {&shop.clientA});
        check(shop.entity(interaction::kCharacterA)->interaction.has_value(), "setup: the first use starts");
        if (serverEnd) {
            check(shop.host.world.interactions().cancel(interaction::kCharacterA), "setup: the server ends the first use");
        }
        send_cancel(shop, shop.clientA, interaction::kCharacterA, block);
        check(!shop.entity(interaction::kCharacterA)->interaction, "setup: the cancel (or the echo of the server end) comes");
        // No input report with no block between: the 2473 is the release (the input order of rule INT-R2).
        InputEntry second = report_entry(shop.sequenceA++, shop.placeA.x, shop.placeA.y, shop.placeA.z);
        second.interaction = block;
        shop.host.datagram(shop.clientA.peer, shop.clientA.input_report(second));
        shop.host.steps(2, {&shop.clientA});
        const bool started = shop.entity(interaction::kCharacterA)->interaction.has_value();
        note("RSU5 server_ended=" + std::to_string(serverEnd) + " second_use_started=" + std::to_string(started));
        check(started, serverEnd ? "a new press after the echo of a use that the server ended starts a use"
                                 : "control: a new press after a cancel of the client starts a use");
    }
}

/** RSU6 (SU-R6): a Cleric and a hound with a live Pylon as their target use their self action. */
void support_while_fighting_summon(const Options&) {
    for (const char* kind : {"Test_Cleric", "Test_Hound"}) {
        for (const bool summon : {false, true}) {
            Support battle(true);
            auto spellTables = support_spells();
            const auto index = static_cast<std::uint32_t>(spellTables.slices.size());
            spellTables.slices.push_back(spell_kind::pylon_slice());
            spellTables.spells.push_back({spell_kind::kPylon, index});
            spellTables.vitals.push_back({"TestPylonVitals", 59.0F, 0.2F, 496.0F, 496.0F, 295.0F});
            auto abilities = support_abilities();
            abilities.spells.push_back({spell_kind::kPylon, 1.0F, 0.0F, 0.0F, 0, 0.0F, 15000.0F, {}});
            check(battle.host.spells.set(std::move(spellTables)) && battle.host.abilities.set(std::move(abilities)),
                  "setup: the Pylon slice and vitals load with the support data");
            battle.enter_both();
            auto* creature = battle.make(kind, 4.0F);
            (void)battle.host.world.creatures().set_health(creature->id, creature->vitals.healthMax * 0.1F);
            world::Entity* target = battle.character();
            if (summon) {
                world::SpellRequest request;
                request.caster = target->id;
                request.spellId = spell_kind::kPylon;
                request.position = target->position;
                request.role = world::SpellRole::summon;
                target = battle.spells().make(request);
                check(target && target->spell.vitalsRow && target->vitals.health > 0, "setup: a live Pylon from the spell system");
            }
            if (!target) {
                continue;
            }
            battle.behavior().hurt_by(*creature, target->id);
            check(creature->creature.mind.target == target->id, "setup: the creature takes this target through the threat path");
            battle.to(battle.now() + milliseconds(5000));
            const auto count = battle.behavior().counts().statusActions;
            note(std::string("RSU6 kind=") + kind + " summon=" + std::to_string(summon) + " status_actions=" + std::to_string(count)
                 + " summon_hits=" + std::to_string(battle.spells().counts().summonHits));
            check(count >= 1, std::string(kind) + (summon ? ": a creature that fights a Pylon uses its self action"
                                                          : ": control: a creature that fights a character uses its self action"));
            if (summon) {
                check(battle.spells().counts().summonHits >= 1, "control: the creature fights the live Pylon");
            }
        }
    }
}

/** RSU7 (SU-R7): the cloud ends at 26 s; a world step of 25.6 s and then one of 27.6 s gives no poison tick of 27.5 s. */
void poison_after_area_life(const Options&) {
    for (const bool late : {false, true}) {
        area::Areas battle(true);
        battle.enter_both();
        const auto created = battle.now();
        review_areas::start_poison(battle);
        battle.to(created + milliseconds(25600));
        check(battle.has_poison(kCharacterA) && battle.areas_of(world::SpellRole::statusArea).size() == 1,
              "setup: A is in the cloud before its destroy time at 26 s");
        const float health = battle.character()->vitals.health;
        const auto ticks = battle.host.world.statuses().counts().damageTicks;
        if (!late) {
            battle.host.clock.advance(milliseconds(500));
            battle.host.world.tick();
        }
        battle.host.clock.advance(milliseconds(late ? 2000 : 1500));
        battle.host.world.tick();
        note("RSU7 late=" + std::to_string(late) + " damage_ticks=" + std::to_string(battle.host.world.statuses().counts().damageTicks - ticks)
             + " loss=" + std::to_string(health - battle.character()->vitals.health));
        check(battle.character()->vitals.health == health, "no poison tick of 27.5 s after the end of the cloud at 26 s");
        check(!battle.has_poison(kCharacterA) && battle.areas_of(world::SpellRole::statusArea).empty(),
              "control: the world tick removes the cloud and its poison");
    }
}

} // namespace review_support

std::vector<Case> review_support_cases() {
    return {{"RSU1", "review of stage 3.4f-D, SU-R1: the same heal again after its last tick keeps its next tick", review_support::refresh_after_last_tick},
            {"RSU2", "review of stage 3.4f-D, SU-R2: a late world step and an earlier end keep a due heal tick", review_support::late_sweep_keeps_tick},
            {"RSU3", "review of stage 3.4f-D, SU-R3: only an event of the entity or of a parent proves the time of a volume",
             review_support::unrelated_activation},
            {"RSU4", "review of stage 3.4f-D, SU-R4: a broken join of a listed area spell stops the creature tool (runs check_creature_tool.py)",
             review_support::listed_spell_join},
            {"RSU5", "review of stage 3.4f-D, SU-R5: a new press after the echo of a use that the server ended", review_support::next_press_after_server_echo},
            {"RSU6", "review of stage 3.4f-D, SU-R6: a creature that fights a Pylon uses its self heal or self buff",
             review_support::support_while_fighting_summon},
            {"RSU7", "review of stage 3.4f-D, SU-R7: no poison tick after the end of the life of its area", review_support::poison_after_area_life}};
}

} // namespace soulwarden::backend::tests

// The cases of the code review of stage 3.4f-C (findings AR-R1 to AR-R5 and AR-R8; AR-R6 and AR-R7 are cases of the tool checks
// tools/gamedata/check_spell_tool.py and check_creature_tool.py). Each case fails on the tree of the commit of stage 3.4f-C and
// passes with the correction of stage 3.4f-D. Made-up accounts, ids, names, and slices only.

#include <chrono>
#include <cmath>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "support/area_fixture.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/station_fixture.h"
#include "world/stamina.h"

namespace soulwarden::backend::tests {
namespace review_areas {

using namespace area;
using nlohmann::json;
using std::chrono::milliseconds;

void advance(Areas& battle, milliseconds amount) {
    battle.host.clock.advance(amount);
    battle.host.world.tick();
}

/** A mushroom that touches A only, with its first application of the poison. */
void start_poison(Areas& battle) {
    battle.host.world.set_creature_behavior(false);
    world::Entity* shaman = battle.shaman(10.0F);
    check(battle.area_at(*shaman, data_cast(battle, 0), at(kPlace.x + 1.0F, kPlace.y)) != nullptr, "setup: a mushroom that touches A only");
    advance(battle, milliseconds(1000));
    advance(battle, milliseconds(500));
    check(battle.has_poison(kCharacterA), "setup: the area gives the poison 0.5 s after the entry");
    advance(battle, milliseconds(1));
}

/** AR-R1: the dodge decides a poison tick at the due time of the tick, also when the world processes the tick late. */
void late_tick_immunity(const Options&) {
    for (const bool late : {false, true}) {
        Areas battle(true);
        battle.enter_both();
        start_poison(battle);
        world::Entity& own = *battle.character();
        const float health = own.vitals.health;
        const float healthB = battle.host.world.find_character(kCharacterB)->vitals.health;
        const auto due = *battle.host.world.statuses().statuses_of(kCharacterA)->front().nextTick;
        advance(battle, std::chrono::duration_cast<milliseconds>(due - milliseconds(480) - battle.now()));
        check(battle.host.world.stamina().dodge(own, battle.now(), false) == world::DodgeVerdict::accepted, "setup: a dodge 480 ms before the tick");
        advance(battle, milliseconds(460));
        check(battle.host.world.stamina().immune(own, due), "setup: the due time of the tick is in the immune window");
        // The next world tick: 20 ms later (in time), or 50 ms later (after the end of the immune window, 20 ms after the due time).
        advance(battle, milliseconds(late ? 50 : 20));
        note(std::string("late ") + (late ? "1" : "0") + ": loss " + std::to_string(health - own.vitals.health));
        check(own.vitals.health == health, "a tick due in the immune window does no damage, also when the world processes it 30 ms late");
        check(battle.host.world.find_character(kCharacterB)->vitals.health == healthB, "B takes no tick of the poison of A");
    }
}

/** AR-R2: a character that left the cloud takes no tick that is due in the world tick that processes its exit. */
void exit_before_tick(const Options&) {
    Areas battle(true);
    battle.enter_both();
    start_poison(battle);
    world::Entity& own = *battle.character();
    const auto due = *battle.host.world.statuses().statuses_of(kCharacterA)->front().nextTick;
    advance(battle, std::chrono::duration_cast<milliseconds>(due - milliseconds(100) - battle.now()));
    check(battle.host.world.set_position(own.id, at(kPlace.x + 8.0F, kPlace.y), true), "setup: A walks 8 m out of the cloud");
    const float health = own.vitals.health;
    advance(battle, milliseconds(100));
    note("poison after the tick " + std::to_string(battle.has_poison(kCharacterA)) + ", loss " + std::to_string(health - own.vitals.health));
    check(!battle.has_poison(kCharacterA), "the area takes the poison of A at the exit");
    check(own.vitals.health == health, "the exit comes before a poison tick that is due in the same world tick");
}

/** AR-R3: a world step that crosses the whole window of a strike still hits each character in the strike one time. */
void strike_crossed_window(const Options&) {
    for (const bool late : {false, true}) {
        Areas battle(true);
        battle.enter_both();
        battle.host.world.set_creature_behavior(false);
        world::Entity* striker = battle.striker(10.0F);
        world::Entity& own = *battle.character();
        const float health = own.vitals.health;
        check(battle.area_at(*striker, data_cast(battle, 1), own.position) != nullptr, "setup: a lightning strike at A (B stands at A)");
        advance(battle, milliseconds(890));
        check(own.vitals.health == health, "control: no damage before the window (0.9 s to 0.95 s)");
        // The next world tick: 910 ms (in the window), or 960 ms (after the window).
        advance(battle, milliseconds(late ? 70 : 20));
        const auto hits = battle.spells().counts().strikeHits;
        note(std::string("late ") + (late ? "1" : "0") + ": hits " + std::to_string(hits) + ", loss " + std::to_string(health - own.vitals.health));
        check(own.vitals.health < health && hits == 2, "a strike hits each character in it one time, also when one world step crosses its window");
        advance(battle, milliseconds(40));
        check(battle.spells().counts().strikeHits == hits, "no second hit of the strike");
    }
}

/** A spell data file with the mushroom of the cases. `active`: the time of the activation of the volume of the cloud. */
json mushroom_document(float active, unsigned format = 4) {
    json slice{{"path", "slices/test/mushroom.dynamicslice"}, {"asset", "71717171-7171-7171-7171-717171717171"}, {"sub", 2U},
               {"root", "000000C600000071"}, {"interestRadius", 50.0}, {"spellSlot", 0U}, {"positionSlot", 1U},
               {"slots", json::array({json::array({1U, "PositionInTheWorld", 13U}), json::array({2U, "DelayedEvent", 3139U}),
                                      json::array({3U, "DelayedEvent", 3139U}), json::array({4U, "DelayedEvent", 3139U})})},
               {"events", json::array({json::array({2U, 26.0, 0U, "Destroy", "", true, "Mushroom", true, true}),
                                       json::array({3U, 1.0, 2U, "Cloud", "", false, "Mushroom", true, true}),
                                       json::array({4U, 1.0, 3U, "Tell", "", false, "Mushroom", true, true})})},
               {"components", json::array()}, {"vitals", nullptr}, {"autoSpell", nullptr}, {"offenses", json::array()},
               {"detectionEvents", json::array()}, {"timelines", json::array()},
               {"volumes", json::array({json::array({"Cloud", 0.0, active, "cylinder", 1.5, 4.0, -0.5, -1.0})})},
               {"areaStatuses", json::array({json::array({"Cloud", "Cloud", 0.0, json::array({kPoison}), true, 2U, 0.5, 1.0, true})})}};
    return {{"format", format}, {"slices", json::array({slice})}, {"spells", json::array({json::array({kBombSpell, 0U})})},
            {"vitals", json::array()}, {"vitalsNotInData", json::array()}};
}

/** AR-R4: an area of spell data of format 3 (no application delays) is not a creature area. */
void old_format_sphere(const Options& options) {
    for (const unsigned format : {4U, 3U}) {
        Areas battle(false);
        battle.host.world.set_creature_behavior(false);
        json document = mushroom_document(1.0F, format);
        json& slice = document["slices"][0];
        // The shape of the area of the mage: a PositionInTheWorld slice with a sphere for the enemies.
        slice["volumes"][0] = json::array({"Cloud", 2.5, 1.0, "sphere", 2.5, 0.0, 0.0, -1.0});
        if (format == 3) {
            slice["volumes"][0].erase(slice["volumes"][0].begin() + 3, slice["volumes"][0].end());
            slice["areaStatuses"][0].erase(slice["areaStatuses"][0].begin() + 6, slice["areaStatuses"][0].end());
        }
        const auto path = options.workDirectory / ("ra4-format-" + std::to_string(format) + ".json");
        std::ofstream(path) << document.dump();
        check(battle.host.spells.load(path.string()), "setup: the file loads (the earlier spell systems use format 3)");
        const bool usable = battle.spells().area_usable(data_cast(battle, 0));
        note("format " + std::to_string(format) + ": usable " + std::to_string(usable));
        check(format == 4 ? usable : !usable, "a creature area needs spell data of format 4 (the application delays)");
        if (!usable) {
            continue;
        }
        world::Entity* shaman = battle.shaman(10.0F);
        check(battle.area_at(*shaman, data_cast(battle, 0), battle.character()->position) != nullptr, "setup: the sphere area");
        advance(battle, milliseconds(1000));
        check(!battle.has_poison(kCharacterA), "no application before the initial delay of 0.5 s");
    }
}

/** AR-R5: the loader refuses an area whose volume is active before the activation event of its entity (the cloud of the client). */
void volume_before_event(const Options& options) {
    for (const bool broken : {false, true}) {
        Areas battle(false);
        battle.host.world.set_creature_behavior(false);
        const auto path = options.workDirectory / (broken ? "ra5-broken.json" : "ra5-valid.json");
        std::ofstream(path) << mushroom_document(broken ? 0.0F : 1.0F).dump();
        const bool accepted = battle.host.spells.load(path.string());
        note(std::string("broken ") + (broken ? "1" : "0") + ": accepted " + std::to_string(accepted));
        if (!broken) {
            check(accepted, "control: the file with the volume active at its event (1 s) loads");
        } else {
            check(!accepted, "a volume that is active before the activation event of its entity is refused");
        }
        if (!accepted) {
            continue;
        }
        world::Entity* shaman = battle.shaman(10.0F);
        check(battle.area_at(*shaman, data_cast(battle, 0), battle.character()->position) != nullptr, "setup: the mushroom");
        battle.host.world.tick();
        advance(battle, milliseconds(501));
        check(!battle.has_poison(kCharacterA), "no poison before the cloud at 1 s");
    }
}

/** AR-R8: a use that the server ended does not hide the early cancel of the next attempt of the same source. */
void cancel_after_server_end(const Options&) {
    using namespace stations;
    Stations shop(false);
    shop.enter(false);
    world::Entity* keg = shop.fixture("powderKeg", 1.0F);
    const InteractionBlock block = child_block(shop.host.interactions, *keg, kTrigger);
    InputEntry first = report_entry(shop.sequenceA++, shop.placeA.x, shop.placeA.y, shop.placeA.z);
    first.interaction = block;
    shop.host.datagram(shop.clientA.peer, shop.clientA.input_report(first));
    shop.host.steps(2, {&shop.clientA});
    check(shop.entity(interaction::kCharacterA)->interaction.has_value(), "setup: the first use starts");
    check(shop.host.world.interactions().cancel(interaction::kCharacterA), "setup: the server ends the first use");
    InputEntry empty = report_entry(shop.sequenceA++, shop.placeA.x, shop.placeA.y, shop.placeA.z);
    shop.host.datagram(shop.clientA.peer, shop.clientA.input_report(empty));
    InputEntry second = report_entry(shop.sequenceA++, shop.placeA.x, shop.placeA.y, shop.placeA.z);
    second.interaction = block;
    const Bytes delayed = shop.clientA.input_report(second);
    // The cancel of the second attempt comes before its press (the echo of the end of the first use did not come).
    send_cancel(shop, shop.clientA, interaction::kCharacterA, block);
    shop.host.datagram(shop.clientA.peer, delayed);
    shop.host.steps(520, {&shop.clientA});
    const auto completions = shop.interactions().completions_of(keg->id);
    note("completions of the second attempt " + std::to_string(completions));
    check(completions == 0, "the early cancel of the second attempt holds after the server ended the first use");
}

} // namespace review_areas

std::vector<Case> review_areas_cases() {
    return {{"RAR1", "review of stage 3.4f-C, AR-R1: the dodge decides a late poison tick at its due time", review_areas::late_tick_immunity},
            {"RAR2", "review of stage 3.4f-C, AR-R2: the exit from a cloud comes before a tick due in the same world tick",
             review_areas::exit_before_tick},
            {"RAR3", "review of stage 3.4f-C, AR-R3: a world step across the window of a strike keeps its hits", review_areas::strike_crossed_window},
            {"RAR4", "review of stage 3.4f-C, AR-R4: spell data of format 3 makes no creature area", review_areas::old_format_sphere},
            {"RAR5", "review of stage 3.4f-C, AR-R5: the loader refuses a volume active before its event", review_areas::volume_before_event},
            {"RAR8", "review of stage 3.4f-C, AR-R8: a server-ended use keeps the early cancel of the next attempt",
             review_areas::cancel_after_server_end}};
}

} // namespace soulwarden::backend::tests

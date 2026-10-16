// Tests of the corrections of the code review of stage 3.4f-A (findings file of the review: CR-R1 to CR-R7), made in stage 3.4f-B. Each
// case fails on the tree of commit 762c1a7 and passes after its correction. The cases RR1 to RR4 and RR7 come from the proof tests of the
// reviewer (AR1, AR2, AR3, AR5, AR4); RR5 and RR6 run the review cases of tools/gamedata/check_creature_tool.py.
//
//   RR1  CR-R1  a slice with a required Spawn action: each observer gets the windup (the action FireBow) before the arrow
//   RR2  CR-R2  a travel of the target in the windup: the pending release does not follow it (no launch, no damage)
//   RR3  CR-R3  two early cancels of different sources: the second one does not erase the mark of the first one
//   RR4  CR-R4  a newer datagram with the old cancelled input entry does not make it a new press
//   RR5  CR-R5  a broken join of a basic ranged action (no catalog entry, no source, no row) stops the tool, and the file stays
//   RR6  CR-R6  a release time or an aim value that is not a finite number stops the tool, and the file stays
//   RR7  CR-R7  the loader refuses wire keys that do not agree with the named ammunition and damage table
//
// The accounts, the characters, the creature kinds, the rows, and the places are made up.

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "support/cases.h"
#include "support/check.h"
#include "support/range_fixture.h"
#include "support/station_fixture.h"

namespace soulwarden::backend::tests {
namespace review_ranged_world {

using namespace range;
using nlohmann::json;

/** @return The action of an action state (slot 5, field 33): the scope index minus 1; -1: none, -2: no field. */
int action_of(const Fragment& fragment) {
    const auto field = fragment.fields.find({0, 33});
    if (field == fragment.fields.end()) {
        return -2;
    }
    server::hub::Reader reader(field->second);
    if (reader.size() == 0) {
        return -1;
    }
    reader.u16();
    reader.size();
    return static_cast<int>(reader.size()) - 1;
}

/** RR1 (CR-R1): the windup of the first shot of a creature with a required Spawn action. */
void spawn_windup(const Options&) {
    Range range(true);
    range.enter_both();
    range.behavior().set_ignored(kCharacterB, true);
    gamedata::CreatureSlice slice = archer_slice();
    slice.actions.push_back({"Spawn", "Spawn", 99, false});
    slice.spawnActionName = "Spawn";
    check(range.host.creatures.set_full({slice}, {archer_kind()}, {}, archer_levels(), {}, {arrow_slice()},
                                        gamedata::CreatureCapsule{kPlayerRadius, kPlayerSegment, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 0.0F}}),
          "setup: an archer with the required Spawn action");
    world::Entity* archer = range.archer(10.0F);
    const world::EntityId id = archer->id;
    const std::size_t fromA = range.a.got.size();
    const std::size_t fromB = range.b.got.size();
    check(range.until([&] { return range.shots().launches == 1; }), "the behavior releases its first shot");
    const world::EntityId projectile = newest_projectile(range);
    const std::uint32_t inA = interest_in(range.host, 1, id);
    const std::uint32_t inB = interest_in(range.host, 2, id);
    unsigned bowA = 0;
    unsigned bowB = 0;
    unsigned spawnA = 0;
    for (const Fragment& fragment : fragments_for(range.a, inA, fromA)) {
        if (fragment.slot == 5) {
            bowA += action_of(fragment) == static_cast<int>(kFireBow) ? 1 : 0;
            spawnA += action_of(fragment) == 6 ? 1 : 0;
        }
    }
    for (const Fragment& fragment : fragments_for(range.b, inB, fromB)) {
        if (fragment.slot == 5) {
            bowB += action_of(fragment) == static_cast<int>(kFireBow) ? 1 : 0;
        }
    }
    const bool arrowA = !projectile_states(range.a, interest_in(range.host, 1, projectile), fromA).empty();
    const bool arrowB = !projectile_states(range.b, interest_in(range.host, 2, projectile), fromB).empty();
    note("records: Spawn " + std::to_string(spawnA) + ", FireBow A " + std::to_string(bowA) + ", FireBow B " + std::to_string(bowB));
    check(spawnA == 1 && arrowA && arrowB, "each session gets the projectile, and A gets the action Spawn");
    check(bowA > 0 && bowB > 0, "each observer gets the windup (FireBow) before the release: the shot waits for the Spawn lead");
}

/** RR2 (CR-R2): a travel of the target in the windup. */
void travel_windup(const Options&) {
    Range range(false);
    range.host.world.set_shelter(std::chrono::seconds(10), std::chrono::seconds(60));
    world::Entity* archer = range.archer(10.0F);
    check(range.until([&] { return range.behavior().counts().shots == 1; }), "setup: a shot starts");
    const auto started = range.now();
    range.to(started + seconds(0.6F));
    const float health = range.character()->vitals.health;
    const world::TravelResult result = range.host.world.travel(kCharacterA, at(kPlace.x + 2.0F, kPlace.y));
    check(result.error == world::TravelError::none && archer->creature.mind.target == 0 && archer->creature.mind.mode == world::CreatureMode::home,
          "the travel takes the target from the archer, and it goes home");
    range.walker.position.x = kPlace.x + 2.0F;
    range.to(started + seconds(2.1F));
    note("after the travel: " + std::to_string(range.shots().launches) + " launches, " + std::to_string(range.shots().damage) + " hits");
    check(range.behavior().sheltered(*range.character()), "the target is in the shelter of its travel");
    check(range.shots().launches == 0 && range.character()->vitals.health == health && range.behavior().counts().releasesCancelled == 1,
          "the pending release of the windup does not follow the target: no launch, no damage, one cancelled release");
}

/** RR5 and RR6 (CR-R5, CR-R6): the review cases of the check of the creature tool. */
void tool_cases(const Options& options, const std::vector<const char*>& names, const char* text) {
    const std::filesystem::path tool = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_creature_tool.py";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::filesystem::path work = options.workDirectory / "check-creature-review";
    std::filesystem::create_directories(work);
    const std::string command = std::string(python != nullptr ? python : "python3") + " -B \"" + tool.string() + "\" \"" + work.string() + "\"";
    FILE* pipe = popen(command.c_str(), "r");
    std::string output;
    if (pipe != nullptr) {
        char buffer[512];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            output += buffer;
        }
    }
    const int status = pipe != nullptr ? pclose(pipe) : -1;
    bool all = true;
    for (const char* name : names) {
        const bool passed = output.find(std::string("ok    ") + name) != std::string::npos;
        if (!passed) {
            note(std::string("no passed case ") + name);
        }
        all = all && passed;
    }
    check(status == 0 && all, text);
}

void broken_joins(const Options& options) {
    tool_cases(options, {"review-control", "review-control-cast", "review-other-action", "review-missing-asset", "review-missing-source",
                         "review-missing-row"},
               "the tool stops with a cause for a basic ranged action with no catalog entry, no source, or no damage row of its "
               "projectile, and the file of an earlier run stays (a control file and a cast load; a different action is not a basic shot)");
}

void broken_times(const Options& options) {
    tool_cases(options, {"review-nonfinite-time", "review-malformed-time", "review-malformed-aim"},
               "the tool stops with a cause for a release time \"nan\" or \"0.1x\" and an aim value that is not a number, and the file "
               "of an earlier run stays (no time 0)");
}

/** RR7 (CR-R7): the wire keys of a release agree with its names. */
void loader_keys(const Options& options) {
    const gamedata::CreatureSlice slice = archer_slice();
    json good{{"format", 3},
              {"levels", {{1, 337.5, 145.52, 50.0}}},
              {"slices",
               {{{"path", slice.path},
                 {"asset", "31313131-3131-3131-3131-313131313131"},
                 {"sub", 2},
                 {"root", "1000000000000031"},
                 {"interestRadius", 60.0},
                 {"movement", 5},
                 {"vitals", 9},
                 {"marker", 15},
                 {"slots", {{5, "ActionList", 11}, {9, "Vitals", 15}, {15, "Marker", 5399}}},
                 {"components", {{"1000000000000031", "0000000000000A01", "Root/Vitals"}}},
                 {"actions", {{"Idle_No_Task", "Idle", 0, false}, {"Death", "r_Death", 4, true}, {"FireBow", "Fire_Bow", 35, false}}},
                 {"behavior", {{"sight", {75.0, 12.0}}, {"combatSight", {180.0, 30.0}}, {"leash", {20.0, 70.0}}, {"speed", {5.5, 5.5}}}}}}},
              {"kinds",
               {{{"id", "Test_Archer"}, {"vitals", "Test_Archer"}, {"category", "Test_Archer_Category"}, {"vitalsId", 0x31112222},
                 {"categoryId", 0x31112223}, {"name", "Test_Archer"}, {"vitalsName", "Test_Archer"}, {"family", "Human"},
                 {"creatureType", "Solo"}, {"health", 1.0}, {"level", 1}, {"bodies", {{0, 100}}}, {"points", 0}}}},
              {"spawnPoints", json::array()},
              {"routes", json::array()}};
    good["slices"][0]["ranged"] = json::array({json::array(
        {2, 2.46667,
         json::array({json{{"time", 1.33333}, {"profile", "AI_Bow"}, {"attack", "Basic"}, {"index", 7}, {"ammo", "Arrow_AI"},
                           {"ammoKey", crc_of("Arrow_AI")}, {"projectile", 0}, {"override", false}, {"table", "TestBowDamageTable"},
                           {"tableKey", crc_of("TestBowDamageTable")}, {"row", "Test_Row"}, {"rowIndex", 0}, {"factor", 0.35},
                           {"added", 0.0}, {"type", "Thrust"}, {"wire", 5}, {"category", "Physical"}, {"charge", 0.0}, {"lead", true},
                           {"aimSpeed", 50.0}, {"leadAngle", 10.0}, {"aimAngle", 45.0}, {"miss", {1.5, 3.0}}, {"range", 100.0},
                           {"radius", 0.0}, {"accelDelay", 0.0}}})})});
    good["projectiles"] = json::array({json::array({"slices/test/arrow.dynamicslice", "61616161-6161-6161-6161-616161616161", 2,
                                                    "6C1BE29704877324", 1, 50.0, 10.0, 1.5, json::array({0.0, 0.0, -2.0}), 1.5, 8.0})});
    const std::filesystem::path path = options.workDirectory / "review-ranged-keys.json";
    const auto load = [&](const json& document) {
        std::ofstream(path) << document.dump();
        gamedata::CreatureData data;
        return data.load(path.string());
    };
    check(load(good), "a complete ranged file loads");
    json keys = good;
    keys["slices"][0]["ranged"][0][2][0]["ammoKey"] = 0;
    keys["slices"][0]["ranged"][0][2][0]["tableKey"] = 0;
    json ammo = good;
    ammo["slices"][0]["ranged"][0][2][0]["ammoKey"] = crc_of("Arrow_Other");
    json table = good;
    table["slices"][0]["ranged"][0][2][0]["tableKey"] = crc_of("OtherDamageTable");
    json empty = good;
    empty["slices"][0]["ranged"][0][2][0]["ammo"] = "";
    check(!load(keys) && !load(ammo) && !load(table) && !load(empty),
          "the loader refuses the keys 0 for Arrow_AI and TestBowDamageTable, an ammunition key or a table key of a different name, and "
          "an ammunition key with no name");
    json cast = good;
    cast["slices"][0]["ranged"][0][2][0]["ammo"] = "";
    cast["slices"][0]["ranged"][0][2][0]["ammoKey"] = 0;
    check(load(cast), "a release with no ammunition and the key 0 (the cast of a mage) loads");
}

} // namespace review_ranged_world

namespace review_ranged_stations {

using namespace stations;

/** RR3 (CR-R3): two early cancels of different sources. */
void two_early_cancels(const Options&) {
    Stations shop(false);
    shop.enter(false);
    world::Entity* kegA = shop.fixture("powderKeg", 1.0F);
    world::Entity* kegB = shop.fixture("powderKeg", 2.0F);
    const InteractionBlock blockA = child_block(shop.host.interactions, *kegA, kTrigger);
    const InteractionBlock blockB = child_block(shop.host.interactions, *kegB, kTrigger);
    InputEntry entryA = report_entry(shop.sequenceA++, shop.placeA.x, shop.placeA.y, shop.placeA.z);
    entryA.interaction = blockA;
    const Bytes lateA = shop.clientA.input_report(entryA);
    send_cancel(shop, shop.clientA, interaction::kCharacterA, blockA);
    InputEntry entryB = report_entry(shop.sequenceA++, shop.placeA.x, shop.placeA.y, shop.placeA.z);
    entryB.interaction = blockB;
    (void)shop.clientA.input_report(entryB); // The press of B is late too; only the old press of A comes.
    send_cancel(shop, shop.clientA, interaction::kCharacterA, blockB);
    shop.host.datagram(shop.clientA.peer, lateA);
    shop.host.steps(520, {&shop.clientA});
    const std::uint32_t completions = shop.interactions().completions_of(kegA->id);
    note("the uses of the first keg: " + std::to_string(completions));
    check(completions == 0 && !shop.entity(interaction::kCharacterA)->interaction,
          "the cancel of the second keg does not erase the mark of the first one: its late press makes no use");
}

/** RR4 (CR-R4): the old cancelled input entry in a newer datagram. */
void repeated_early_press(const Options&) {
    Stations shop(false);
    shop.enter(false);
    world::Entity* keg = shop.fixture("powderKeg", 1.0F);
    const InteractionBlock block = child_block(shop.host.interactions, *keg, kTrigger);
    InputEntry entry = report_entry(shop.sequenceA++, shop.placeA.x, shop.placeA.y, shop.placeA.z);
    entry.interaction = block;
    const Bytes original = shop.clientA.input_report(entry);
    send_cancel(shop, shop.clientA, interaction::kCharacterA, block);
    // The client repeats the same input entry in a newer datagram (its redundant reports).
    const Bytes repeated = shop.clientA.input_report(entry);
    shop.host.datagram(shop.clientA.peer, repeated);
    shop.host.datagram(shop.clientA.peer, original);
    shop.host.steps(520, {&shop.clientA});
    const std::uint32_t completions = shop.interactions().completions_of(keg->id);
    note("the uses of the keg: " + std::to_string(completions));
    check(completions == 0, "a newer datagram does not make the old cancelled input entry a new press");
}

} // namespace review_ranged_stations

std::vector<Case> review_ranged_cases() {
    return {{"RR1", "review 3.4f-A CR-R1: a required Spawn action, and the windup of the first shot for each observer", review_ranged_world::spawn_windup},
            {"RR2", "review 3.4f-A CR-R2: a travel of the target in the windup: no launch", review_ranged_world::travel_windup},
            {"RR3", "review 3.4f-A CR-R3: two early cancels of different sources", review_ranged_stations::two_early_cancels},
            {"RR4", "review 3.4f-A CR-R4: the old cancelled input entry in a newer datagram", review_ranged_stations::repeated_early_press},
            {"RR5", "review 3.4f-A CR-R5: a broken join of a basic ranged action stops the tool (runs check_creature_tool.py)",
             review_ranged_world::broken_joins},
            {"RR6", "review 3.4f-A CR-R6: a time or an aim value that is not a number stops the tool (runs check_creature_tool.py)",
             review_ranged_world::broken_times},
            {"RR7", "review 3.4f-A CR-R7: the loader refuses wire keys that do not agree with the names", review_ranged_world::loader_keys}};
}

} // namespace soulwarden::backend::tests

// The corrections of the code review of stage 2.5b (findings ST-R1 to ST-R8; ~/Games/soulwarden/work/evidence/stations/code-review/
// FINDINGS.md), made in stage 3.4f-A. Each case fails on the tree of stage 2.5b (commit cc02d8c) and passes with its correction.
//
//   RY1  ST-R1: a client that ends a timed use at its full time or later, in a report that the world takes before the timer: one
//        completion; an end before the time: none
//   RY2  ST-R2: a cancel (2473) that the server gets before a press of an earlier datagram on a different channel: no use, no completion;
//        a press of a later datagram is a new use
//   RY3  ST-R3: a service grant ends when its character walks out of the range of its source (and does not come back with a return)
//   RY4  ST-R4: an accepted use of one time of a different source ends the service grant
//   RY5  ST-R5: the loader of the interaction data refuses two components of a template at one slot
//   RY6  ST-R6: the interaction tool stops for a named placement with no position, a list as its position, or no z
//   RY7  ST-R7: the interaction tool stops for a station or a forge child whose concrete slice asset the catalog does not have
//   RY8  ST-R8: the interaction tool stops for one placement id in two spellings of its UUID at two places
//
// RY6 to RY8 run tools/gamedata/check_interactions_tool.py --review-stations. All keys, ids, and positions are made up.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "support/cases.h"
#include "support/check.h"
#include "support/interaction_fixture.h"
#include "support/station_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace interaction;
using namespace stations;

/** RY1 (ST-R1): the end of a timed use at its full time. */
void full_time_release(const Options&) {
    for (const int elapsed : {5000, 5005, 5010}) {
        Stations shop(false);
        shop.enter(false);
        world::Entity* keg = shop.fixture("powderKeg", 1.0F);
        shop.report(shop.clientA, child_block(shop.host.interactions, *keg, kTrigger));
        const world::ActiveInteraction active = *shop.entity(kCharacterA)->interaction;
        check(active.due && *active.due - active.started == std::chrono::seconds(5), "setup: the timed use has its 5 s");
        shop.host.advance(std::chrono::milliseconds(elapsed));
        // The world takes the report of the client (no block: it ended its request) before its due timers.
        shop.host.world.post([&](world::World&) { shop.report(shop.clientA, std::nullopt); });
        shop.host.tick();
        shop.host.steps(10, {&shop.clientA});
        check(shop.interactions().completions_of(keg->id) == 1 && !shop.entity(kCharacterA)->interaction,
              "the client ends the timed use " + std::to_string(elapsed) + " ms after its start, before the timer: one completion");
    }
    // A release before the time stays a release.
    Stations shop(false);
    shop.enter(false);
    world::Entity* keg = shop.fixture("powderKeg", 1.0F);
    shop.report(shop.clientA, child_block(shop.host.interactions, *keg, kTrigger));
    shop.host.advance(std::chrono::milliseconds(4990));
    shop.host.world.post([&](world::World&) { shop.report(shop.clientA, std::nullopt); });
    shop.host.tick();
    shop.host.steps(300, {&shop.clientA});
    check(shop.interactions().completions_of(keg->id) == 0, "control: an end 10 ms before the time is a release: no completion");
}

/** RY2 (ST-R2): a cancel before a delayed press. */
void cancel_before_press(const Options&) {
    Stations shop(false);
    shop.enter(false);
    world::Entity* keg = shop.fixture("powderKeg", 1.0F);
    const InteractionBlock block = child_block(shop.host.interactions, *keg, kTrigger);
    // The client makes the report of the press first (its datagram), and its release (2473) after it; the report comes late.
    InputEntry entry = report_entry(shop.sequenceA++, shop.placeA.x, shop.placeA.y, shop.placeA.z);
    entry.interaction = block;
    const auto late = shop.clientA.input_report(entry);
    send_cancel(shop, shop.clientA, kCharacterA, block);
    shop.host.datagram(shop.clientA.peer, late);
    shop.host.steps(520, {&shop.clientA});
    check(shop.interactions().completions_of(keg->id) == 0 && !shop.entity(kCharacterA)->interaction,
          "a release that the server gets before its press of an earlier datagram: no use and no completion");
    check(log_count("is older than the cancel of its client") == 1, "the session logs the old request");
    // A new press after it (a later datagram) is a new use, and it completes.
    shop.report(shop.clientA, block);
    check(shop.entity(kCharacterA)->interaction.has_value(), "control: a press of a later datagram is a new use");
    shop.host.steps(520, {&shop.clientA});
    check(shop.interactions().completions_of(keg->id) == 1, "control: the new use completes after its 5 s");
}

/** RY3 (ST-R3): a walk out of the range of the source and back. */
void grant_range(const Options&) {
    Stations shop;
    shop.enter();
    world::Interactions& system = shop.interactions();
    const InteractionBlock talk = block_for(shop.host.interactions, kPerson, kConversation);
    shop.request(shop.clientA, talk);
    check(system.grant_service(*shop.entity(kCharacterA), "shop", {11}, 1), "setup: A has a grant of the person");
    shop.request(shop.clientB, talk);
    check(system.grant_service(*shop.entity(kCharacterB), "shop", {22}, 1), "setup: B has its own grant");
    send_cancel(shop, shop.clientA, kCharacterA, talk);
    shop.end_block(shop.clientA);
    for (int step = 1; step <= 12; ++step) {
        shop.placeA.x = 100.0F + 0.5F * static_cast<float>(step);
        shop.report(shop.clientA, std::nullopt);
        shop.host.steps(10, {&shop.clientA, &shop.clientB});
    }
    check(shop.entity(kCharacterA)->position.x > 105.9F && !shop.entity(kCharacterA)->positionEstimated
              && !shop.entity(kCharacterA)->serviceGrant,
          "A walks out of the range of the source: the grant ends at that position");
    for (int step = 11; step >= 0; --step) {
        shop.placeA.x = 100.0F + 0.5F * static_cast<float>(step);
        shop.report(shop.clientA, std::nullopt);
        shop.host.steps(10, {&shop.clientA, &shop.clientB});
    }
    check(system.require_service(kCharacterA, "shop", 11) == world::ServiceError::noGrant,
          "A walks back with no new conversation: the old grant does not come back");
    check(system.require_service(kCharacterB, "shop", 22) == world::ServiceError::none, "B keeps its grant");
}

/** RY4 (ST-R4): a use of one time of a different source. */
void grant_one_shot(const Options&) {
    Stations shop;
    shop.enter();
    world::Interactions& system = shop.interactions();
    const InteractionBlock talk = block_for(shop.host.interactions, kPerson, kConversation);
    shop.request(shop.clientA, talk);
    check(system.grant_service(*shop.entity(kCharacterA), "shop", {11}, 1), "setup: A has a grant of the person");
    send_cancel(shop, shop.clientA, kCharacterA, talk);
    shop.end_block(shop.clientA);
    const std::size_t from = shop.clientA.got.size();
    shop.request(shop.clientA, block_for(shop.host.interactions, kPage, kAddLore));
    check(replies(shop.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{50, 1}}
              && shop.host.store.list_achievements(kCharacterA).size() == 1,
          "setup: the use of one time of the lore page of a different object is accepted");
    check(system.require_service(kCharacterA, "shop", 11) == world::ServiceError::noGrant,
          "the accepted use of one time of a different source ends the grant");
}

/** RY5 (ST-R5): two components at one slot of a template. */
void slot_collision(const Options&) {
    StationParts good = station_parts();
    gamedata::InteractionData control;
    check(set_stations(control, good), "control: the complete data loads");
    StationParts bad = station_parts();
    bad.templates[kStationTemplate].positionSlot = bad.templates[kStationTemplate].bounds.front().slot; // PositionInTheWorld at slot 4
    gamedata::InteractionData data;
    check(!set_stations(data, bad) && data.error().find("the same slot") != std::string::npos,
          "the loader refuses a template with the PositionInTheWorld component and an Interact bound at one slot");
    StationParts twice = station_parts();
    twice.templates[kKegTemplate].ownershipSlot = twice.templates[kKegTemplate].positionSlot;
    gamedata::InteractionData other;
    check(!set_stations(other, twice), "the loader refuses a template with the Ownership and the PositionInTheWorld components at one slot");
}

/** The lines of the check of the interaction tool in its mode for the code review of stage 2.5b. */
std::vector<std::string> tool_lines(const Options& options) {
    const std::filesystem::path script =
        options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_interactions_tool.py";
    const std::filesystem::path work = options.workDirectory / "review-stations-tool";
    std::filesystem::create_directories(work);
    const auto output = options.workDirectory / "review-stations-tool.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + script.lexically_normal().string() + "\" \"" + work.string() + "\" --review-stations > \""
                                + output.string() + "\" 2>&1";
    (void)std::system(command.c_str());
    std::vector<std::string> lines;
    std::ifstream log(output);
    for (std::string line; std::getline(log, line);) {
        note(line.substr(0, 200));
        lines.push_back(line);
    }
    return lines;
}

/** @return True when the line of the case starts with "ok". */
bool passed(const std::vector<std::string>& lines, const std::string& text) {
    for (const std::string& line : lines) {
        if (line.find(text) != std::string::npos) {
            return line.rfind("ok", 0) == 0;
        }
    }
    return false;
}

/** RY6 (ST-R6): a named placement with an incomplete position. */
void tool_position(const Options& options) {
    const std::vector<std::string> lines = tool_lines(options);
    check(passed(lines, "control: a complete source with the station"), "control: a complete source gives the file with the station");
    check(passed(lines, "a named placement with no position") && passed(lines, "a list as its position")
              && passed(lines, "no z in its position"),
          "the tool stops and keeps the old file for a named placement with no position, a list, or no z (no coordinate 0)");
}

/** RY7 (ST-R7): a concrete child asset with no catalog path. */
void tool_child_join(const Options& options) {
    const std::vector<std::string> lines = tool_lines(options);
    check(passed(lines, "a station child whose concrete slice asset") && passed(lines, "a forge child whose concrete slice asset"),
          "the tool stops and keeps the old file for a station or a forge child whose slice asset the catalog does not have");
}

/** RY8 (ST-R8): one placement id in two spellings. */
void tool_uuid_case(const Options& options) {
    const std::vector<std::string> lines = tool_lines(options);
    check(passed(lines, "two spellings of its UUID"), "the tool stops for one settlement id in two spellings at two places");
}

} // namespace

std::vector<Case> review_station_cases() {
    return {{"RY1", "review of 2.5b, ST-R1: the end of a timed use at its full time before the timer: one completion", full_time_release},
            {"RY2", "review of 2.5b, ST-R2: a cancel before a delayed press of an earlier datagram: no use", cancel_before_press},
            {"RY3", "review of 2.5b, ST-R3: a walk out of the range of the source ends the service grant", grant_range},
            {"RY4", "review of 2.5b, ST-R4: a use of one time of a different source ends the service grant", grant_one_shot},
            {"RY5", "review of 2.5b, ST-R5: the loader refuses two components at one slot of a template", slot_collision},
            {"RY6", "review of 2.5b, ST-R6: the interaction tool and a placement with an incomplete position (runs "
                    "check_interactions_tool.py --review-stations)", tool_position},
            {"RY7", "review of 2.5b, ST-R7: the interaction tool and a child asset with no catalog path", tool_child_join},
            {"RY8", "review of 2.5b, ST-R8: the interaction tool and two spellings of one placement id", tool_uuid_case}};
}

} // namespace soulwarden::backend::tests

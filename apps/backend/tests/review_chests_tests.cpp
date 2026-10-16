// The cases of the code review of stage 3.5b.1 (findings CH-R1 to CH-R8), corrected in stage 3.5b.2. Each case fails on the tree of the
// commit of stage 3.5b.1 and passes with the correction. The cases come from the proof tests of the review (CHP1 to CHP5 and the export
// cases); RCH6 runs the review cases of the check of the chest tool (tools/gamedata/check_chests_tool.py). Made-up accounts, ids, names,
// and data only.
//
//   RCH1  CH-R1: a delayed input report of the held block, made before a matched cancel (or the echo of a server end), starts no open
//   RCH2  CH-R2: a release after the open time (a late world step) completes the open first: the timed use of the base
//   RCH3  CH-R3: an item of the roll that the catalog does not have: no item, and the claim keeps the complete roll (Rolled)
//   RCH4  CH-R4: the loader refuses a chest file with two chest keys of one actor
//   RCH5  CH-R8: the loader refuses a loot file of format 2 with no container floor for a level of the gear score ranges
//   RCH6  CH-R5, CH-R6, CH-R7: the review cases of the check of the chest tool

#include <chrono>
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
#include "support/chest_fixture.h"

namespace soulwarden::backend::tests {
namespace review_chests {

using namespace chests;
using namespace std::chrono_literals;
using nlohmann::json;

/** The cancel 2473 of the client A for a block. */
void cancel(ChestShop& shop, const InteractionBlock& block) {
    const world::Entity* own = shop.entity(kCharacterA);
    server::hub::Writer body;
    body.u64(own->id + server::rep::kInteractorComponent);
    body.u64(own->id);
    body.raw(block.actor);
    body.u64(block.group);
    body.u64(block.target);
    shop.host.datagram(shop.clientA.peer, shop.clientA.component_message(kCancelMessage, own->actor.bytes, body.data()));
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
}

/** RCH1 (CH-R1): the delayed report of the held block, made before the cancel, comes after it. */
void delayed_report_after_cancel(const Options&) {
    for (const bool serverEnded : {false, true}) {
        ChestShop shop;
        shop.enter();
        const InteractionBlock block = shop.open_block(kSupply);
        shop.request(shop.clientA, block);
        shop.hold(shop.clientA, block, 1000);
        InputEntry old = report_entry(shop.sequenceA++, shop.placeA.x, shop.placeA.y, shop.placeA.z);
        old.interaction = block;
        const auto delayed = shop.clientA.input_report(old); // Made before the cancel, delivered after it.
        if (serverEnded) {
            check(shop.host.world.interactions().cancel(kCharacterA), "setup: the world ends the first open (3746)");
        }
        cancel(shop, block);
        check(!shop.entity(kCharacterA)->interaction && shop.chests().open_count() == 0,
              serverEnded ? "setup: the echo of the server end comes, the open ended with no item"
                          : "setup: the matched cancel ends the open with no item");
        shop.host.datagram(shop.clientA.peer, delayed);
        shop.host.steps(280, {&shop.clientA, &shop.clientB});
        note("RCH1 server_ended=" + std::to_string(serverEnded) + " begins=" + std::to_string(shop.chests().counts().begins)
             + " completions=" + std::to_string(shop.chests().counts().completions)
             + " ore=" + std::to_string(bag_count(*shop.entity(kCharacterA), "TestOre")));
        check(shop.chests().counts().begins == 1 && shop.chests().counts().completions == 0
                  && bag_count(*shop.entity(kCharacterA), "TestOre") == 0,
              "the delayed report of the cancelled attempt starts no second open and gives no item");
        check(bag_count(*shop.entity(kCharacterB), "TestOre") == 0, "two sessions: the other character gets no item");
        // A new press in a later datagram is a new open (finding SU-R5 stays true).
        shop.request(shop.clientA, block);
        check(shop.entity(kCharacterA)->interaction.has_value() && shop.chests().counts().begins == 2,
              "control: a new press of the same chest in a later datagram starts an open");
    }
}

/** RCH2 (CH-R2): a report with no block after the open time, before the world step of the timer. */
void release_after_due(const Options&) {
    for (const bool tickFirst : {false, true}) {
        ChestShop shop;
        shop.enter();
        const InteractionBlock block = shop.open_block(kSupply);
        shop.request(shop.clientA, block);
        check(shop.entity(kCharacterA)->interaction && shop.entity(kCharacterA)->interaction->due,
              "the open of the chest is a timed use of the base: the use has the time of the open");
        shop.host.clock.advance(3000ms);
        if (tickFirst) {
            shop.host.tick();
        }
        shop.report(shop.clientA, std::nullopt);
        shop.host.steps(5, {&shop.clientA, &shop.clientB});
        note("RCH2 tick_first=" + std::to_string(tickFirst) + " completions=" + std::to_string(shop.chests().counts().completions)
             + " cancels=" + std::to_string(shop.chests().counts().cancels)
             + " ore=" + std::to_string(bag_count(*shop.entity(kCharacterA), "TestOre")));
        check(shop.chests().counts().completions == 1 && shop.chests().counts().cancels == 0
                  && bag_count(*shop.entity(kCharacterA), "TestOre") == 3,
              tickFirst ? "control: the timer first, then the release: the open completes"
                        : "the release after the open time completes the open before it ends it (the order of the input changes nothing)");
        check(bag_count(*shop.entity(kCharacterB), "TestOre") == 0, "two sessions: the other character gets no item");
    }
    // A release before the time stays a cancel.
    ChestShop shop;
    shop.enter();
    shop.request(shop.clientA, shop.open_block(kSupply));
    shop.host.clock.advance(1000ms);
    shop.report(shop.clientA, std::nullopt);
    shop.host.steps(5, {&shop.clientA, &shop.clientB});
    shop.host.clock.advance(3000ms);
    shop.host.steps(5, {&shop.clientA, &shop.clientB});
    check(shop.chests().counts().completions == 0 && shop.chests().counts().cancels == 1 && bag_count(*shop.entity(kCharacterA), "TestOre") == 0,
          "control: a release before the open time is a cancel, and the timer after it gives nothing");
}

/** RCH3 (CH-R3): the catalog has no TestSword, which the roll of the supply chest gives. */
void unknown_rolled_item(const Options&) {
    ChestShop shop;
    std::vector<gamedata::ItemDefinition> items = chest_items();
    std::erase_if(items, [](const gamedata::ItemDefinition& item) { return item.id == "TestSword"; });
    shop.host.catalog.set(std::move(items));
    shop.enter();
    shop.request(shop.clientA, shop.open_block(kSupply));
    shop.hold(shop.clientA, shop.open_block(kSupply), 2700);
    shop.end_block(shop.clientA);
    const std::vector<state::LootClaimRow> claims = shop.host.store.list_loot_claims(kCharacterA);
    note("RCH3 ore=" + std::to_string(bag_count(*shop.entity(kCharacterA), "TestOre"))
         + " claim_state=" + std::to_string(claims.empty() ? 0 : claims[0].state)
         + " retained_items=" + std::to_string(claims.empty() ? 0 : claims[0].items.size()));
    check(claims.size() == 1 && claims[0].state == 1 && claims[0].items.size() == 3 && bag_count(*shop.entity(kCharacterA), "TestOre") == 0,
          "an item that the catalog does not have: no item is given, and the claim keeps the complete roll (Rolled, 3 items)");
    check(tracker_state(shop.clientA, shop.chest(kSupply).actor) != world::chest_rules::kLooted, "the map of the client does not show the chest as looted");
    // The catalog gets the item (a new item file): the next open gives the kept roll, with no new roll.
    shop.host.catalog.set(chest_items());
    shop.request(shop.clientA, shop.open_block(kSupply));
    shop.hold(shop.clientA, shop.open_block(kSupply), 2700);
    shop.end_block(shop.clientA);
    const std::vector<state::LootClaimRow> after = shop.host.store.list_loot_claims(kCharacterA);
    check(bag_count(*shop.entity(kCharacterA), "TestSword") == 1 && bag_count(*shop.entity(kCharacterA), "TestOre") == 3
              && shop.chests().counts().rolls == 1 && after.size() == 1 && after[0].state == 2,
          "with the complete catalog the next open gives the kept roll (one roll) and stores Looted");
    check(bag_count(*shop.entity(kCharacterB), "TestOre") == 0 && shop.host.store.list_loot_claims(kCharacterB).empty(),
          "two sessions: the other character has no item and no claim");
}

std::string hex_of(const std::array<std::uint8_t, 16>& bytes) {
    std::string text;
    for (const std::uint8_t byte : bytes) {
        char digits[3];
        std::snprintf(digits, sizeof(digits), "%02x", byte);
        text += digits;
    }
    return text;
}

/** A chest file of format 1 with the chests of the fixture. @param sameActor True: the first chest has the actor of the second one. */
json chest_file(bool sameActor) {
    json document{{"format", 1U},
                  {"rules", {{"resetMinutes", 60U}, {"countLimit", 60U}}},
                  {"gatherables", json::object()},
                  {"chests", json::array()}};
    for (const gamedata::ChestGatherable& row : chest_gatherables()) {
        document["gatherables"][row.id] = {{"open", row.openSeconds}, {"resetMin", row.resetMin}, {"resetMax", row.resetMax},
                                           {"elite", row.elite},      {"event", row.event}};
    }
    const std::vector<gamedata::ChestDefinition> definitions = chest_definitions();
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        const gamedata::ChestDefinition& chest = definitions[index];
        document["chests"].push_back({{"key", chest.key},
                                      {"actor", hex_of(sameActor && index == 0 ? definitions[1].actor : chest.actor)},
                                      {"position", {chest.position.x, chest.position.y, chest.position.z}},
                                      {"variant", chest.variant},
                                      {"name", chest.name},
                                      {"root", chest.root},
                                      {"reason", chest.reason},
                                      {"region", chest.region},
                                      {"kind", gamedata::chest_kind_text(chest.kind)},
                                      {"state", chest.available ? "ordinary" : "unavailable"},
                                      {"gatherable", chest_gatherables()[chest.gatherable].id},
                                      {"tags", chest.tags},
                                      {"contentLevel", chest.contentLevel ? json(*chest.contentLevel) : json(nullptr)}});
    }
    return document;
}

/** RCH4 (CH-R4): two chest keys with one actor. */
void repeated_actor(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    for (const bool sameActor : {false, true}) {
        ChestShop shop;
        const std::filesystem::path path = options.workDirectory / (sameActor ? "review-chest-same-actor.json" : "review-chest-actors.json");
        std::ofstream(path) << chest_file(sameActor).dump();
        const bool loaded = shop.host.chests.load(path.string());
        note("RCH4 same_actor=" + std::to_string(sameActor) + " accepted=" + std::to_string(loaded)
             + (loaded ? "" : " (" + shop.host.chests.error() + ")"));
        check(sameActor ? !loaded : loaded, sameActor ? "the loader refuses a chest file with two chest keys of one actor"
                                                      : "control: the file with one actor for each chest loads");
    }
}

/** A loot file of format 2 with the loot of the fixture. @param missingFloor True: no container floor row for the level 12. */
json loot_file(bool missingFloor) {
    const gamedata::LootData data = chest_loot();
    json document{{"format", 2U},
                  {"rewards", json::array()},
                  {"modifiers", {{"Solo", {1, 1, 0, 1, nullptr}}}},
                  {"disparity", json::array()},
                  {"events", {{"killevent", 0.05}}},
                  {"credit", {{"timeLimit", 120U}, {"healthShare", 0.01}, {"range", 200U}, {"receivers", 1U}, {"modifiers", {{1, 1, 1}}}}},
                  {"gearScore", json::array()},
                  {"lootCeiling", json::array()},
                  {"containerFloor", json::array()},
                  {"chestRoots", data.chestRoots},
                  {"limits", json::object()},
                  {"bases", json::array()},
                  {"vitals", json::object()},
                  {"tables", json::object()},
                  {"buckets", json::object()}};
    for (std::size_t index = 0; index < data.rewards.size(); ++index) {
        const std::uint32_t level = static_cast<std::uint32_t>(index + 1);
        const auto& reward = data.rewards[index];
        document["rewards"].push_back({level, reward.experience, reward.coin, reward.progression});
        json ranges = json::array();
        for (const auto& [low, high] : data.gearScore[index]) {
            ranges.push_back(low);
            ranges.push_back(high);
        }
        document["gearScore"].push_back(ranges);
        document["lootCeiling"].push_back({level, data.lootCeiling.at(level)});
        if (!missingFloor || level != 12) {
            document["containerFloor"].push_back({level, data.containerFloor.at(level)});
        }
    }
    for (const gamedata::LootTable& table : data.tables) {
        json entries = json::array();
        for (const gamedata::LootEntry& entry : table.entries) {
            const char* kind = entry.kind == gamedata::LootEntryKind::table ? "t" : entry.kind == gamedata::LootEntryKind::bucket ? "b" : "i";
            entries.push_back({kind, entry.id, entry.threshold, entry.quantityMin, entry.quantityMax, entry.gearScoreMin, entry.gearScoreMax});
        }
        document["tables"][table.id] = {table.all ? 1 : 0,       static_cast<unsigned>(table.bonus), table.maxRoll, table.conditions,
                                        table.conditionOverridesRoll, table.useLevelGearScore,     table.gearScoreBonus, entries};
    }
    return document;
}

/** RCH5 (CH-R8): a format 2 loot file with no container floor of the level 12. */
void missing_container_floor(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    for (const bool missing : {false, true}) {
        ChestShop shop;
        const std::filesystem::path path = options.workDirectory / (missing ? "review-loot-no-floor.json" : "review-loot-floors.json");
        std::ofstream(path) << loot_file(missing).dump();
        const bool accepted = shop.host.loot.load(path.string());
        note("RCH5 missing_floor=" + std::to_string(missing) + " accepted=" + std::to_string(accepted));
        check(missing ? !accepted : accepted, missing ? "the loader refuses a format 2 file with no container floor for a level of the gear score ranges"
                                                      : "control: the complete format 2 file loads");
        if (!accepted) {
            continue;
        }
        shop.enter();
        shop.request(shop.clientA, shop.open_block(kSupply));
        shop.hold(shop.clientA, shop.open_block(kSupply), 2700);
        shop.end_block(shop.clientA);
        std::uint32_t score = 0;
        for (const world::BagSlot& slot : shop.entity(kCharacterA)->bag.slots) {
            if (slot.item && slot.item->itemId == "TestSword") {
                score = slot.item->gearScore;
            }
        }
        check(score == kFloor, "control: the sword of the complete file has the container floor " + std::to_string(kFloor));
    }
}

/** RCH6 (CH-R5, CH-R6, CH-R7): the review cases of the check of the chest tool. */
void chest_tool_joins(const Options& options) {
    const std::filesystem::path tool = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_chests_tool.py";
    check(std::filesystem::is_regular_file(tool), "the check of the chest tool is in the tree");
    const std::filesystem::path work = options.workDirectory / "check-chests-review";
    std::filesystem::create_directories(work);
    const std::filesystem::path output = options.workDirectory / "check-chests-review.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string cases = " valid missing-metadata missing-territory wrong-record-first record-conflict";
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + tool.lexically_normal().string() + "\" \"" + work.string() + "\"" + cases + " > \"" + output.string()
                                + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream log(output);
    std::set<std::string> passed;
    for (std::string line; std::getline(log, line);) {
        if (line.rfind("ok    ", 0) == 0) {
            passed.insert(line.substr(6, line.find(':') - 6));
            note(line.substr(0, 120));
        } else if (line.rfind("FAIL", 0) == 0) {
            note(line.substr(0, 200));
        }
    }
    check(status == 0, "each review case of the check of the chest tool passes");
    check(passed.contains("missing-metadata"), "CH-R5: a chest that can be ordinary and has no record stops the tool, the earlier file stays");
    check(passed.contains("missing-territory"), "CH-R6: a record that names a territory with no row stops the tool, the earlier file stays");
    check(passed.contains("wrong-record-first") && passed.contains("record-conflict"),
          "CH-R7: only a record of the gatherable row of the variation is the record of a chest; two such records that differ stop the tool");
}

} // namespace review_chests

std::vector<Case> review_chests_cases() {
    using namespace review_chests;
    return {{"RCH1", "review of stage 3.5b.1, CH-R1: a delayed report of the held block after a cancel starts no open", delayed_report_after_cancel},
            {"RCH2", "review of stage 3.5b.1, CH-R2: a release after the open time completes the open first", release_after_due},
            {"RCH3", "review of stage 3.5b.1, CH-R3: an item that the catalog does not have keeps the complete roll", unknown_rolled_item},
            {"RCH4", "review of stage 3.5b.1, CH-R4: one chest for each actor of the chest file", repeated_actor},
            {"RCH5", "review of stage 3.5b.1, CH-R8: a container floor for each level of a format 2 loot file", missing_container_floor},
            {"RCH6", "review of stage 3.5b.1, CH-R5 to CH-R7: the record join of the chest tool", chest_tool_joins}};
}

} // namespace soulwarden::backend::tests

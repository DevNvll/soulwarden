// Cases from the code review of stage 3.7a (the fixed crafts). Each one is a fault that the review found, and the case passes only with the
// correction (each one fails on the tree of stage 3.7a, 4c6f77d). All items, recipes, and ids are made up.
//
//   CRV1  CR7A-R1: the plan of a batch makes the jobs in their order: a bag with one free slot and stacks of 3 refuses a batch of 3 knives
//         (before: accepted, then stopped after the first knife), and a batch of 1 is accepted
//   CRV2  CR7A-R2: a recipe with no cause and a rule that this stage does not apply (an unlock, an achievement, a procedural result, a
//         cooldown, ...): the loader refuses the file; the same rule with a cause loads
//   CRV3  CR7A-R3: the data tool stops at an ingredient entry with a type and no id, and the earlier file stays (check_crafting_tool.py,
//         case incomplete)
//   CRV4  CR7A-R4: a refused request and an accepted request in one tick: the session sends no stop form for the new batch, and the batch
//         completes

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "support/cases.h"
#include "support/check.h"
#include "support/crafting_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace crafting;

const std::vector<Entry> kKnifeEntries{entry("TestFlintT1"), entry("TestWoodT1")};

/** A in the world at the station with 1000 coin, and `count` Flint and Wood (one stack each). */
void ready(Crafter& shop, std::uint32_t count) {
    shop.enter(false);
    check(shop.host.world.inventory().set_coin(kCharacterA, 1000).ok(), "setup: the coin of A is 1000");
    shop.give(kCharacterA, "TestFlintT1", count);
    shop.give(kCharacterA, "TestWoodT1", count);
    shop.use_station(shop.clientA);
    check(shop.host.world.interactions().station(*shop.entity(kCharacterA)).has_value(), "setup: A uses the station");
}

/** CRV1: CR7A-R1, the plan of a batch job by job. */
void batch_in_order(const Options&) {
    Crafter shop(false);
    ready(shop, 3);
    world::Entity* a = shop.entity(kCharacterA);
    std::uint32_t occupied = 0;
    for (const world::BagSlot& slot : a->bag.slots) {
        occupied += slot.item ? 1 : 0;
    }
    // The bag has one free slot: 497 fillers (a stack of 1 each), the stack of 3 Flint, the stack of 3 Wood.
    shop.give(kCharacterA, "TestFiller", world::kMaxContainerSlots - occupied - 1);
    const auto one = shop.host.world.inventory().plan_craft(kCharacterA, {{"TestFlintT1", 1}, {"TestWoodT1", 1}}, {"TestKnifeT1", 1}, 1);
    const auto three = shop.host.world.inventory().plan_craft(kCharacterA, {{"TestFlintT1", 1}, {"TestWoodT1", 1}}, {"TestKnifeT1", 1}, 3);
    check(one.ok() && three.error == world::ItemError::bagFull,
          "the plan of 1 job fits; the plan of 3 jobs is bagFull: job 1 takes the free slot, and job 2 has no slot (each stack keeps 2, then 1)");
    const std::size_t from = shop.clientA.got.size();
    shop.craft(shop.clientA, kCharacterA, "TestKnifeT1", 3, kKnifeEntries);
    const bool accepted = shop.crafting().batch_count() == 1;
    shop.finish();
    const auto sent = progresses(shop.clientA, from);
    check(!accepted && shop.count(kCharacterA, "TestKnifeT1") == 0 && shop.count(kCharacterA, "TestFlintT1") == 3
              && shop.count(kCharacterA, "TestWoodT1") == 3 && shop.entity(kCharacterA)->wallet.coin == 1000 && sent.size() == 1
              && sent[0].recipe.empty() && sent[0].total == 0 && log_count("refused: the bag has no place for the whole output") == 1,
          "a request of 3 knives with an unchanged bag is refused at the request (no batch, no cost, no fee, the stop form), not stopped "
          "after the first knife");
    shop.craft(shop.clientA, kCharacterA, "TestKnifeT1", 1, kKnifeEntries);
    shop.finish();
    check(shop.count(kCharacterA, "TestKnifeT1") == 1 && shop.count(kCharacterA, "TestFlintT1") == 2 && shop.count(kCharacterA, "TestWoodT1") == 2
              && shop.entity(kCharacterA)->wallet.coin == 999,
          "control: a request of 1 knife is accepted and completes (its cost, its output, the fee)");

    // The cost of the plan job by job: the largest count of the client (10,000) with a stackable output and a full bag.
    Crafter large(false);
    large.enter(false);
    large.give(kCharacterA, "TestWoodT1", 10000);
    std::uint32_t used = 0;
    for (const world::BagSlot& slot : large.entity(kCharacterA)->bag.slots) {
        used += slot.item ? 1 : 0;
    }
    large.give(kCharacterA, "TestFiller", world::kMaxContainerSlots - used - 2);
    const auto start = std::chrono::steady_clock::now();
    const auto plan = large.host.world.inventory().plan_craft(kCharacterA, {{"TestWoodT1", 1}}, {"TestBar", 1}, 10000);
    const auto time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    note("the plan of 10,000 jobs in a bag of 500 slots: " + std::to_string(time.count()) + " ms");
    check(plan.ok() && time.count() < 500, "the plan of 10,000 jobs (10 stacks of Wood, 10 stacks of bars, 488 other items) takes less than 0.5 s");
}

/** CRV2: CR7A-R2, a recipe with no cause and a rule that the stage does not apply. */
void rules_and_causes(const Options&) {
    const std::vector<std::pair<std::string, nlohmann::json>> rules{
        {"known", false},          {"requiredAchievement", "Test_Unlock"}, {"hiddenByAchievement", "Test_Hidden"},
        {"unlockedAchievement", "Test_Unlocked"}, {"firstCraftAchievement", "Test_First"}, {"procedural", true},
        {"refining", true},        {"craftAll", true},                     {"temporary", true},
        {"event", "Craft_Test"},   {"cooldownSeconds", 600},               {"cooldownQuantity", 1},
        {"perkItems", 1},          {"attributeSlots", 1},                  {"perkCost", 5},
        {"baseGearScore", 200},    {"skipGrant", true},                    {"worldTags", "Test_Season"}};
    int refused = 0;
    int withCause = 0;
    for (const auto& [field, value] : rules) {
        nlohmann::json document = nlohmann::json::parse(craft_document());
        document["recipes"][0][field] = value;
        gamedata::CraftingData data;
        const bool loaded = data.set(document.dump());
        refused += loaded ? 0 : 1;
        if (loaded) {
            note("loaded with no cause and the rule " + field);
        }
        document["recipes"][0]["unsupported"] = nlohmann::json::array({"made-up cause"});
        gamedata::CraftingData marked;
        withCause += marked.set(document.dump()) ? 1 : 0;
    }
    check(refused == static_cast<int>(rules.size()),
          "a recipe with no cause and an unlock, an achievement rule, a procedural result, refining, craft-all, a temporary recipe, an "
          "event, a cooldown, perk items, a gear score rule, SkipGrantItems, or world tags: the loader refuses the file (" + std::to_string(refused)
              + " of " + std::to_string(rules.size()) + ")");
    check(withCause == static_cast<int>(rules.size()), "control: the same rule on a recipe with a cause loads (the recipe is refused at a request)");

    // The whole path: a file with a locked recipe and no cause gives no knife to a character with no unlock.
    Crafter shop(false, false);
    nlohmann::json document = nlohmann::json::parse(craft_document());
    document["recipes"][0]["known"] = false;
    document["recipes"][0]["requiredAchievement"] = "Test_Unlock";
    const bool loaded = shop.host.crafting.set(document.dump());
    ready(shop, 2);
    shop.craft(shop.clientA, kCharacterA, "TestKnifeT1", 1, kKnifeEntries);
    shop.finish();
    check(!loaded && shop.count(kCharacterA, "TestKnifeT1") == 0 && shop.count(kCharacterA, "TestFlintT1") == 2,
          "the request for the locked recipe of such a file makes no knife (the backend has no crafting data)");
}

/** CRV3: CR7A-R3, the data tool and a typed ingredient with no id. */
void typed_entry_with_no_id(const Options& options) {
    const std::filesystem::path script = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_crafting_tool.py";
    const std::filesystem::path work = options.workDirectory / "review-37a-tool";
    std::filesystem::create_directories(work);
    const std::filesystem::path output = work / "tool.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + script.lexically_normal().string() + "\" \"" + work.string() + "\" incomplete > \"" + output.string()
                                + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream log(output);
    int passed = 0;
    for (std::string line; std::getline(log, line);) {
        note(line.substr(0, 200));
        passed += line.rfind("ok", 0) == 0 && line.find("incomplete:") != std::string::npos ? 1 : 0;
    }
    check(status == 0 && passed == 5,
          "the crafting data tool stops at an entry of the type Item, Category_Only, or Currency with no id, and the earlier file stays; an "
          "id with no type is not an ingredient (check_crafting_tool.py, case incomplete)");
}

/** CRV4: CR7A-R4, a refused request and an accepted request in one tick. */
void refusal_before_batch(const Options&) {
    Crafter shop(false);
    ready(shop, 2);
    world::Entity* a = shop.entity(kCharacterA);
    const std::size_t from = shop.clientA.got.size();
    // The first request has one entry for two ingredients (refused); the second one is correct. Both arrive before the next tick.
    Bytes body = craft_body(crafting_target(a->id), a->id, gamedata::item_id_crc("TestKnifeT1"), 1, 1, 100, {entry("TestFlintT1")});
    shop.host.datagram(shop.clientA.peer, shop.clientA.component_message(server::rep::kTypeCraftRequest, a->actor.bytes, body));
    body = craft_body(crafting_target(a->id), a->id, gamedata::item_id_crc("TestKnifeT1"), 1, 1, 100, kKnifeEntries);
    shop.host.datagram(shop.clientA.peer, shop.clientA.component_message(server::rep::kTypeCraftRequest, a->actor.bytes, body));
    shop.host.steps(2, {&shop.clientA});
    const auto sent = progresses(shop.clientA, from);
    bool stopForm = false;
    for (const Progress& progress : sent) {
        stopForm = stopForm || (progress.recipe.empty() && progress.total == 0);
    }
    check(shop.crafting().batch_count() == 1 && !stopForm && log_count("craft request of character " + std::string(kCharacterA) + " refused") == 1,
          "the refused request and then the accepted one in one tick: the batch runs, and the session sends no stop form (the client would "
          "end its crafting and send 2999, which stops the new batch)");
    if (stopForm) {
        shop.stop(shop.clientA, kCharacterA); // The answer of the client to a stop form.
    }
    shop.finish();
    const auto later = progresses(shop.clientA, from);
    check(shop.count(kCharacterA, "TestKnifeT1") == 1 && shop.count(kCharacterA, "TestFlintT1") == 1 && !later.empty()
              && later.back().recipe == "TestKnifeT1" && later.back().completed == 1 && later.back().total == 1,
          "the batch completes: one knife, its cost, and the final progress 1 of 1");
}

} // namespace

std::vector<Case> review_37a_cases() {
    return {
        {"CRV1", "review of stage 3.7a, CR7A-R1: the plan of a batch makes its jobs in order (a bag that has no place for job 2)", batch_in_order},
        {"CRV2", "review of stage 3.7a, CR7A-R2: a recipe with no cause and a rule that the stage does not apply: the file is refused", rules_and_causes},
        {"CRV3", "review of stage 3.7a, CR7A-R3: the crafting data tool stops at a typed ingredient with no id (runs check_crafting_tool.py)",
         typed_entry_with_no_id},
        {"CRV4", "review of stage 3.7a, CR7A-R4: a refusal in the same tick sends no stop form for a newer accepted batch", refusal_before_batch},
    };
}

} // namespace soulwarden::backend::tests

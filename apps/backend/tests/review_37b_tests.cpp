// The corrections of the code review of stage 3.7b (~/Games/soulwarden/work/evidence/crafting/37b/code-review/FINDINGS.md), each with
// made-up data:
//   CR7B1  CR7B-R1: the trade skill experience of a batch is the floor of the raw total with its first-craft part (a reward of 11.125)
//   CR7B2  CR7B-R2: the harness commands set_craft_seed and set_craft_cooldown refuse through the admin routes with no harness gate
//   CR7B3  CR7B-R3: a first-craft achievement whose bit differs between two sources gives the bonus one time in a batch
// CR7B-R4 corrected one sentence of docs/systems/crafting.md (decision D2): no code change.

#include <algorithm>
#include <cmath>
#include <string>

#include <nlohmann/json.hpp>

#include "server/http/admin_access.h"
#include "server/http/request_handler.h"
#include "server/http/router.h"
#include "server/http/routes.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/crafting_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace crafting;
using nlohmann::json;

/** CR7B1: the experience of a batch with a fractional reward. */
void fractional_experience(const Options&) {
    for (const double reward : {11.0, 11.125}) {
        Refiner shop(true);
        json document = json::parse(refine_document());
        document["events"]["Craft_TestSmelting"]["reward"] = reward;
        check(shop.host.crafting.set(document.dump()), "setup: the data with the reward " + std::to_string(reward));
        shop.enter(true);
        shop.host.world.inventory().set_coin(kCharacterA, 1000);
        shop.give(kCharacterA, "TestOre", 60);
        shop.use_station(shop.clientA);
        shop.craft(shop.clientA, kCharacterA, "TestRefine", 3, {entry("TestOre")});
        shop.finish();
        const std::uint64_t first = shop.crafting().counts().experience;
        shop.craft(shop.clientA, kCharacterA, "TestRefine", 10, {entry("TestOre")});
        shop.finish();
        const std::uint64_t later = shop.crafting().counts().experience - first;
        // craftingstatspanel.luac UpdateSkillAmount: floor(raw x n + raw x (3 - 1)) for a first batch, floor(raw x n) later.
        const auto expectedFirst = static_cast<std::uint64_t>(std::floor(reward * 4.0 * 5.0));
        const auto expectedLater = static_cast<std::uint64_t>(std::floor(reward * 4.0 * 10.0));
        note("CR7B1 reward " + std::to_string(reward) + ": first batch " + std::to_string(first) + " (" + std::to_string(expectedFirst) +
             "), later batch " + std::to_string(later) + " (" + std::to_string(expectedLater) + ")");
        check(first == expectedFirst && later == expectedLater,
              "the batch gives the floor of its raw total with the first-craft part (11.125: 222 and 445, not 220 and 440)");
        check(shop.smelting(kCharacterB) == std::make_pair(0U, std::uint64_t{0}), "the other character gets no experience");
    }
}

/** CR7B2: the harness commands need the harness gate of the process also through the admin routes. */
void harness_commands(const Options&) {
    const HarnessGate off(false);
    Refiner shop(true);
    shop.enter(true);
    server::http::Router router;
    server::http::AdminAccess access;
    constexpr const char* kToken = "review-37b-made-up-token-0123456789abcdef";
    check(access.add(kToken, shop.a.steamId), "setup: the admin token of the account of A");
    const server::http::Services services{shop.host.store, "127.0.0.1:32210", "test-version", "", shop.host.tables, shop.host.world};
    server::http::register_admin_routes(router, services, access);
    shop.host.tick();
    const auto command = [&](const char* name, const json& arguments) {
        service::HttpRequest request;
        request.method = "POST";
        request.path = "/admin/v1/commands";
        request.target = request.path;
        request.body = json{{"command", name}, {"character", kCharacterA}, {"args", arguments}}.dump();
        request.headers.emplace_back(server::http::kAdminTokenHeader, kToken);
        return server::http::handle_request(router, request);
    };
    check(shop.crafting().set_cooldown(kCharacterA, "TestRefineCd", 10, 100).empty(), "setup: a window with 10 of 10 used");
    shop.host.world.set_random_seed(9);
    const service::HttpResponse seed = command("set_craft_seed", {{"seed", 117}});
    const service::HttpResponse cooldown = command("set_craft_cooldown", {{"recipe", "TestRefineCd"}, {"used", 0}, {"seconds", 0}});
    std::uint64_t state = 9;
    const bool kept = shop.host.world.random_unit() == world::World::next_unit(state);
    const std::uint32_t left = shop.crafting().cooldown_left(*shop.entity(kCharacterA), *shop.host.crafting.recipe_of("TestRefineCd"));
    check(seed.status == 403 && cooldown.status == 403 && left == 0 && kept,
          "with no SOULWARDEN_HARNESS=1: the seed and the cooldown commands of the admin route are refused (403), and nothing changes");
    const HarnessGate on(true);
    check(command("set_craft_seed", {{"seed", 117}}).status == 200
              && command("set_craft_cooldown", {{"recipe", "TestRefineCd"}, {"used", 1}, {"seconds", 100}}).status == 200,
          "with the harness gate the commands run");
    check(shop.entity(kCharacterB)->craftCooldowns.empty(), "the other character has no cooldown change");
}

/** CR7B3: one bit for one first-craft fact. */
void achievement_bits(const Options&) {
    for (const bool conflicting : {false, true}) {
        Refiner shop(true);
        gamedata::ProgressionData progression = refine_progression();
        progression.achievements.push_back({"FC_TestRefine", kFirstCraftBit + (conflicting ? 1U : 0U)});
        shop.host.progression.set(progression);
        check(shop.host.progression.loaded(), "setup: the progression tables with the achievement");
        shop.enter(true);
        shop.host.world.inventory().set_coin(kCharacterA, 1000);
        shop.give(kCharacterA, "TestOre", 12);
        shop.use_station(shop.clientA);
        shop.craft(shop.clientA, kCharacterA, "TestRefine", 3, {entry("TestOre")});
        shop.finish();
        const auto rows = shop.host.store.list_achievements(kCharacterA);
        const auto facts = std::count(rows.begin(), rows.end(), "FC_TestRefine");
        const std::uint64_t experience = shop.crafting().counts().experience;
        const std::uint64_t firsts = shop.crafting().counts().firstCrafts;
        note("CR7B3 conflicting " + std::to_string(conflicting) + ": rows " + std::to_string(facts) + ", experience " + std::to_string(experience) +
             ", first crafts " + std::to_string(firsts));
        check(facts == 1 && experience == 220 && firsts == 1,
              std::string("one first-craft bonus in a batch of 3 (220), ") + (conflicting ? "also with two bits in two sources" : "one bit"));
        check(shop.smelting(kCharacterB) == std::make_pair(0U, std::uint64_t{0}), "the other character gets no bonus");
    }
}

} // namespace

std::vector<Case> review_37b_cases() {
    return {{"CR7B1", "review of stage 3.7b, CR7B-R1: the experience of a batch is the floor of its raw total (a fractional reward)",
             fractional_experience},
            {"CR7B2", "review of stage 3.7b, CR7B-R2: the seed and cooldown commands need the harness gate through the admin routes",
             harness_commands},
            {"CR7B3", "review of stage 3.7b, CR7B-R3: one first-craft bonus with two bits of one achievement in two sources", achievement_bits}};
}

} // namespace soulwarden::backend::tests

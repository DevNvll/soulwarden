// The cases of stage 3.7b (docs/systems/crafting.md, protocol notes 4.51): refining and the reward progress of a craft.
//
// The ids CRB1 to CRB3 of the research (crafting-stage.md section 10) are not free (CRB1 is a case of stage 3.4f-B): the cases of this
// stage are CRAFTB1 to CRAFTB-CMD.
//
//   CRAFTB1     the bonus items: the chance of the recipe and of the materials (the tier lists, ExtraBonusItemChance), the roll bonus points
//             (IngredientBonusPrimary and Secondary, the RollBonus of the rank, the ROL effects, DisallowBonusesToGS), the extra units of
//             decision D2 (OutputQty x the chance; a chance above 1; floor and one draw), the mean of many draws, a seeded batch at the
//             station (the counts of the preview with the seed, the bag, the cause ItemAdded before the last job, ItemCreated at the last)
//   CRAFTB2     the trade skill experience: 44 for each job of TestRefine, the first craft (88 one time, across a rank), its achievement row
//             and bit, the next batch with no bonus, a restart (the row gives the bit), the state of the achievements for the client
//   CRAFTB3     the cooldown: a window of 86,400 s at the first craft, the state 205 (the end on the synced wall clock in nanoseconds), a
//             count above the crafts that are left, 10 of 10, the end of the window on a controlled clock, a restart (the run clock)
//   CRAFTB-DATA the loader of format 2: the rules of a recipe with no cause (an event, a first craft, a cooldown, refining with no rank
//             table), the format 1 of stage 3.7a with its rules
//   CRAFTB-MP   two sessions: the cooldown, the experience, and the first craft of A do not change B; B gets no state 205 of A
//   CRAFTB-LOAD two batches of 1,000 jobs with 100 more characters with 1,000 cooldowns each and 20,000 recipes of the data: the same visits,
//             in less than 2 s
//   CRAFTB-CMD  the commands: the preview with the bonus, the experience, the cooldown, and a seed; the cooldown command; the seed command

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "server/http/request_handler.h"
#include "server/http/router.h"
#include "server/http/routes.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/crafting_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace crafting;

constexpr std::uint32_t kCraftingSlot = 63;
constexpr std::uint32_t kCraftingType = 205;
constexpr std::uint32_t kAchievementSlot = 66;

/** One entry of the cooldown map of state 205 that a client got. */
struct WireCooldown {
    std::uint32_t recipe{0};
    std::uint8_t used{0};
    std::uint64_t end{0};
};

/** The two maps of a fragment of state 205 (protocol notes 4.51). */
struct WireCrafting {
    bool ok{false};
    std::uint64_t sequence{0};
    std::vector<WireCooldown> cooldowns;
    std::uint32_t bonuses{0};
};

WireCrafting read_crafting(const Fragment& fragment) {
    WireCrafting state;
    if (fragment.type != kCraftingType) {
        return state;
    }
    server::hub::Reader cooldowns(field_of(fragment, 0, 0));
    if (cooldowns.size() != 0 || cooldowns.u8() != 1) {
        return state;
    }
    state.sequence = cooldowns.size64();
    const std::uint32_t count = cooldowns.size();
    for (std::uint32_t index = 0; index < count; ++index) {
        WireCooldown entry;
        entry.recipe = cooldowns.u32();
        entry.used = cooldowns.u8();
        entry.end = cooldowns.u64();
        state.cooldowns.push_back(entry);
    }
    server::hub::Reader bonuses(field_of(fragment, 0, 1));
    const bool bonusHead = bonuses.size() == 0 && bonuses.u8() == 1 && bonuses.size64() == state.sequence;
    state.bonuses = bonuses.size();
    state.ok = cooldowns.ok() && cooldowns.left() == 0 && bonusHead && bonuses.ok() && bonuses.left() == 0;
    return state;
}

std::uint64_t verdicts(world::Crafting& crafting, world::CraftVerdict verdict) {
    return crafting.counts().verdicts[static_cast<std::size_t>(verdict)];
}

/** Passes time on the clock of the host in steps of 1 s (a larger step ends the idle sessions). */
void pass(Refiner& shop, int seconds) {
    for (int second = 0; second < seconds; ++second) {
        shop.host.advance(std::chrono::milliseconds(1000));
        shop.host.steps(2, {&shop.clientA, &shop.clientB});
    }
}

/** CRAFTB1: the bonus items and decision D2. */
void bonus_items(const Options&) {
    Refiner shop(false);
    shop.enter(false);
    world::Crafting& crafting = shop.crafting();
    const gamedata::CraftingData& data = shop.host.crafting;
    const gamedata::Recipe& refine = *data.recipe_of("TestRefine");
    const gamedata::Recipe& cat = *data.recipe_of("TestRefineCat");
    const gamedata::Recipe& cooldown = *data.recipe_of("TestRefineCd");
    const auto rank = [&](std::uint32_t value) {
        check(shop.host.world.skills().set_rank(kCharacterA, "Smelting", gamedata::ProgressionKind::tradeSkill, value).ok(),
              "setup: Smelting " + std::to_string(value));
        return shop.entity(kCharacterA);
    };
    const auto near = [](double one, double two) { return std::fabs(one - two) < 1e-9; };
    world::Entity* a = rank(0);
    const world::CraftYield zero = crafting.yield_of(*a, refine, {"TestOre"});
    a = rank(1);
    const world::CraftYield one = crafting.yield_of(*a, refine, {"TestOre"});
    check(zero.chance == 0.0 && zero.extraUnits == 0.0 && zero.maxExtra == 0 && one.rankPoints == 510 && near(one.chance, 0.051)
              && near(one.extraUnits, 0.051) && one.maxExtra == 1,
          "the rank: Smelting 0 gives no bonus (live check L4: 0%); rank 1 has the RollBonus 510: the chance 0.051 (live: 5%), one extra unit at "
          "most");
    a = rank(200);
    const world::CraftYield rare = crafting.yield_of(*a, cooldown, {"TestOre", "TestRareOre"});
    const world::CraftYield primary = crafting.yield_of(*a, cooldown, {"TestRareOre", "TestOre"});
    check(rare.materialPoints == 5 && rare.rankPoints == 2500 && near(rare.materialChance, -0.25) && near(rare.chance, 0.0005)
              && primary.materialPoints == 10,
          "the materials: TestRareOre as the second entry gives IngredientBonusSecondary 5, as the first IngredientBonusPrimary 10; at Smelting 200 "
          "(2500) the recipe chance -0.25 gives 0.0005 (live check L6: Asmodeum 0% at 200 with no rare material)");
    a = rank(0);
    const world::CraftYield same = crafting.yield_of(*a, cat, {"TestOre", "TestOre"});
    const world::CraftYield mid = crafting.yield_of(*a, cat, {"TestOre", "TestOreMid"});
    const world::CraftYield low = crafting.yield_of(*a, cat, {"TestOre", "TestOreLow"});
    const world::CraftYield top = crafting.yield_of(*a, cat, {"TestOre", "TestOreTop"});
    check(near(same.materialChance, 0.0) && near(mid.materialChance, 0.5) && near(low.materialChance, -0.05) && near(low.chance, 0.0)
              && near(top.materialChance, 2.0) && near(top.extraUnits, 2.0) && top.maxExtra == 2,
          "the tier lists (base tier 1): a member of tier 1 0, tier 3 +0.5 (the second value of BonusItemChanceIncrease), tier 0 -0.05 (the "
          "chance is 0 at least), tier 5 +1.0 and its ExtraBonusItemChance 1: the chance 2.0, two extra units each job");
    const world::CraftYield triple = crafting.yield_of(*a, *data.recipe_of("TestRefineTriple"), {"TestOre"});
    a = rank(200);
    const world::CraftYield flat = crafting.yield_of(*a, *data.recipe_of("TestRefineFlat"), {"TestOre"});
    check(near(triple.extraUnits, 1.5) && triple.maxExtra == 2 && flat.rankPoints == 0 && flat.effectPoints == 0 && near(flat.chance, 0.3),
          "decision D2: OutputQty 3 with the chance 0.5 gives 1.5 extra units each job; DisallowBonusesToGS: no rank bonus at Smelting 200");
    // A status with ROLSmelting (the food SE_FoodLostBlackLicoriceT5 of the game data has 200).
    gamedata::AbilityTables tables;
    tables.cooldowns.push_back({"Test_Cooldown", 1.0F, 0.0F});
    gamedata::StatusRow food;
    food.id = "Test_SmeltingFood";
    food.cells.numbers = {{"ROLSmelting", 200.0F}};
    tables.statusEffects.push_back(food);
    check(shop.host.abilities.set(tables), "setup: the ability data with the status Test_SmeltingFood (" + shop.host.abilities.error() + ")");
    a = rank(0);
    world::ActiveStatus status;
    status.status = "Test_SmeltingFood";
    status.permanent = true;
    a->statuses.push_back(status);
    const world::CraftYield fed = crafting.yield_of(*a, refine, {"TestOre"});
    const world::CraftYield fedFlat = crafting.yield_of(*a, *data.recipe_of("TestRefineFlat"), {"TestOre"});
    a->statuses.clear();
    check(fed.effectPoints == 200 && near(fed.chance, 0.02) && fedFlat.effectPoints == 0,
          "a status with ROLSmelting 200: 0.02 (the ROL effects of the trade skill); none with DisallowBonusesToGS");

    // The draws of decision D2: floor(e), and one more unit when one draw is below the rest.
    int calls = 0;
    const auto low_draw = [&] { ++calls; return 0.1F; };
    const auto high_draw = [&] { ++calls; return 0.9F; };
    const std::uint32_t twoLow = world::Crafting::draw_extra(2.3, low_draw);
    const std::uint32_t twoHigh = world::Crafting::draw_extra(2.3, high_draw);
    const int callsBefore = calls;
    const std::uint32_t whole = world::Crafting::draw_extra(1.0, low_draw);
    const std::uint32_t none = world::Crafting::draw_extra(0.0, low_draw);
    check(twoLow == 3 && twoHigh == 2 && whole == 1 && none == 0 && calls == callsBefore,
          "e = 2.3: 3 with a draw below 0.3, else 2; e = 1.0 and e = 0 take no draw (no rest)");
    for (const double expected : {0.37, 1.6}) {
        std::uint64_t state = 77;
        std::uint64_t sum = 0;
        const int jobs = 200000;
        for (int job = 0; job < jobs; ++job) {
            sum += world::Crafting::draw_extra(expected, [&state] { return world::World::next_unit(state); });
        }
        const double mean = static_cast<double>(sum) / jobs;
        check(std::fabs(mean - expected) < 0.005, "the mean of 200,000 jobs with the generator of the world: " + std::to_string(mean)
                                                      + " for e = " + std::to_string(expected) + " (the mean that the client shows)");
    }

    // A seeded batch at the station: 30 jobs of TestRefineHigh (the chance 1.4: one or two extra units each job).
    shop.host.world.inventory().set_coin(kCharacterA, 100000);
    shop.give(kCharacterA, "TestOre", 30);
    shop.use_station(shop.clientA);
    const world::CraftPreview preview = crafting.preview(kCharacterA, "TestRefineHigh", 30, 4242);
    std::uint32_t bonus = 0;
    bool range = preview.draws.size() == 30;
    for (const std::uint32_t draw : preview.draws) {
        bonus += draw;
        range = range && (draw == 1 || draw == 2);
    }
    check(preview.verdict == world::CraftVerdict::accepted && range && near(preview.yield.extraUnits, 1.4),
          "the preview with the seed 4242: 30 draws of 1 or 2 extra units (e = 1.4), " + std::to_string(bonus) + " in all");
    shop.host.world.set_random_seed(4242);
    const std::size_t from = shop.clientA.got.size();
    shop.craft(shop.clientA, kCharacterA, "TestRefineHigh", 30, {entry("TestOre")});
    shop.finish(10);
    check(shop.count(kCharacterA, "TestRefined") == 30 + bonus && shop.count(kCharacterA, "TestOre") == 0
              && crafting.counts().bonusUnits == bonus && shop.entity(kCharacterA)->wallet.coin == 100000 - 30,
          "the batch with the same seed: " + std::to_string(30 + bonus) + " TestRefined (30 base and the " + std::to_string(bonus)
              + " of the preview), the 30 ores, the fee 30");
    const std::uint32_t refinedIndex = shop.host.catalog.find("TestRefined")->index;
    std::vector<std::uint8_t> actions;
    std::uint32_t lastQuantity = 0;
    for (const SeenItem& seen : bag_elements(shop.clientA, from)) {
        if (seen.item.index == refinedIndex) {
            actions.push_back(seen.item.action);
            lastQuantity = seen.item.quantity;
        }
    }
    bool quietFirst = actions.size() >= 2 && actions.back() == world::sync::kItemCreated;
    for (std::size_t index = 0; index + 1 < actions.size(); ++index) {
        quietFirst = quietFirst && actions[index] == world::sync::kItemAdded;
    }
    check(quietFirst && lastQuantity == 30 + bonus,
          "the craft-all result (live check L4): the updates of the jobs before the last job have the cause ItemAdded (" + std::to_string(actions.size())
              + " updates), the last one ItemCreated with the whole count");
}

/** CRAFTB2: the trade skill experience and the first craft. */
void experience(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const std::string path = (options.workDirectory / "crafting-b2.sqlite3").string();
    std::filesystem::remove(path);
    {
        Refiner shop(false, path);
        shop.enter(false);
        shop.host.world.inventory().set_coin(kCharacterA, 1000);
        shop.give(kCharacterA, "TestOre", 40);
        shop.use_station(shop.clientA);
        const world::CraftPreview first = shop.crafting().preview(kCharacterA, "TestRefine", 3);
        check(first.experience.progression == "Smelting" && first.experience.perJob == 44 && first.experience.firstCraft == "FC_TestRefine"
                  && first.experience.firstCraftExtra == 88,
              "the preview: 44 for each job (11 x 4 ore), the first craft 88 (live check L4: Base 132, First Craft 88 (x2), Total 220)");
        const std::size_t from = shop.clientA.got.size();
        shop.craft(shop.clientA, kCharacterA, "TestRefine", 3, {entry("TestOre")});
        shop.finish();
        const auto [rankAfter, pointsAfter] = shop.smelting(kCharacterA);
        world::Entity* a = shop.entity(kCharacterA);
        const std::vector<std::string> rows = shop.host.store.list_achievements(kCharacterA);
        check(rankAfter == 1 && pointsAfter == 112 && shop.crafting().counts().experience == 220 && shop.crafting().counts().firstCrafts == 1
                  && std::find(rows.begin(), rows.end(), "FC_TestRefine") != rows.end()
                  && std::binary_search(a->achievements.begin(), a->achievements.end(), kFirstCraftBit),
              "the first batch of 3: 44 x 3 + 88 = 220 (rank 0 needs 108: rank 1 with 112), the achievement row and its bit 5330");
        bool bit = false;
        for (const Fragment& fragment : slot_updates(shop.clientA, kAchievementSlot, from).fragments) {
            server::hub::Reader vector(field_of(fragment, 0, 0));
            if (vector.size() == 0 && vector.u8() == 1) {
                (void)vector.size64();
                const std::uint32_t bytes = vector.size();
                const auto bits = vector.raw(bytes);
                bit = bit || (bits.size() > kFirstCraftBit / 8 && (bits[kFirstCraftBit / 8] & (1U << (kFirstCraftBit % 8))) != 0);
            }
        }
        check(bit && !slot_updates(shop.clientA, kCategoricalSlot, from).fragments.empty(),
              "the client got the achievement state with the bit 5330 (the screen then shows no first-craft bonus) and the categorical state");
        const world::CraftPreview next = shop.crafting().preview(kCharacterA, "TestRefine", 2);
        shop.craft(shop.clientA, kCharacterA, "TestRefine", 2, {entry("TestOre")});
        shop.finish();
        const auto [rankNext, pointsNext] = shop.smelting(kCharacterA);
        check(next.experience.firstCraftExtra == 0 && next.experience.firstCraft.empty() && rankNext == 2 && pointsNext == 86
                  && shop.crafting().counts().experience == 308 && shop.crafting().counts().firstCrafts == 1,
              "the next batch of 2: 88 and no first-craft bonus (220 + 88 = 308: rank 2 with 86)");
    }
    {
        Refiner again(false, path, true);
        again.enter(false);
        world::Entity* a = again.entity(kCharacterA);
        const world::CraftPreview preview = again.crafting().preview(kCharacterA, "TestRefine", 1);
        check(std::binary_search(a->achievements.begin(), a->achievements.end(), kFirstCraftBit) && preview.experience.firstCraftExtra == 0
                  && again.smelting(kCharacterA).first == 2,
              "after a restart: the row gives the bit at the world entry, no first-craft bonus again, Smelting 2");
    }
    std::filesystem::remove(path);
}

/** CRAFTB3: the cooldown of a recipe. */
void cooldowns(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const std::string path = (options.workDirectory / "crafting-b3.sqlite3").string();
    std::filesystem::remove(path);
    const std::uint32_t crc = gamedata::item_id_crc("TestRefineCd");
    {
        Refiner shop(false, path);
        shop.enter(false);
        shop.host.world.inventory().set_coin(kCharacterA, 100000);
        shop.give(kCharacterA, "TestOre", 200);
        shop.give(kCharacterA, "TestRareOre", 80);
        shop.use_station(shop.clientA);
        std::size_t from = shop.clientA.got.size();
        check(slot_updates(shop.clientA, kCraftingSlot).fragments.empty(), "the first record of a character with no cooldown has no state 205");
        shop.craft(shop.clientA, kCharacterA, "TestRefineCd", 1, {entry("TestOre"), entry("TestRareOre")});
        shop.finish();
        world::Entity* a = shop.entity(kCharacterA);
        const auto entry205 = a->craftCooldowns.find(crc);
        const bool window = entry205 != a->craftCooldowns.end() && entry205->second.used == 1
                            && entry205->second.end - shop.host.clock.now() <= std::chrono::seconds(86400)
                            && entry205->second.end - shop.host.clock.now() > std::chrono::seconds(86398);
        const std::vector<Fragment> fragments = slot_updates(shop.clientA, kCraftingSlot, from).fragments;
        const WireCrafting state = fragments.empty() ? WireCrafting{} : read_crafting(fragments.back());
        check(window && state.ok && state.cooldowns.size() == 1 && state.cooldowns[0].recipe == crc && state.cooldowns[0].used == 1
                  && state.cooldowns[0].end == shop.host.clock.wall_time_ns(entry205->second.end) && state.bonuses == 0,
              "the first craft: a window of 86,400 s with 1 of 10 used; the state 205 has the CRC of the recipe, 1, and the end on the synced "
              "wall clock in nanoseconds (live check L6: 59m 57s per 9 items); the bonus map is empty");
        const std::vector<state::CraftCooldownRow> rows = shop.host.store.list_craft_cooldowns(kCharacterA);
        check(rows.size() == 1 && rows[0].recipe == "TestRefineCd" && rows[0].used == 1, "the row of the cooldown: TestRefineCd, 1");
        // A count above the crafts that are left: refused, no change.
        const std::uint32_t before = shop.count(kCharacterA, "TestRefinedC");
        shop.craft(shop.clientA, kCharacterA, "TestRefineCd", 10, {entry("TestOre"), entry("TestRareOre")});
        shop.finish();
        check(verdicts(shop.crafting(), world::CraftVerdict::cooldown) == 1 && shop.count(kCharacterA, "TestRefinedC") == before,
              "10 with 9 left: refused (cooldown), no change");
        shop.craft(shop.clientA, kCharacterA, "TestRefineCd", 9, {entry("TestOre"), entry("TestRareOre")});
        shop.finish();
        shop.craft(shop.clientA, kCharacterA, "TestRefineCd", 1, {entry("TestOre"), entry("TestRareOre")});
        shop.finish();
        a = shop.entity(kCharacterA);
        check(a->craftCooldowns[crc].used == 10 && verdicts(shop.crafting(), world::CraftVerdict::cooldown) == 2
                  && shop.crafting().preview(kCharacterA, "TestRefineCd", 1).cooldownLeft == 0U,
              "9 more: 10 of 10 used; one more is refused (live check L6: per 0 items, CRAFT off)");
        // The end of the window on a controlled clock: the command sets 3 s; after 4 s the next craft starts a new window.
        check(shop.crafting().set_cooldown(kCharacterA, "TestRefineCd", 10, 3.0).empty(), "the command: 10 of 10 used, 3 s to the end");
        pass(shop, 4);
        a = shop.entity(kCharacterA);
        check(shop.crafting().cooldown_left(*a, *shop.host.crafting.recipe_of("TestRefineCd")) == 10,
              "after the end of the window: 10 left (the client ends the window itself: live check L6)");
        from = shop.clientA.got.size();
        shop.craft(shop.clientA, kCharacterA, "TestRefineCd", 1, {entry("TestOre"), entry("TestRareOre")});
        shop.finish();
        a = shop.entity(kCharacterA);
        check(a->craftCooldowns[crc].used == 1 && a->craftCooldowns[crc].end > shop.host.clock.now() + std::chrono::seconds(86000),
              "the next craft: a new window, 1 of 10, 86,400 s");
        // A restart: the window keeps its time on the run clock of the world.
        check(shop.crafting().set_cooldown(kCharacterA, "TestRefineCd", 4, 100.0).empty(), "the command: 4 of 10 used, 100 s to the end");
        pass(shop, 30);
        check(shop.host.world.world_time().save("test"), "the world time and the run clock are stored");
    }
    {
        Refiner again(false, path, true);
        again.enter(false);
        world::Entity* a = again.entity(kCharacterA);
        const auto found = a->craftCooldowns.find(crc);
        const auto left = found != a->craftCooldowns.end() ? found->second.end - again.host.clock.now() : std::chrono::nanoseconds(0);
        const std::vector<Fragment> fragments = slot_updates(again.clientA, kCraftingSlot).fragments;
        const WireCrafting state = fragments.empty() ? WireCrafting{} : read_crafting(fragments.front());
        check(found != a->craftCooldowns.end() && found->second.used == 4 && left > std::chrono::seconds(60) && left < std::chrono::seconds(72)
                  && state.ok && state.cooldowns.size() == 1 && state.cooldowns[0].used == 4,
              "after a restart: 4 of 10 used, about 70 s left (the run clock: 100 s minus 30 s; the downtime does not count), and the first "
              "record has the state 205 (" + std::to_string(std::chrono::duration<double>(left).count()) + " s)");
    }
    std::filesystem::remove(path);
}

/** CRAFTB-DATA: the loader of format 2. */
void data_rules(const Options&) {
    using nlohmann::json;
    gamedata::CraftingData good;
    check(good.set(refine_document()) && good.format() == 2 && good.roll_bonus("smelting", 300) == 3000.0F
              && good.achievement_index("fc_testrefine") == kFirstCraftBit && good.material("TestRareOre").secondary == 5,
          "the file of format 2: the RollBonus (a rank above the table: the last row), the first-craft bit, the bonus values of an item");
    const auto refused = [](const std::function<void(json&)>& change) {
        json document = json::parse(refine_document());
        change(document);
        gamedata::CraftingData data;
        return !data.set(document.dump());
    };
    const auto recipe = [](json& document, const char* id) -> json& {
        for (json& entry : document["recipes"]) {
            if (entry["id"] == id) {
                return entry;
            }
        }
        throw std::runtime_error("no recipe");
    };
    check(refused([&](json& d) { recipe(d, "TestRefine")["event"] = "NoSuchEvent"; })
              && refused([&](json& d) { d["events"]["Craft_TestSmelting"]["xp"] = 50.0; })
              && refused([&](json& d) { d["events"]["Craft_TestSmelting"]["progression"] = "Musician"; })
              && refused([&](json& d) { recipe(d, "TestRefine")["firstCraftAchievement"] = "FC_Missing"; })
              && refused([&](json& d) { recipe(d, "TestRefineCd")["cooldownQuantity"] = 0; })
              && refused([&](json& d) { recipe(d, "TestRefine")["skill"] = "Weaving"; })
              && refused([&](json& d) { d.erase("ranks"); }) && refused([&](json& d) { recipe(d, "TestRefine").erase("bonusIncrease"); })
              && refused([&](json& d) { recipe(d, "TestRefine")["procedural"] = true; }),
          "a recipe with no cause and an event with no row, with character experience, or of a progression with no rank table, a first-craft "
          "achievement with no bit, a cooldown with no count, refining with no rank table, no ranks, no tier list, a procedural result: refused");
    json old = json::parse(refine_document());
    old["format"] = 1;
    gamedata::CraftingData first;
    check(!first.set(old.dump()), "a file of format 1 (stage 3.7a) with a refining recipe with no cause: refused (the rules of stage 3.7a)");
}

/** CRAFTB-MP: two sessions. */
void two_sessions(const Options&) {
    Refiner shop(true);
    shop.enter(true);
    for (const char* character : {kCharacterA, kCharacterB}) {
        shop.host.world.inventory().set_coin(character, 1000);
        shop.give(character, "TestOre", 40);
        shop.give(character, "TestRareOre", 10);
    }
    shop.use_station(shop.clientA);
    shop.use_station(shop.clientB);
    const std::size_t fromB = shop.clientB.got.size();
    shop.craft(shop.clientA, kCharacterA, "TestRefineCd", 2, {entry("TestOre"), entry("TestRareOre")});
    shop.finish();
    shop.craft(shop.clientA, kCharacterA, "TestRefine", 1, {entry("TestOre")});
    shop.finish();
    world::Entity* b = shop.entity(kCharacterB);
    const world::CraftPreview preview = shop.crafting().preview(kCharacterB, "TestRefine", 1);
    const world::CraftPreview cooldown = shop.crafting().preview(kCharacterB, "TestRefineCd", 1);
    std::size_t aCraftingFragments = 0;
    for (std::uint32_t interest = 1; interest < 8; ++interest) {
        aCraftingFragments += slot_updates(shop.clientB, kCraftingSlot, fromB, interest).fragments.size();
    }
    check(shop.entity(kCharacterA)->craftCooldowns.size() == 1 && b->craftCooldowns.empty() && cooldown.cooldownLeft == 10U
              && preview.experience.firstCraftExtra == 88 && shop.smelting(kCharacterB).first == 0 && shop.smelting(kCharacterA).first > 0
              && aCraftingFragments == 0 && !std::binary_search(b->achievements.begin(), b->achievements.end(), kFirstCraftBit),
          "the cooldown, the experience, and the first craft of A do not change B (10 left, its first-craft bonus, Smelting 0); B gets no "
          "state 205");
    shop.craft(shop.clientB, kCharacterB, "TestRefine", 1, {entry("TestOre")});
    shop.finish();
    check(shop.smelting(kCharacterB).first == 1 && shop.smelting(kCharacterB).second == 132 - 108,
          "B gets its own first craft: 44 + 88 = 132 (rank 1 with 24)");
}

/** CRAFTB-LOAD: the cost of a batch does not depend on the other characters, their cooldowns, or the recipes of the data. */
void load(const Options&) {
    const auto measure = [](std::size_t dormantRecipes, std::size_t extraCharacters) {
        Refiner shop(true);
        if (dormantRecipes > 0) {
            nlohmann::json document = nlohmann::json::parse(refine_document());
            for (std::size_t index = 0; index < dormantRecipes; ++index) {
                nlohmann::json recipe = document["recipes"][0];
                recipe["id"] = "TestDormant" + std::to_string(index);
                recipe["crc"] = gamedata::item_id_crc(recipe["id"].get<std::string>());
                document["recipes"].push_back(recipe);
            }
            if (!shop.host.crafting.set(document.dump())) {
                throw std::runtime_error("load: " + shop.host.crafting.error());
            }
        }
        shop.enter(true);
        for (std::size_t index = 0; index < extraCharacters; ++index) {
            const std::string id = "1a7e0000-0000-4000-8000-2" + std::to_string(100000000000ULL + index);
            add_player(shop.host.store, std::to_string(76561202210000000ULL + index), "Load", id.c_str(), ("Smelter" + std::to_string(index)).c_str(),
                       state::WorldPosition{5000.0F + static_cast<float>(index), 5000.0F, 30.0F});
            (void)shop.host.world.spawn_character(id, state::WorldPosition{5000.0F + static_cast<float>(index), 5000.0F, 30.0F});
            if (world::Entity* entity = shop.host.world.find_character(id)) {
                for (std::uint32_t recipe = 0; recipe < 1000; ++recipe) {
                    entity->craftCooldowns[recipe] = world::CraftCooldown{"TestRefineCd", 1, 0, shop.host.clock.now() + std::chrono::hours(20)};
                }
            }
        }
        for (const char* character : {kCharacterA, kCharacterB}) {
            shop.host.world.inventory().set_coin(character, 1000000);
            shop.give(character, "TestOre", 4000);
        }
        shop.use_station(shop.clientA);
        shop.use_station(shop.clientB);
        const std::uint64_t jobVisits = shop.crafting().counts().jobVisits;
        const std::uint64_t slotVisits = shop.crafting().counts().slotVisits;
        const auto start = std::chrono::steady_clock::now();
        shop.craft(shop.clientA, kCharacterA, "TestRefine", 1000, {entry("TestOre")});
        shop.craft(shop.clientB, kCharacterB, "TestRefine", 1000, {entry("TestOre")});
        shop.host.advance(std::chrono::milliseconds(1000));
        for (int tick = 0; tick < 60 && shop.crafting().batch_count() > 0; ++tick) {
            shop.host.steps(1, {&shop.clientA, &shop.clientB});
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        return std::make_tuple(shop.crafting().counts().jobs, shop.crafting().counts().jobVisits - jobVisits,
                               shop.crafting().counts().slotVisits - slotVisits, elapsed, shop.count(kCharacterA, "TestRefined"));
    };
    const auto [jobsSmall, jobVisitsSmall, slotVisitsSmall, timeSmall, refinedSmall] = measure(0, 0);
    const auto [jobsLarge, jobVisitsLarge, slotVisitsLarge, timeLarge, refinedLarge] = measure(20000, 100);
    note("CRAFTB-LOAD: 2,000 jobs of two batches in " + std::to_string(timeSmall.count()) + " ms and " + std::to_string(timeLarge.count())
         + " ms (20,000 more recipes, 100 more characters with 1,000 cooldowns each); job visits " + std::to_string(jobVisitsSmall) + " and "
         + std::to_string(jobVisitsLarge) + "; slot visits " + std::to_string(slotVisitsSmall) + " and " + std::to_string(slotVisitsLarge));
    check(jobsSmall == 2000 && jobsLarge == 2000 && jobVisitsSmall == 2000 && jobVisitsLarge == 2000 && slotVisitsSmall == slotVisitsLarge
              && refinedSmall == 1000 && refinedLarge == 1000,
          "two batches of 1,000 jobs: one job visit for each job and the same bag slots with 20,000 more recipes and 100 more characters with "
          "100,000 cooldowns (no timer of a cooldown)");
    check(timeLarge.count() < 2000, "2,000 jobs with the large world in less than 2 s (" + std::to_string(timeLarge.count()) + " ms)");
}

/** CRAFTB-CMD: the commands. */
void commands(const Options&) {
    using nlohmann::json;
    Refiner shop(false);
    shop.enter(false);
    server::http::Router router;
    const server::http::Services services{shop.host.store, "127.0.0.1:32210", "test-version", "", shop.host.tables, shop.host.world};
    server::http::register_dev_routes(router, services);
    shop.host.tick();
    const auto send = [&](const char* method, const std::string& path, const std::string& query) {
        service::HttpRequest request;
        request.method = method;
        request.path = path;
        request.query = query;
        request.target = path + "?" + query;
        return server::http::handle_request(router, request);
    };
    shop.host.world.inventory().set_coin(kCharacterA, 1000);
    shop.give(kCharacterA, "TestOre", 20);
    shop.use_station(shop.clientA);
    const std::string character = std::string("character=") + kCharacterA;
    const json plan = json::parse(send("POST", "/v1/dev/crafting/preview", character + "&recipe=TestRefineHigh&quantity=4&seed=9").body, nullptr, false);
    const service::HttpResponse set = send("POST", "/v1/dev/crafting/cooldown", character + "&recipe=TestRefineCd&used=3&seconds=600");
    const service::HttpResponse wrong = send("POST", "/v1/dev/crafting/cooldown", character + "&recipe=TestRefine&used=3&seconds=600");
    const json view = json::parse(send("GET", "/v1/dev/crafting", character).body, nullptr, false);
    const service::HttpResponse seed = send("POST", "/v1/dev/crafting/seed", character + "&seed=9");
    check(plan.is_object() && plan["accepted"] == true && std::fabs(plan["bonus"]["chance"].get<double>() - 1.4) < 1e-9
              && plan["experience"]["perJob"] == 0 && plan["seeded"]["draws"].size() == 4 && set.status == 200 && wrong.status == 400
              && view.is_object() && view["cooldowns"].size() == 1 && view["cooldowns"][0]["used"] == 3 && view["cooldowns"][0]["quantity"] == 10
              && seed.status == 200,
          "the preview with the bonus (1.4) and 4 seeded draws, the cooldown command (a recipe with no cooldown: 400), the cooldowns in the view, "
          "the seed command");
}

} // namespace

std::vector<Case> refining_cases() {
    return {{"CRAFTB1", "refining (3.7b, I1, I2, D2): the bonus chance, the tier lists, the roll bonus, D2 draws, a seeded batch, the craft-all causes",
             bonus_items},
            {"CRAFTB2", "refining (3.7b, I3): the trade skill experience, the first craft one time with its achievement, a restart", experience},
            {"CRAFTB3", "refining (3.7b, I4): the cooldown window, state 205 (wall ns), the count left, the end, a restart on the run clock", cooldowns},
            {"CRAFTB-DATA", "refining (3.7b, I6): the loader of format 2 and its rules for a recipe with no cause", data_rules},
            {"CRAFTB-MP", "refining (3.7b, I5, MP-10): the cooldown, the experience, the first craft of A do not change B", two_sessions},
            {"CRAFTB-LOAD", "refining (3.7b, I5): 2,000 jobs with 20,000 recipes and 100,000 cooldowns of 100 characters: the same visits, 2 s",
             load},
            {"CRAFTB-CMD", "refining (3.7b): the preview with the bonus and a seed, the cooldown and seed commands", commands}};
}

} // namespace soulwarden::backend::tests

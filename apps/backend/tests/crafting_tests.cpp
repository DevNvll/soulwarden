// The cases of stage 3.7a (docs/systems/crafting.md, protocol notes 4.50): the fixed crafts at a station and at a camp.
//
// The ids CRA1 and CRA2 of the research (crafting-stage.md section 10) are the ids of two cases of stage 3.4f-C (the areas): the cases
// of this stage are CRAFT1, CRAFT2, CRAFT-MP, CRAFT-LOAD, CRAFT-D1, and CRAFT-TOOL.
//
//   CRAFT1     the request 2997 of live check L3 (its fields and the limits of the reader), the craft of one knife at the station: the costs of
//            the recipe, the knife with the cause ItemCreated before the progress 2993 (its exact bytes), the fee, the rows after a restart;
//            the empty storage search 7046 for 7044 and 7039
//   CRAFT2     the material plan and the refusals: the quantities of the recipe (not the entries), two ingredients of one item, a locked
//            stack, a quick slot, a category member, wrong entries, a full bag, a late change of the bag in a batch (D5), a repeated
//            request, the stop of the client, the end of the use, the stop form of a refusal, no data, a key with no row, the camp
//   CRAFT-MP   two sessions: two batches at one station, the owner views (no item and no progress of A for B), a request that names the
//            entity or the facet of a different character, the stop of B does not stop A, a request with no station use
//   CRAFT-LOAD 20,000 far station placements and 100 more characters: the same visits for each validation, 10,000 validations and 200
//            crafts in their time limit
//   CRAFT-D1   decision D1: the loader of the interaction data keeps the override of a station, and its log text
//   CRAFT-CMD  the commands of the development routes (the view, the preview, the stop)
//   CRAFT-TOOL the check of the data tool (tools/gamedata/check_crafting_tool.py)

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

/** The fields of the live 2997 of the knife (live check L3) with a made-up target and G. */
Bytes live_request(std::uint32_t quantity = 1) {
    Bytes body;
    const Bytes prefix{0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x4C, 0x55, 0x4F, 0x53, 0x00, 0x00, 0x00, 0x02};
    body.insert(body.end(), prefix.begin(), prefix.end());
    const Bytes fields{0xA5, 0x46, 0xF7, 0xC5, 0x00, 0x00, 0x00, static_cast<std::uint8_t>(quantity), 0x00, 0x00, 0x00, 0x01, 0x00, 0x64, 0x00,
                       0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x01, 0x0C, 0x92, 0x80, 0xC8, 0x01, 0x00, 0x00, 0x00, 0x01, 0x8A, 0x91, 0x44, 0x27};
    body.insert(body.end(), fields.begin(), fields.end());
    return body;
}

void request_and_result(const Options& options) {
    // The reader: the live body of the knife (51 bytes), and the limits of the reader of the client.
    std::uint64_t entity = 0;
    const std::optional<world::CraftRequest> live = server::rep::read_craft_request(live_request(), entity);
    check(live && live->target == 0x1122334455667788ULL && entity == 0x4C554F5300000002ULL && live->recipe == 0xA546F7C5U && live->quantity == 1
              && live->coinFee == 1 && live->gearScore == 100 && live->perkItems.empty() && live->upgrade == 0 && live->ingredients.size() == 2
              && live->ingredients[0].type == 1 && live->ingredients[0].quantity == 1 && live->ingredients[0].item == 0x0C9280C8U
              && live->ingredients[1].item == 0x8A914427U && live_request().size() == 51,
          "the live 2997 of the knife: recipe A546F7C5, count 1, fee 1, gear score 100, no perk, two Item entries (Flint, Wood), 51 bytes");
    Bytes over = live_request();
    over[22] = 0x27; // the count 0x2711: above 10,000
    over[23] = 0x11;
    Bytes trailing = live_request();
    trailing.push_back(0);
    Bytes badType = live_request();
    badType[33] = 5;
    check(!server::rep::read_craft_request(over, entity) && !server::rep::read_craft_request(trailing, entity)
              && !server::rep::read_craft_request(badType, entity) && gamedata::item_id_crc("1hSkinningKnifeT1") == 0xA546F7C5U
              && gamedata::item_id_crc("FlintT1") == 0x0C9280C8U,
          "the reader refuses a count above 10,000, a byte after the last entry, and an entry type 5; the CRC-32 rule of the ids");

    // One knife at the station: the costs of the recipe, the item with the cause ItemCreated, then the progress.
    std::filesystem::create_directories(options.workDirectory);
    const std::string path = (options.workDirectory / "crafting-cra1.sqlite3").string();
    std::filesystem::remove(path);
    std::uint32_t knifeIndex = 0;
    {
        Crafter shop(false, true, path);
        shop.enter(false);
        knifeIndex = shop.host.catalog.find("TestKnifeT1")->index;
        shop.host.world.inventory().set_coin(kCharacterA, 1000);
        shop.give(kCharacterA, "TestFlintT1", 1);
        shop.give(kCharacterA, "TestWoodT1", 1);
        shop.use_station(shop.clientA);
        const std::size_t from = shop.clientA.got.size();
        shop.craft(shop.clientA, kCharacterA, "TestKnifeT1", 1, {entry("TestFlintT1"), entry("TestWoodT1")});
        shop.host.advance(std::chrono::milliseconds(500));
        shop.host.steps(2, {&shop.clientA});
        check(shop.count(kCharacterA, "TestKnifeT1") == 0 && shop.count(kCharacterA, "TestFlintT1") == 1 && shop.crafting().batch_count() == 1,
              "0.5 s after the request: no item yet, the materials are there, one batch runs (the craft time 1.0 s)");
        shop.finish();
        world::Entity* a = shop.entity(kCharacterA);
        check(shop.count(kCharacterA, "TestKnifeT1") == 1 && shop.count(kCharacterA, "TestFlintT1") == 0
                  && shop.count(kCharacterA, "TestWoodT1") == 0 && a->wallet.coin == 999 && shop.crafting().batch_count() == 0
                  && shop.crafting().counts().jobs == 1,
              "after 1 s: one knife, no Flint, no Wood, the fee 1 (0.01 coin: the minimum of the tax path), the batch ended");
        const std::vector<SeenItem> elements = bag_elements(shop.clientA, from);
        std::size_t knifeAt = 0;
        bool knife = false;
        for (const SeenItem& seen : elements) {
            if (seen.item.index == knifeIndex && seen.item.quantity == 1) {
                knife = seen.item.action == world::sync::kItemCreated && seen.item.gearScore == 100 && seen.item.durability == 1000
                        && seen.item.maxDurability == 1000 && (seen.item.flags & world::kItemBound) != 0;
                knifeAt = seen.index;
            }
        }
        check(knife, "the client got the knife with the cause ItemCreated (3), gear score 100, durability 1000 of 1000, bound");
        const std::vector<Progress> sent = progresses(shop.clientA, from);
        Bytes expected;
        const std::uint64_t target = crafting_target(a->id);
        for (int shift = 56; shift >= 0; shift -= 8) {
            expected.push_back(static_cast<std::uint8_t>(target >> shift));
        }
        for (int shift = 56; shift >= 0; shift -= 8) {
            expected.push_back(static_cast<std::uint8_t>(a->id >> shift));
        }
        const std::string recipe = "TestKnifeT1";
        expected.push_back(static_cast<std::uint8_t>(recipe.size()));
        expected.insert(expected.end(), recipe.begin(), recipe.end());
        const Bytes counts{0, 0, 0, 1, 0, 0, 0, 1};
        expected.insert(expected.end(), counts.begin(), counts.end());
        Bytes got;
        for (std::size_t index = from; index < shop.clientA.got.size(); ++index) {
            if (shop.clientA.got[index].type == server::rep::kTypeCraftProgress) {
                got = shop.clientA.got[index].body;
            }
        }
        check(sent.size() == 1 && got == expected && sent.front().index > knifeAt,
              "one progress 2993: the exact bytes (the Crafting facet of A, G, \"TestKnifeT1\", 1, 1), after the bundle of the knife");
    }
    // A restart: the rows of the store have the knife.
    {
        Crafter again(false, true, path, true);
        check(again.count(kCharacterA, "TestKnifeT1") == 1 && again.count(kCharacterA, "TestFlintT1") == 0,
              "after a restart of the backend: the knife is in the rows, the materials are not");
    }
    std::filesystem::remove(path);

    // The storage search of the crafting screen: the empty result with the UUID of the request.
    Crafter shop(false);
    shop.enter(false);
    world::Entity* a = shop.entity(kCharacterA);
    Bytes search;
    const std::uint64_t target = storage_target(a->id);
    for (int shift = 56; shift >= 0; shift -= 8) {
        search.push_back(static_cast<std::uint8_t>(target >> shift));
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        search.push_back(static_cast<std::uint8_t>(a->id >> shift));
    }
    const Bytes uuid{0x43, 0xF0, 0xC0, 0x43, 0xBB, 0xED, 0x4C, 0x0C, 0xA4, 0x81, 0x1F, 0x09, 0x51, 0x7F, 0xC4, 0x09};
    search.insert(search.end(), uuid.begin(), uuid.end());
    const Bytes parameters{0, 0, 0, 0, 0, 0, 0, 0, 0, 0x0E, 0x76, 0x93, 0xAE, 0, 0};
    search.insert(search.end(), parameters.begin(), parameters.end());
    std::size_t from = shop.clientA.got.size();
    shop.host.datagram(shop.clientA.peer, shop.clientA.component_message(server::rep::kTypeStorageSearch, a->actor.bytes, search));
    shop.host.steps(2, {&shop.clientA});
    shop.host.datagram(shop.clientA.peer, shop.clientA.component_call(server::rep::kTypeStorageSearchReplicate, a->actor.bytes, target, a->id));
    shop.host.steps(2, {&shop.clientA});
    Bytes expected(search.begin(), search.begin() + 32);
    expected.push_back(0);
    std::vector<Bytes> results;
    for (std::size_t index = from; index < shop.clientA.got.size(); ++index) {
        if (shop.clientA.got[index].type == server::rep::kTypeStorageSearchResults) {
            results.push_back(shop.clientA.got[index].body);
        }
    }
    check(results.size() == 2 && results[0] == expected && results[1] == expected && expected.size() == 33,
          "7044 and then 7039: two empty results 7046 (33 bytes: the facet, G, the UUID of the search, count 0; live check L2)");
    from = shop.clientA.got.size();
    search[7] ^= 0x01; // a target that is not the GlobalStorage facet of A
    shop.host.datagram(shop.clientA.peer, shop.clientA.component_message(server::rep::kTypeStorageSearch, a->actor.bytes, search));
    shop.host.steps(2, {&shop.clientA});
    bool none = true;
    for (std::size_t index = from; index < shop.clientA.got.size(); ++index) {
        none = none && shop.clientA.got[index].type != server::rep::kTypeStorageSearchResults;
    }
    check(none, "a 7044 with a target that is not the GlobalStorage facet of the character: no reply");
}

/** @return True when the last progress that a client got from an index on is this one. */
bool last_progress(const FakeClient& client, std::size_t from, const std::string& recipe, std::uint32_t completed, std::uint32_t total) {
    const std::vector<Progress> sent = progresses(client, from);
    return !sent.empty() && sent.back().recipe == recipe && sent.back().completed == completed && sent.back().total == total;
}

void plan_and_refusals(const Options&) {
    {
        // The quantities of the recipe, not the entries: the bar costs 2 TestIngotB (a member of TestMetal) and 1 Wood; its entry says 1.
        Crafter shop(false);
        shop.enter(false);
        shop.host.world.inventory().set_coin(kCharacterA, 1000);
        shop.give(kCharacterA, "TestIngotB", 3);
        shop.give(kCharacterA, "TestWoodT1", 1);
        shop.give(kCharacterA, "TestFlintT1", 1);
        shop.use_station(shop.clientA);
        shop.craft(shop.clientA, kCharacterA, "TestBarRecipe", 1, {entry("TestFlintT1"), entry("TestWoodT1")});
        check(shop.crafting().batch_count() == 0 && shop.count(kCharacterA, "TestFlintT1") == 1,
              "a category entry with an item that is not a member of the category: refused, no change");
        std::size_t from = shop.clientA.got.size();
        shop.craft(shop.clientA, kCharacterA, "TestBarRecipe", 1, {entry("TestIngotB", 1), entry("TestWoodT1", 1)});
        shop.finish();
        check(shop.count(kCharacterA, "TestIngotB") == 1 && shop.count(kCharacterA, "TestWoodT1") == 0 && shop.count(kCharacterA, "TestBar") == 2
                  && shop.entity(kCharacterA)->wallet.coin == 995 && last_progress(shop.clientA, from, "TestBarRecipe", 1, 1),
              "the bar: 2 TestIngotB (the quantity of the recipe, the entry said 1) and 1 Wood; 2 bars (OutputQty); the fee 5 of the recipe");
        // Two ingredients of one item are one cost of 3: two Flint are not enough.
        shop.give(kCharacterA, "TestFlintT1", 1);
        from = shop.clientA.got.size();
        shop.craft(shop.clientA, kCharacterA, "TestDouble", 1, {entry("TestFlintT1"), entry("TestFlintT1")});
        check(shop.crafting().batch_count() == 0 && shop.count(kCharacterA, "TestFlintT1") == 2 && last_progress(shop.clientA, from, "", 0, 0),
              "two ingredients of one item (1 + 2 Flint) with 2 Flint: refused (one stack pays a unit once), no change, the stop form 2993");
        // A locked stack does not pay; a quick slot does.
        shop.give(kCharacterA, "TestFlintT1", 1);
        world::Entity* a = shop.entity(kCharacterA);
        std::uint32_t flintSlot = 0;
        for (std::uint32_t slot = 0; slot < a->bag.slots.size(); ++slot) {
            if (a->bag.slots[slot].item && a->bag.slots[slot].item->itemId == "TestFlintT1") {
                flintSlot = slot;
            }
        }
        shop.host.world.inventory().set_locked(kCharacterA, flintSlot, nullptr, true);
        shop.craft(shop.clientA, kCharacterA, "TestDouble", 1, {entry("TestFlintT1"), entry("TestFlintT1")});
        check(shop.crafting().batch_count() == 0 && shop.count(kCharacterA, "TestFlintT1") == 3,
              "3 Flint in a locked stack: refused (the cache of the client counts unlocked stacks only), no change");
        shop.host.world.inventory().set_locked(kCharacterA, flintSlot, nullptr, false);
        shop.give(kCharacterA, "TestPotionT1", 1);
        a = shop.entity(kCharacterA);
        for (std::uint32_t slot = 0; slot < a->bag.slots.size(); ++slot) {
            if (a->bag.slots[slot].item && a->bag.slots[slot].item->itemId == "TestPotionT1") {
                shop.host.world.inventory().equip(kCharacterA, slot, world::paperdoll::kQuickFirst);
                break;
            }
        }
        shop.give(kCharacterA, "TestPotionT1", 1);
        shop.craft(shop.clientA, kCharacterA, "TestTonic", 1, {entry("TestPotionT1")});
        shop.finish();
        a = shop.entity(kCharacterA);
        check(shop.count(kCharacterA, "TestPotionT1") == 0 && a->equipment.item(world::paperdoll::kQuickFirst) == nullptr
                  && shop.count(kCharacterA, "TestBar") == 3,
              "2 potions: one from the bag and one from quick slot 1 (the cache of the client counts the quick slots 1 to 4)");
        // Wrong entries: a count that is not the count of the ingredients, a wrong item, a type that is not Item.
        from = shop.clientA.got.size();
        shop.craft(shop.clientA, kCharacterA, "TestKnifeT1", 1, {entry("TestFlintT1")});
        shop.craft(shop.clientA, kCharacterA, "TestKnifeT1", 1, {entry("TestFlintT1"), entry("TestFlintT1")});
        shop.craft(shop.clientA, kCharacterA, "TestKnifeT1", 1, {Entry{3, 1, gamedata::item_id_crc("TestFlintT1")}, entry("TestWoodT1")});
        check(shop.crafting().counts().verdicts[static_cast<std::size_t>(world::CraftVerdict::badIngredients)] == 4
                  && progresses(shop.clientA, from).size() == 3,
              "one entry for two ingredients, a wrong item, and a type 3 entry: refused (badIngredients), each with the stop form");
    }
    {
        // The rules of the recipe and of the request.
        Crafter shop(false);
        shop.enter(false);
        shop.host.world.inventory().set_coin(kCharacterA, 1000);
        shop.give(kCharacterA, "TestFlintT1", 5);
        shop.give(kCharacterA, "TestWoodT1", 5);
        const std::vector<Entry> knife{entry("TestFlintT1"), entry("TestWoodT1")};
        shop.craft(shop.clientA, kCharacterA, "TestKnifeT1", 1, knife);
        shop.use_station(shop.clientA);
        shop.craft(shop.clientA, kCharacterA, "TestEvent", 1, knife);
        shop.craft(shop.clientA, kCharacterA, "TestEngineering", 1, knife);
        shop.craft(shop.clientA, kCharacterA, "TestRank", 1, knife);
        shop.craft(shop.clientA, kCharacterA, "TestKnifeT1", 0, knife);
        shop.craft(shop.clientA, kCharacterA, "TestKnifeT1", 1, knife, 1, 100, {0x1234U});
        shop.craft(shop.clientA, kCharacterA, "TestKnifeT1", 1, knife, 1, 100, {}, 1);
        shop.craft(shop.clientA, kCharacterA, "NoSuchRecipe", 1, knife);
        shop.host.world.inventory().set_coin(kCharacterA, 0);
        shop.craft(shop.clientA, kCharacterA, "TestKnifeT1", 1, knife);
        const auto& verdicts = shop.crafting().counts().verdicts;
        const auto of = [&](world::CraftVerdict verdict) { return verdicts[static_cast<std::size_t>(verdict)]; };
        check(of(world::CraftVerdict::noStation) == 1 && of(world::CraftVerdict::unsupported) == 1 && of(world::CraftVerdict::wrongStation) == 1
                  && of(world::CraftVerdict::rank) == 1 && of(world::CraftVerdict::badQuantity) == 1 && of(world::CraftVerdict::perkItems) == 1
                  && of(world::CraftVerdict::upgrade) == 1
                  && of(world::CraftVerdict::unknownRecipe) == 1 && of(world::CraftVerdict::notEnoughCoin) == 1
                  && shop.count(kCharacterA, "TestFlintT1") == 5 && shop.count(kCharacterA, "TestBar") == 0 && shop.crafting().counts().jobs == 0,
              "no station use, a cause of the data, a station key that the row does not permit, the rank, the count 0, a perk item, an "
              "upgrade (stage 3.7c: its own verdict), an unknown recipe, no coin for the fee: each refused, no change");
        // A full bag: the knife needs a free slot (the bag has 500 slots at most).
        shop.host.world.inventory().set_coin(kCharacterA, 1000);
        world::Entity* a = shop.entity(kCharacterA);
        const std::uint32_t used = static_cast<std::uint32_t>(std::count_if(a->bag.slots.begin(), a->bag.slots.end(),
                                                                             [](const world::BagSlot& slot) { return slot.item.has_value(); }));
        shop.give(kCharacterA, "TestFiller", world::kMaxContainerSlots - used);
        shop.craft(shop.clientA, kCharacterA, "TestKnifeT1", 1, knife);
        check(of(world::CraftVerdict::bagFull) == 1 && shop.count(kCharacterA, "TestFlintT1") == 5,
              "a full bag (500 slots, the Flint and Wood stacks stay): refused before any cost (no loot overflow for a paid craft)");
    }
    {
        // Decision D5 (option A): a late change of the bag. A batch of 3 knives is accepted with 3 free slots; then the bag gets fillers up
        // to 1 free slot: job 1 completes, job 2 stops before its cost and output.
        Crafter shop(false);
        shop.enter(false);
        shop.host.world.inventory().set_coin(kCharacterA, 1000);
        shop.give(kCharacterA, "TestFlintT1", 3);
        shop.give(kCharacterA, "TestWoodT1", 3);
        world::Entity* a = shop.entity(kCharacterA);
        const std::uint32_t used = static_cast<std::uint32_t>(std::count_if(a->bag.slots.begin(), a->bag.slots.end(),
                                                                             [](const world::BagSlot& slot) { return slot.item.has_value(); }));
        shop.give(kCharacterA, "TestFiller", world::kMaxContainerSlots - used - 3);
        shop.use_station(shop.clientA);
        const std::size_t from = shop.clientA.got.size();
        shop.craft(shop.clientA, kCharacterA, "TestKnifeT1", 3, {entry("TestFlintT1"), entry("TestWoodT1")});
        check(shop.crafting().batch_count() == 1, "a batch of 3 with 3 free slots: accepted");
        shop.give(kCharacterA, "TestFiller", 2);
        shop.finish();
        check(shop.count(kCharacterA, "TestKnifeT1") == 1 && shop.count(kCharacterA, "TestFlintT1") == 2 && shop.count(kCharacterA, "TestWoodT1") == 2
                  && shop.entity(kCharacterA)->wallet.coin == 999 && shop.crafting().batch_count() == 0
                  && shop.crafting().counts().ends[static_cast<std::size_t>(world::CraftEnd::jobRefused)] == 1
                  && last_progress(shop.clientA, from, "TestKnifeT1", 1, 1),
              "the late change: job 1 completes (a knife, 1 Flint, 1 Wood, the fee 1), job 2 stops before its cost and its output; the "
              "progress 1 of 1 ends the crafting of the client");
    }
    {
        // The repeated request, the stop of the client, the end of the use.
        Crafter shop(false);
        shop.enter(false);
        shop.host.world.inventory().set_coin(kCharacterA, 100000);
        shop.give(kCharacterA, "TestFlintT1", 40);
        shop.give(kCharacterA, "TestWoodT1", 40);
        shop.use_station(shop.clientA);
        const std::vector<Entry> knife{entry("TestFlintT1"), entry("TestWoodT1")};
        std::size_t from = shop.clientA.got.size();
        shop.craft(shop.clientA, kCharacterA, "TestKnifeT1", 1, knife);
        shop.craft(shop.clientA, kCharacterA, "TestKnifeT1", 1, knife);
        check(shop.crafting().counts().verdicts[static_cast<std::size_t>(world::CraftVerdict::busy)] == 1 && progresses(shop.clientA, from).empty(),
              "the same request again while the batch runs: refused (busy) with no message (a stop form would end the crafting of the client)");
        shop.finish();
        check(shop.count(kCharacterA, "TestKnifeT1") == 1 && shop.count(kCharacterA, "TestFlintT1") == 39,
              "the repeated request gives no second item: one knife, one Flint");
        shop.stop(shop.clientA, kCharacterA);
        check(shop.crafting().counts().idleStops == 1, "the 2999 of the client after the final progress: no batch, no change");
        // A batch of 25: 20 jobs in the first tick of the batch (kJobsPerTick), then the stop of the client.
        shop.craft(shop.clientA, kCharacterA, "TestKnifeT1", 25, knife);
        shop.host.advance(std::chrono::milliseconds(1000));
        shop.host.steps(1, {&shop.clientA});
        const std::uint32_t partial = shop.count(kCharacterA, "TestKnifeT1");
        shop.stop(shop.clientA, kCharacterA);
        shop.host.steps(10, {&shop.clientA});
        check(partial == 21 && shop.count(kCharacterA, "TestKnifeT1") == 21 && shop.count(kCharacterA, "TestFlintT1") == 19
                  && shop.crafting().counts().ends[static_cast<std::size_t>(world::CraftEnd::clientStop)] == 1,
              "a batch of 25: 20 jobs in the first tick, then the stop of the client (2999): the 20 jobs stay, no more job");
        // The end of the use (the client closes the station: no block) stops a batch; the client gets the final progress.
        shop.craft(shop.clientA, kCharacterA, "TestKnifeT1", 10, knife);
        from = shop.clientA.got.size();
        shop.end_block(shop.clientA);
        shop.finish();
        check(shop.count(kCharacterA, "TestKnifeT1") == 21 && shop.count(kCharacterA, "TestFlintT1") == 19
                  && shop.crafting().counts().ends[static_cast<std::size_t>(world::CraftEnd::useEnded)] == 1 && last_progress(shop.clientA, from, "", 0, 0),
              "the end of the station use before the first job: no job, the stop form");
    }
    {
        // No crafting data: refused with no change. A station whose key has no row (Carpentry2 with no D1 override): refused.
        Crafter empty(false, false);
        empty.enter(false);
        empty.give(kCharacterA, "TestFlintT1", 1);
        empty.give(kCharacterA, "TestWoodT1", 1);
        empty.use_station(empty.clientA);
        empty.craft(empty.clientA, kCharacterA, "TestKnifeT1", 1, {entry("TestFlintT1"), entry("TestWoodT1")});
        Crafter shop(false);
        shop.enter(false);
        shop.give(kCharacterA, "TestFlintT1", 1);
        shop.give(kCharacterA, "TestWoodT1", 1);
        const world::FixtureResult noRow = shop.host.world.interactions().make_fixture(kCharacterA, "noRow", {0.0F, 2.5F, 0.0F}, 0.0F, 1);
        shop.host.steps(60, {&shop.clientA});
        shop.use_station(shop.clientA, noRow.entity);
        shop.craft(shop.clientA, kCharacterA, "TestKnifeT1", 1, {entry("TestFlintT1"), entry("TestWoodT1")});
        check(empty.crafting().counts().verdicts[static_cast<std::size_t>(world::CraftVerdict::noData)] == 1
                  && empty.count(kCharacterA, "TestFlintT1") == 1
                  && shop.crafting().counts().verdicts[static_cast<std::size_t>(world::CraftVerdict::unknownStation)] == 1
                  && shop.count(kCharacterA, "TestFlintT1") == 1,
              "no crafting data, and a station key with no row of the station database: refused, no change");
        // The camp: the same recipe (camp1), no storage, no fee (Camp5 has no fee flag).
        shop.host.world.inventory().set_coin(kCharacterA, 10);
        world::Entity* camp = shop.camp();
        shop.end_block(shop.clientA);
        shop.use_station(shop.clientA, camp);
        shop.craft(shop.clientA, kCharacterA, "TestKnifeT1", 1, {entry("TestFlintT1"), entry("TestWoodT1")}, 0);
        shop.finish();
        check(shop.count(kCharacterA, "TestKnifeT1") == 1 && shop.count(kCharacterA, "TestFlintT1") == 0 && shop.entity(kCharacterA)->wallet.coin == 10,
              "at the camp (Camp5, camp1): the knife from the bag, no fee");
    }
}

void two_sessions(const Options&) {
    Crafter shop(true);
    shop.enter(true);
    for (const char* character : {kCharacterA, kCharacterB}) {
        shop.host.world.inventory().set_coin(character, 1000);
        shop.give(character, "TestFlintT1", 2);
        shop.give(character, "TestWoodT1", 2);
    }
    const std::vector<Entry> knife{entry("TestFlintT1"), entry("TestWoodT1")};
    // B with no station use: refused. Then both use the station.
    shop.craft(shop.clientB, kCharacterB, "TestKnifeT1", 1, knife);
    shop.use_station(shop.clientA);
    shop.use_station(shop.clientB);
    const std::size_t fromA = shop.clientA.got.size();
    const std::size_t fromB = shop.clientB.got.size();
    world::Entity* a = shop.entity(kCharacterA);
    // B names the entity of A, then its own entity with the facet target of A: no change.
    shop.craft(shop.clientB, kCharacterB, "TestKnifeT1", 1, knife, 1, 100, {}, 0, crafting_target(a->id), a->id);
    shop.craft(shop.clientB, kCharacterB, "TestKnifeT1", 1, knife, 1, 100, {}, 0, crafting_target(a->id));
    check(shop.crafting().counts().requests == 1 && shop.crafting().batch_count() == 0,
          "B names the entity of A, and the facet of A with its own entity: the session refuses each (no request of the crafting system)");
    shop.craft(shop.clientA, kCharacterA, "TestKnifeT1", 2, knife);
    shop.craft(shop.clientB, kCharacterB, "TestKnifeT1", 1, knife);
    check(shop.crafting().batch_count() == 2, "A (a batch of 2) and B (1) at one station: two batches");
    shop.stop(shop.clientB, kCharacterB);
    shop.finish();
    check(shop.count(kCharacterA, "TestKnifeT1") == 2 && shop.count(kCharacterA, "TestFlintT1") == 0 && shop.count(kCharacterB, "TestKnifeT1") == 0
              && shop.count(kCharacterB, "TestFlintT1") == 2 && shop.entity(kCharacterA)->wallet.coin == 999
              && shop.entity(kCharacterB)->wallet.coin == 1000,
          "the stop of B ends only the batch of B: A gets its 2 knives (the fee 1 of its batch), B keeps its materials and its coin");
    const std::uint32_t knifeIndex = shop.host.catalog.find("TestKnifeT1")->index;
    bool bSawKnife = false;
    for (const SeenItem& seen : bag_elements(shop.clientB, fromB)) {
        bSawKnife = bSawKnife || seen.item.index == knifeIndex;
    }
    check(progresses(shop.clientB, fromB).size() == 0 && !bSawKnife && last_progress(shop.clientA, fromA, "TestKnifeT1", 2, 2)
              && progresses(shop.clientA, fromA).size() >= 1,
          "B gets no bag and no progress of A (owner states and messages); A gets its progress 2 of 2");
}

void load(const Options&) {
    const auto measure = [](std::size_t farChildren, std::size_t extraCharacters) {
        Crafter shop(false);
        StationParts parts = station_parts();
        for (std::size_t index = 0; index < farChildren; ++index) {
            gamedata::SpawnedPlacement child = parts.spawned.front();
            child.key = std::string(kSettlementKey) + "/far" + std::to_string(index);
            child.position = state::WorldPosition{1000.0F + 97.0F * static_cast<float>(index % 300), 1000.0F + 97.0F * static_cast<float>(index / 300),
                                                  30.0F};
            parts.spawned.push_back(child);
        }
        if (!set_stations(shop.host.interactions, std::move(parts))) {
            throw std::runtime_error("load: " + shop.host.interactions.error());
        }
        shop.enter(false);
        for (std::size_t index = 0; index < extraCharacters; ++index) {
            const std::string id = "1a7e0000-0000-4000-8000-1" + std::to_string(100000000000ULL + index);
            add_player(shop.host.store, std::to_string(76561202200000000ULL + index), "Load", id.c_str(), ("Loader" + std::to_string(index)).c_str(),
                       state::WorldPosition{5000.0F + static_cast<float>(index), 5000.0F, 30.0F});
            (void)shop.host.world.spawn_character(id, state::WorldPosition{5000.0F + static_cast<float>(index), 5000.0F, 30.0F});
        }
        shop.host.world.inventory().set_coin(kCharacterA, 1000000);
        shop.give(kCharacterA, "TestFlintT1", 1000);
        shop.give(kCharacterA, "TestWoodT1", 1000);
        shop.use_station(shop.clientA);
        // 10,000 validations (the checks and the plan of a request) with no change.
        const std::uint64_t slotsBefore = shop.crafting().counts().slotVisits;
        const auto start = std::chrono::steady_clock::now();
        std::uint32_t accepted = 0;
        for (int index = 0; index < 10000; ++index) {
            accepted += shop.crafting().preview(kCharacterA, "TestKnifeT1", 1).verdict == world::CraftVerdict::accepted ? 1 : 0;
        }
        const auto validations = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        // 200 crafts through the session: the request, the job, the records.
        const std::uint64_t jobVisits = shop.crafting().counts().jobVisits;
        const auto craftStart = std::chrono::steady_clock::now();
        for (int index = 0; index < 200; ++index) {
            shop.craft(shop.clientA, kCharacterA, "TestKnifeT1", 1, {entry("TestFlintT1"), entry("TestWoodT1")});
            shop.host.advance(std::chrono::milliseconds(1000));
            shop.host.steps(2, {&shop.clientA});
        }
        const auto crafts = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - craftStart);
        return std::make_tuple(accepted, validations, crafts, shop.crafting().counts().jobs, shop.crafting().counts().jobVisits - jobVisits,
                               shop.crafting().counts().slotVisits - slotsBefore);
    };
    const auto [acceptedSmall, validSmall, craftSmall, jobsSmall, jobVisitsSmall, slotsSmall] = measure(1000, 0);
    const auto [acceptedLarge, validLarge, craftLarge, jobsLarge, jobVisitsLarge, slotsLarge] = measure(20000, 100);
    note("CRAFT-LOAD: 10,000 validations " + std::to_string(validSmall.count()) + " ms (1,000 placements) and " + std::to_string(validLarge.count())
         + " ms (20,000 placements, 100 more characters); 200 crafts " + std::to_string(craftSmall.count()) + " ms and "
         + std::to_string(craftLarge.count()) + " ms; job visits " + std::to_string(jobVisitsSmall) + " and " + std::to_string(jobVisitsLarge)
         + "; slot visits " + std::to_string(slotsSmall) + " and " + std::to_string(slotsLarge));
    check(acceptedSmall == 10000 && acceptedLarge == 10000 && jobsSmall == 200 && jobsLarge == 200 && jobVisitsSmall == 200
              && jobVisitsLarge == 200 && slotsSmall == slotsLarge,
          "the same visits with 20,000 far placements and 100 more characters: one job visit for each craft, the same bag slots of the plans");
    check(validLarge.count() < 2000 && craftLarge.count() < 2000,
          "10,000 validations and 200 crafts with 20,000 placements, each in less than 2 s (" + std::to_string(validLarge.count()) + " ms, "
              + std::to_string(craftLarge.count()) + " ms)");
}

void decision_d1(const Options& options) {
    using nlohmann::json;
    // The loader keeps the override of a server child (an optional member of the format of tools/gamedata/interactions.py), and refuses an
    // override with no authored template or with an authored template that the file does not have.
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "interaction-data-d1.json";
    const json option = {{"name", "OpenCraftingOption"}, {"id", gamedata::interaction_option_id("OpenCraftingOption")}, {"action", 20},
                         {"committed", true}, {"delay", 0.0}, {"hold", 0.5}, {"instantCancel", false}, {"cooldown", 0}, {"maxUsage", 0.0},
                         {"inactive", 0.0}, {"requiredLevel", 0}, {"requiredItem", ""}, {"requiredItemCount", 0}, {"requiredCurrency", 0},
                         {"requiredAchievement", -1}, {"statusRules", false}};
    const auto slice = [](const char* name, const char* key, bool resolved, const char* asset) {
        return json{{"slice", name}, {"asset", asset}, {"sub", 2}, {"kind", "craftingStation"}, {"root", "0000000000003000"},
                    {"slots", {{"position", 2}, {"buildable", 1}, {"ownership", 0}, {"vitals", 0}}}, {"buildableState", 5},
                    {"permissionRule", -1},
                    {"assembly", {{"key", key}, {"fee", true}, {"entity", "0000000000003001"}, {"component", "698f02d45d873f1a"}, {"resolved", resolved}}},
                    {"bounds", json::array()}, {"children", json::array()}};
    };
    const json child = {{"key", kStationKey}, {"parent", kSettlementKey}, {"parentActor", "0b5e771e5345544c0000000000000000"},
                        {"parentSlice", "slices/test/settlement.dynamicslice"}, {"spawner", "Public_Carpentry"}, {"template", 1},
                        {"authoredTemplate", 0}, {"spawnOnInitialize", false}, {"region", "r_+00_+00"}, {"position", {100.0, 202.0, 30.0}},
                        {"rotation", {0.0, 0.0, 0.0, 1.0}},
                        {"override", {{"rule", "D1"}, {"from", "Carpentry2"}, {"to", "Carpentry3"}, {"slice", "slices/test/carpentry_t2.dynamicslice"}}}};
    const json good = {{"format", 2}, {"options", json::array({option})}, {"achievements", json::array()}, {"objects", json::array()},
                       {"templates", json::array({slice("slices/test/carpentry_master.dynamicslice", "Carpentry2", false, "a55e7501000000000000000000000002"),
                                                  slice("slices/test/carpentry_t2.dynamicslice", "Carpentry3", true, "a55e7502000000000000000000000002")})},
                       {"spawned", json::array({child})}, {"fixtures", json::object()},
                       {"counts", {{"objects", 0}, {"bounds", 0}, {"options", 1}, {"achievements", 0}, {"templates", 2}, {"spawned", 1}, {"fixtures", 0}}}};
    const auto load = [&](const json& document, gamedata::InteractionData& data) {
        std::ofstream(path) << document.dump();
        return data.load(path.string());
    };
    gamedata::InteractionData data;
    const bool loaded = load(good, data);
    check(loaded && data.spawned().size() == 1 && data.spawned()[0].templateIndex == 1 && data.spawned()[0].authoredTemplate == 0U
              && data.spawned()[0].overrideFrom == "Carpentry2" && data.spawned()[0].overrideTo == "Carpentry3"
              && data.templates()[data.spawned()[0].templateIndex].assembly->key == "Carpentry3",
          "a station child with the override of decision D1: the server makes the variant (Carpentry3), the authored template stays (" + data.error()
              + ")");
    json noAuthored = good;
    noAuthored["spawned"][0].erase("authoredTemplate");
    json outside = good;
    outside["spawned"][0]["authoredTemplate"] = 7;
    gamedata::InteractionData first;
    gamedata::InteractionData second;
    check(!load(noAuthored, first) && !load(outside, second), "an override with no authored template, or with a template that the file does "
                                                             "not have: the file is refused");
    std::filesystem::remove(path);
}

void commands(const Options&) {
    using nlohmann::json;
    Crafter shop(false);
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
    shop.give(kCharacterA, "TestFlintT1", 2);
    shop.give(kCharacterA, "TestWoodT1", 2);
    const std::string character = std::string("character=") + kCharacterA;
    const json noUse = json::parse(send("POST", "/v1/dev/crafting/preview", character + "&recipe=TestKnifeT1&quantity=2").body, nullptr, false);
    shop.use_station(shop.clientA);
    const json plan = json::parse(send("POST", "/v1/dev/crafting/preview", character + "&recipe=testknifet1&quantity=2").body, nullptr, false);
    const json view = json::parse(send("GET", "/v1/dev/crafting", character).body, nullptr, false);
    check(noUse.is_object() && noUse["verdict"] == world::craft_verdict_text(world::CraftVerdict::noStation) && noUse["costs"].size() == 2
              && plan.is_object() && plan["accepted"] == true && plan["fee"] == 1 && plan["gearScore"] == 100 && plan["output"]["item"] == "TestKnifeT1"
              && plan["station"]["assemblyKey"] == "Blacksmith5" && shop.count(kCharacterA, "TestFlintT1") == 2
              && view.is_object() && view["station"]["row"] == "Blacksmith5" && view["batch"].is_null() && view["supportedRecipes"] == 6,
          "the preview with no station use (noStation and the plan) and at the station (accepted, the fee 1, gear score 100): no change; the "
          "crafting view names the station row");
    shop.craft(shop.clientA, kCharacterA, "TestKnifeT1", 2, {entry("TestFlintT1"), entry("TestWoodT1")});
    const json running = json::parse(send("GET", "/v1/dev/crafting", character).body, nullptr, false);
    const service::HttpResponse stopped = send("POST", "/v1/dev/crafting/stop", character);
    const service::HttpResponse again = send("POST", "/v1/dev/crafting/stop", character);
    shop.finish();
    check(running.is_object() && running["batch"]["total"] == 2 && running["batch"]["done"] == 0 && stopped.status == 200 && again.status == 404
              && shop.count(kCharacterA, "TestKnifeT1") == 0 && shop.count(kCharacterA, "TestFlintT1") == 2
              && send("POST", "/v1/dev/crafting/preview", character + "&recipe=TestKnifeT1&quantity=0").status == 400,
          "the batch in the view, the stop command (200, then 404 with no batch): no job, no change; a count 0: 400");
}

void tool_check(const Options& options) {
    const std::filesystem::path script = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_crafting_tool.py";
    std::filesystem::create_directories(options.workDirectory / "crafting-tool");
    const std::string command = "python3 -B \"" + script.string() + "\" \"" + (options.workDirectory / "crafting-tool").string() + "\" > \""
                                + (options.workDirectory / "crafting-tool.log").string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    check(status == 0, "the check of the crafting data tool (check_crafting_tool.py): status 0 (log: work folder crafting-tool.log)");
}

} // namespace

std::vector<Case> crafting_cases() {
    return {{"CRAFT1", "crafting (3.7a, I3, I5): the request 2997 of L3, one knife, ItemCreated before 2993, the fee, a restart, the empty search",
             request_and_result},
            {"CRAFT2", "crafting (3.7a, I3 to I5, D5): recipe quantities, overlap, locks, quick slots, categories, a full bag, a late change, "
                     "repeats, stops, refusals, the camp",
             plan_and_refusals},
            {"CRAFT-MP", "crafting (3.7a, I6, MP-10): two sessions at one station, foreign entity and facet, owner views, the stop of one", two_sessions},
            {"CRAFT-LOAD", "crafting (3.7a, I6): 20,000 far placements and 100 characters: the same visits; 10,000 validations and 200 crafts in 2 s",
             load},
            {"CRAFT-D1", "crafting (3.7a, D1): the loader keeps the station override of a server child", decision_d1},
            {"CRAFT-CMD", "crafting (3.7a, I7): the development commands: the crafting view, the preview, the stop", commands},
            {"CRAFT-TOOL", "crafting (3.7a, I1): the check of the data tool (runs check_crafting_tool.py)", tool_check}};
}

} // namespace soulwarden::backend::tests

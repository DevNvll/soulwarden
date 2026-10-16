// The cases of stage 3.5b.1 (the ordinary chests; world/chests.h, docs/systems/chests.md; research chests-ground-loot.md, CH1 to CH8):
//   LC1  CH1, I1: the chest file and the loot tables of format 2 (the loader and its refusals); no chest data: each open is refused
//   LC2  CH2, I2: the open of a supply chest (2.5 s) and of an elite chest (5 s): the reply, the Gathering state, one completion, the
//        loot, the claim, the LootTracker; the ends before the time (the release, 2473, damage, the range) give nothing; a new open after
//        a cancel runs the full time again; the same block in each report is one open
//   LC3  CH3, I3: the roll of a chest: the content level of its territories (MinPOIContLevel), the territory tags, the container floor
//        of the gear score (not the creature ceiling); one seed gives one result
//   LC4  CH4, I4, C-I10: the claim in the store: a restart keeps it (the LootTracker of the first record, a refused open); the reset at its
//        time (supply: 3,600 s; the elite row: 82,800 s, the value of the game data) on a synthetic clock; a reset while the character is
//        away; the development override of a reset
//   LC5  CH5, I5, MP-10: two characters open one chest: two claims; the owner states go only to the owner; a second session of a
//        character does not give a second open
//   LC6  CH6, I6: a refused write (the claim row, the item rows) gives no item and no claim; a full bag keeps the roll in the claim (no item
//        is lost, no new roll), and the next open gives the kept items
//   LC7  CH7, I7: an unavailable chest is refused and has the Locked state in the LootTracker; an achievement gate of the data: the
//        character with the achievement opens, the other one is refused; a root with no table is refused
//   LC8  CH8, I8: the load: 100,000 far chest definitions and 50 characters (the time limit is in the case)
//   LC9  the chest tool (tools/gamedata/check_chests_tool.py): its stops and its census with made-up sources
// All values are made up.

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "support/cases.h"
#include "support/chest_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace chests;
using nlohmann::json;
using namespace std::chrono_literals;

/** @return The JSON document of a chest file of the tool with the chests of the fixture. */
json chest_document() {
    json document{{"format", 1}, {"rules", {{"resetMinutes", 60}, {"countLimit", 60}}}, {"gatherables", json::object()}, {"chests", json::array()}};
    for (const gamedata::ChestGatherable& row : chest_gatherables()) {
        document["gatherables"][row.id] = {{"open", row.openSeconds}, {"resetMin", row.resetMin}, {"resetMax", row.resetMax}, {"elite", row.elite},
                                           {"event", row.event}};
    }
    for (const gamedata::ChestDefinition& chest : chest_definitions()) {
        std::string actor;
        for (const std::uint8_t byte : chest.actor) {
            char digits[3];
            std::snprintf(digits, sizeof(digits), "%02x", byte);
            actor += digits;
        }
        document["chests"].push_back({{"key", chest.key},
                                      {"actor", actor},
                                      {"position", {chest.position.x, chest.position.y, chest.position.z}},
                                      {"variant", chest.variant},
                                      {"name", chest.name},
                                      {"gatherable", chest.gatherable >= 0 ? chest_gatherables()[static_cast<std::size_t>(chest.gatherable)].id : ""},
                                      {"root", chest.root},
                                      {"kind", gamedata::chest_kind_text(chest.kind)},
                                      {"tags", chest.tags},
                                      {"contentLevel", chest.contentLevel ? json(*chest.contentLevel) : json(nullptr)},
                                      {"state", chest.available ? "ordinary" : "unavailable"},
                                      {"reason", chest.reason},
                                      {"region", chest.region}});
    }
    return document;
}

bool loads(const Options& options, const json& document, const std::string& name, std::string* error = nullptr) {
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / ("chest-" + name + ".json");
    std::ofstream(path, std::ios::binary | std::ios::trunc) << document.dump();
    gamedata::ChestData data;
    const bool loaded = data.load(path.string());
    if (error != nullptr) {
        *error = data.error();
    }
    return loaded && data.loaded();
}

/** LC1: the chest file, the loot tables of format 2, and no chest data. */
void chest_data_rules(const Options& options) {
    std::string error;
    check(loads(options, chest_document(), "complete", &error), "a complete chest file loads (" + error + ")");
    {
        gamedata::ChestData data;
        check(set_chest_data(data, 10) && data.chests().size() == 17 && data.ordinary() == 16 && data.find("0B1EC700-0000-4000-8000-000000000001")
                  && *data.find("0b1ec700-0000-4000-8000-000000000001") == 0 && data.rules().resetMinutes == 60,
              "the data: 17 chests, 16 ordinary; a key in any case finds its chest");
        gamedata::ChestLookupWork work;
        std::size_t near = 0;
        data.for_each_near(kHere, 30.0F, [&](std::uint32_t) { ++near; }, &work);
        check(near == 6 && work.cells == 4 && work.chests == 6, "6 chests in 30 m; the 4 cells of 64 m near the place looked at");
    }
    struct Broken {
        const char* name;
        void (*change)(json&);
    };
    const Broken broken[] = {
        {"format", [](json& document) { document["format"] = 2; }},
        {"no-rules", [](json& document) { document.erase("rules"); }},
        {"reset-zero", [](json& document) { document["rules"]["resetMinutes"] = 0; }},
        {"key-twice", [](json& document) { document["chests"][1]["key"] = document["chests"][0]["key"]; }},
        {"key-not-uuid", [](json& document) { document["chests"][0]["key"] = "chest-1"; }},
        {"actor", [](json& document) { document["chests"][0]["actor"] = "zz"; }},
        {"gatherable", [](json& document) { document["chests"][0]["gatherable"] = "NoSuchRow"; }},
        {"ordinary-no-gatherable", [](json& document) { document["chests"][0]["gatherable"] = ""; }},
        {"ordinary-no-root", [](json& document) { document["chests"][0]["root"] = ""; }},
        {"unavailable-no-reason", [](json& document) { document["chests"][2]["reason"] = ""; }},
        {"open-zero", [](json& document) { document["gatherables"]["LootMediumT1"]["open"] = 0; }},
        {"reset-order", [](json& document) { document["gatherables"]["LootLargeT3ElitePOI"]["resetMin"] = 90000; }},
        {"position", [](json& document) { document["chests"][0]["position"] = {1.0, "x", 3.0}; }},
        {"level", [](json& document) { document["chests"][0]["contentLevel"] = 0; }},
        {"kind", [](json& document) { document["chests"][0]["kind"] = "golden"; }},
    };
    for (const Broken& variant : broken) {
        json document = chest_document();
        variant.change(document);
        check(!loads(options, document, variant.name, &error), std::string(variant.name) + ": the file is refused as a whole (" + error + ")");
    }
    gamedata::ChestData none;
    check(!none.load((options.workDirectory / "no-such-chest-file.json").string()) && !none.loaded(), "no file: no chest data");
    // The loot tables of format 2: a chest root names a table of the file.
    gamedata::LootData loot = chest_loot();
    loot.chestRoots.push_back("Test_NoSuchTable");
    gamedata::LootTables tables;
    check(!tables.set(loot) && !tables.loaded(), "loot tables: a chest root with no table is refused");
    gamedata::LootTables good;
    check(good.set(chest_loot()) && good.container_floor(12) == kFloor && good.table_index("test_chestroot") == 0,
          "loot tables: the container floor of a level, a root by its id in any case");
    // No chest data: the open of a chest is refused (3743 result 2), and no Gathering state goes out.
    ChestShop shop(":memory:", false, false);
    shop.enter();
    const std::size_t from = shop.clientA.got.size();
    shop.request(shop.clientA, shop.open_block(kSupply));
    check(replies(shop.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{14U, 2U}} && gather_states(shop.clientA, 1, from).empty()
              && shop.chests().counts().refusals[static_cast<std::size_t>(world::ChestRefusal::noData)] == 1,
          "no chest data: the open is refused, with no Gathering state");
}

/** LC2: the open, its time, its completion, and its ends. */
void chest_open(const Options&) {
    ChestShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    const InteractionBlock supply = shop.open_block(kSupply);
    std::size_t from = shop.clientA.got.size();
    shop.request(shop.clientA, supply);
    auto gathers = gather_states(shop.clientA, 1, from);
    check(replies(shop.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{14U, 1U}} && gathers.size() == 1
              && gathers[0].status == 1 && gathers[0].remaining == 2.5F && gathers[0].speed == 1.0F && gathers[0].reference == 1
              && gathers[0].active && gathers[0].target == shop.chest(kSupply).group,
          "the open: 3743 result 1, and the Gathering state Success, 2.5 s, speed 1, reference 1, the G of the chest");
    shop.hold(shop.clientA, supply, 2300);
    check(shop.chests().counts().completions == 0 && bag_count(a, "TestOre") == 0 && !tracker_state(shop.clientA, shop.chest(kSupply).actor),
          "2.4 s: no completion, no item, no claim");
    from = shop.clientA.got.size();
    shop.hold(shop.clientA, supply, 300);
    gathers = gather_states(shop.clientA, 1, from);
    check(shop.chests().counts().completions == 1 && shop.chests().counts().begins == 1 && bag_count(a, "TestOre") == 3
              && bag_count(a, "TestHide") == 2 && bag_count(a, "TestSword") == 1,
          "2.7 s: one completion of one open: 3 TestOre, 2 TestHide (the tag Everfall), 1 TestSword");
    check(gathers.size() == 1 && gathers[0].status == 3 && !gathers[0].active && gathers[0].target == kNoObject
              && tracker_state(shop.clientA, shop.chest(kSupply).actor) == world::chest_rules::kLooted,
          "the Gathering state Done, and the LootTracker has the chest Looted");
    // The client ends its use (2473 after Done: live check L3): no change.
    {
        world::Entity* entity = shop.entity(kCharacterA);
        server::hub::Writer cancel;
        cancel.u64(entity->id + server::rep::kInteractorComponent);
        cancel.u64(entity->id);
        cancel.raw(supply.actor);
        cancel.u64(supply.group);
        cancel.u64(supply.target);
        shop.host.datagram(shop.clientA.peer, shop.clientA.component_message(kCancelMessage, entity->actor.bytes, cancel.data()));
        shop.host.steps(3, {&shop.clientA, &shop.clientB});
    }
    shop.end_block(shop.clientA);
    check(!a.interaction && shop.chests().counts().completions == 1 && shop.chests().counts().cancels == 0 && bag_count(a, "TestOre") == 3,
          "the 2473 of the client after Done ends the use: no second completion, no cancel");
    // A second open of the looted chest: refused.
    from = shop.clientA.got.size();
    shop.request(shop.clientA, supply);
    shop.end_block(shop.clientA);
    check(replies(shop.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{14U, 2U}} && bag_count(a, "TestOre") == 3,
          "a second open of the looted chest: refused, no item");

    // The ends before the time, on the elite chest (5 s): the release, damage, and the range give nothing.
    const InteractionBlock elite = shop.open_block(kElite);
    shop.request(shop.clientA, elite);
    shop.hold(shop.clientA, elite, 1000);
    from = shop.clientA.got.size();
    shop.end_block(shop.clientA);
    shop.hold(shop.clientA, std::nullopt, 5000);
    gathers = gather_states(shop.clientA, 1, from);
    check(shop.chests().counts().cancels == 1 && bag_count(a, "TestArmorT2") == 0 && !gathers.empty() && gathers[0].status == 2,
          "the release at 1 s: the Gathering state Failure, no item after 6 s");
    from = shop.clientA.got.size();
    shop.request(shop.clientA, elite);
    shop.hold(shop.clientA, elite, 1000);
    (void)shop.host.world.health().hurt(kCharacterA, 10.0F);
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    check(count_type(shop.clientA, kServerCancel, from) == 1 && shop.chests().counts().cancels == 2 && !a.interaction,
          "damage at 1 s: the use ends (3746), a cancel");
    shop.end_block(shop.clientA);
    shop.hold(shop.clientA, std::nullopt, 5000);
    check(bag_count(a, "TestArmorT2") == 0, "damage: no item");
    from = shop.clientA.got.size();
    shop.request(shop.clientA, elite);
    shop.hold(shop.clientA, elite, 1000);
    for (int step = 0; step < 30; ++step) { // A walk to the north at 5 m/s: 15 m from the place, out of the range of the chest (10 m).
        shop.placeA.y += 0.5F;
        shop.report(shop.clientA, elite);
        shop.host.steps(10, {&shop.clientA, &shop.clientB});
    }
    check(count_type(shop.clientA, kServerCancel, from) == 1 && shop.chests().counts().cancels == 3, "the range lost: the use ends (3746)");
    shop.end_block(shop.clientA);
    shop.placeA = kHere;
    shop.hold(shop.clientA, std::nullopt, 6000);
    check(bag_count(a, "TestArmorT2") == 0, "the range lost: no item");
    // A new open after the cancels runs its full time (5 s) and completes one time; the same block in each report is one open.
    const std::uint64_t begins = shop.chests().counts().begins;
    shop.request(shop.clientA, elite);
    shop.hold(shop.clientA, elite, 4700);
    check(bag_count(a, "TestArmorT2") == 0 && shop.chests().counts().begins == begins + 1, "4.8 s: no completion; one open for the held block");
    shop.hold(shop.clientA, elite, 400);
    check(bag_count(a, "TestArmorT2") == 1 && bag_count(a, "TestOre") == 8 && shop.chests().counts().completions == 2,
          "5.2 s: the elite completion: TestArmorT2 and 5 TestOre");
}

/** LC3: the context of the roll of a chest, the container floor, and one seed. */
void chest_roll(const Options&) {
    ChestShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    const auto open = [&](std::uint8_t tag) {
        shop.request(shop.clientA, shop.open_block(tag));
        shop.hold(shop.clientA, shop.open_block(tag), 2700);
        shop.end_block(shop.clientA);
    };
    open(kSupply);
    const world::ItemInstance* sword = nullptr;
    for (const world::BagSlot& slot : a.bag.slots) {
        if (slot.item && slot.item->itemId == "TestSword") {
            sword = &*slot.item;
        }
    }
    check(sword != nullptr && sword->gearScore == kFloor && bag_count(a, "TestSwordT3") == 0,
          "the supply chest (content level 12, character level 30): MinPOIContLevel 12 gives TestSword, not TestSwordT3; its gear score "
          "is the container floor (" + std::to_string(sword != nullptr ? sword->gearScore : 0) + ")");
    open(kProvision);
    check(bag_count(a, "TestSwordT3") == 1 && bag_count(a, "TestHide") == 2,
          "the provision chest (content level 25, the tag Brimstone): TestSwordT3; no TestHide of the tag Everfall");
    // The rule of the container in the roller: the same table with a creature context has the creature ceiling, not the floor.
    world::LootContext creature;
    creature.characterLevel = 30;
    creature.creatureLevel = 12;
    creature.tags = {"Everfall"};
    const std::uint32_t gear = *shop.host.loot.table_index("Test_ChestGear");
    const std::vector<world::LootDrop> creatureDrops = shop.host.world.rewards().roll_root(gear, creature);
    world::LootContext container = creature;
    container.container = true;
    const std::vector<world::LootDrop> containerDrops = shop.host.world.rewards().roll_root(gear, container);
    check(creatureDrops.empty() && containerDrops.size() == 1 && containerDrops[0].gearScore == kFloor,
          "the table with MinPOIContLevel: no item with a creature context (the tag is not numeric there), the floor with a container");
    // One seed gives one result (a second world with the same seed and the same opens).
    ChestShop again;
    again.enter();
    again.request(again.clientA, again.open_block(kSupply));
    again.hold(again.clientA, again.open_block(kSupply), 2700);
    const world::Entity& second = *again.entity(kCharacterA);
    check(bag_count(second, "TestOre") == 3 && bag_count(second, "TestSword") == 1, "the same seed: the same items");
}

/** LC4: the claim in the store, the restart, the resets, and the development override. */
void chest_persistence(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "chest-claims.sqlite3";
    std::filesystem::remove(path);
    std::chrono::nanoseconds firstWorld{0};
    {
        ChestShop shop(path.string());
        shop.enter();
        for (const std::uint8_t tag : {kSupply, kElite}) {
            shop.request(shop.clientA, shop.open_block(tag));
            shop.hold(shop.clientA, shop.open_block(tag), 5200);
            shop.end_block(shop.clientA);
        }
        const auto rows = shop.host.store.list_loot_claims(kCharacterA);
        check(rows.size() == 2 && rows[0].state == 2 && rows[1].state == 2 && rows[0].items.empty(),
              "two looted claims in the store (no items kept)");
        const auto supply = std::find_if(rows.begin(), rows.end(), [&](const state::LootClaimRow& row) { return row.objectKey == shop.chest(kSupply).key; });
        const auto elite = std::find_if(rows.begin(), rows.end(), [&](const state::LootClaimRow& row) { return row.objectKey == shop.chest(kElite).key; });
        check(supply != rows.end() && elite != rows.end() && supply->resetAt - supply->lootedAt == 3600000
                  && elite->resetAt - elite->lootedAt == 82800000,
              "the resets: 3,600 s for the supply chest (the base attributes: 60 minutes), 82,800 s for the elite row (its gatherable row)");
        firstWorld = std::chrono::duration_cast<std::chrono::nanoseconds>(shop.host.clock.now().time_since_epoch()) - std::chrono::seconds(1'000'000);
    }
    {
        // A restart of the backend: the same database, a new world. Its clock starts at the time of the end of the first world (the
        // manual clock of a test starts at one value: the wall clock of the new world is the wall clock of the end of the first one).
        ChestShop shop(path.string(), true);
        shop.host.clock.advance(firstWorld);
        shop.enter();
        world::Entity& a = *shop.entity(kCharacterA);
        check(tracker_state(shop.clientA, shop.chest(kSupply).actor) == world::chest_rules::kLooted
                  && tracker_state(shop.clientA, shop.chest(kElite).actor) == world::chest_rules::kLooted,
              "the restart: the first record of A has the two chests Looted");
        const std::uint32_t oreBefore = bag_count(a, "TestOre");
        std::size_t from = shop.clientA.got.size();
        shop.request(shop.clientA, shop.open_block(kSupply));
        shop.end_block(shop.clientA);
        check(replies(shop.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{14U, 2U}} && bag_count(a, "TestOre") == oreBefore,
              "the restart: an open of the looted chest is refused (no new item)");
        // The resets on a synthetic clock (the world ticks at 1 s), from the remaining time of each claim (the wall clock of its row).
        double supplyLeft = 0.0;
        double eliteLeft = 0.0;
        for (const world::ClaimView& claim : shop.chests().view(kCharacterA, 0.0F).claims) {
            (claim.key == shop.chest(kSupply).key ? supplyLeft : eliteLeft) = claim.resetSeconds;
        }
        note("the time to the resets after the restart: " + std::to_string(supplyLeft) + " s and " + std::to_string(eliteLeft) + " s");
        check(supplyLeft > 3590.0 && supplyLeft <= 3600.0 && eliteLeft > 82790.0 && eliteLeft <= 82800.0,
              "the restart keeps the remaining time of each claim (the supply chest about 3,600 s, the elite chest about 82,800 s)");
        // The clients read and acknowledge at each step: their sessions stay (a session with no datagram ends after 30 s).
        const auto pass = [&](double seconds) {
            for (double passed = 0.0; passed < seconds; passed += 1.0) {
                shop.host.advance(1s);
                shop.host.tick();
                shop.clientA.exchange(shop.host);
                shop.clientB.exchange(shop.host);
            }
        };
        pass(supplyLeft - 5.0);
        check(shop.chests().counts().resets == 0, "5 s before the supply reset: no reset");
        from = shop.clientA.got.size();
        pass(10.0);
        shop.host.steps(3, {&shop.clientA, &shop.clientB});
        check(shop.chests().counts().resets == 1 && !tracker_state(shop.clientA, shop.chest(kSupply).actor, from)
                  && tracker_state(shop.clientA, shop.chest(kElite).actor, from) == world::chest_rules::kLooted
                  && shop.host.store.list_loot_claims(kCharacterA).size() == 1,
              "5 s after the supply reset: its row goes away and the map has no entry; the elite claim stays");
        // The elite row: 82,800 s (C-I10, the value of the game data).
        pass(eliteLeft - supplyLeft - 10.0 - 5.0);
        check(shop.chests().counts().resets == 1, "5 s before the elite reset (82,800 s after its open): the elite claim stays");
        pass(10.0);
        check(shop.chests().counts().resets == 2 && shop.host.store.list_loot_claims(kCharacterA).empty(), "5 s after it: the elite claim resets");
        from = shop.clientA.got.size();
        shop.request(shop.clientA, shop.open_block(kSupply));
        shop.hold(shop.clientA, shop.open_block(kSupply), 2700);
        shop.end_block(shop.clientA);
        check(bag_count(a, "TestOre") == oreBefore + 3, "after the reset the chest opens again");
        // The development override (live check L8): 5 s.
        check(shop.chests().set_reset(kCharacterA, shop.chest(kSupply).key, 0s) == world::ClaimError::badTime
                  && shop.chests().set_reset(kCharacterA, shop.chest(kSupply).key, std::chrono::seconds(8 * 24 * 3600)) == world::ClaimError::badTime
                  && shop.chests().set_reset(kCharacterA, shop.chest(kElite).key, 5s) == world::ClaimError::noClaim
                  && shop.chests().set_reset(kCharacterB, shop.chest(kSupply).key, 5s) == world::ClaimError::noClaim,
              "the override refuses 0 s, more than a week, and a chest with no looted claim");
        check(shop.chests().set_reset(kCharacterA, shop.chest(kSupply).key, 5s) == world::ClaimError::none, "the override: 5 s");
        pass(4.0);
        check(shop.chests().counts().resets == 2, "4 s: no reset");
        pass(2.0);
        check(shop.chests().counts().resets == 3, "6 s: the reset of the override");
        // A claim whose reset comes while the character is away: the next entry removes it.
        shop.request(shop.clientA, shop.open_block(kSupply));
        shop.hold(shop.clientA, shop.open_block(kSupply), 2700);
        shop.end_block(shop.clientA);
        check(shop.host.store.list_loot_claims(kCharacterA).size() == 1, "a new claim");
        firstWorld = std::chrono::duration_cast<std::chrono::nanoseconds>(shop.host.clock.now().time_since_epoch()) - std::chrono::seconds(1'000'000);
    }
    {
        ChestShop shop(path.string(), true);
        shop.host.clock.advance(firstWorld + 3601s); // The wall clock of the new world: one hour after the end of the world before.
        shop.enter();
        check(shop.host.store.list_loot_claims(kCharacterA).empty() && shop.chests().claim_count() == 0
                  && !tracker_state(shop.clientA, shop.chest(kSupply).actor),
              "the reset came while the character was away: the entry removes the row, and the map has no entry");
    }
}

/** LC5: two characters, one chest; the owner states; a second session. */
void chest_two_sessions(const Options&) {
    ChestShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    world::Entity& b = *shop.entity(kCharacterB);
    const InteractionBlock supply = shop.open_block(kSupply);
    const std::size_t fromB = shop.clientB.got.size();
    shop.request(shop.clientA, supply);
    shop.hold(shop.clientA, supply, 2700);
    shop.end_block(shop.clientA);
    check(bag_count(a, "TestOre") == 3 && bag_count(b, "TestOre") == 0, "A opens the chest: A gets the items, B nothing");
    check(gather_states(shop.clientB, 1, fromB).empty() && tracker_maps(shop.clientB, 1, fromB).empty(),
          "B gets no Gathering state and no LootTracker state of its own");
    bool observerFragment = false;
    for (const std::uint32_t slot : {kGatherSlot, kTrackerSlot}) {
        for (const Seen& seen : fragments_of(shop.clientB, slot, slot == kGatherSlot ? kGatherType : kTrackerType, fromB)) {
            observerFragment = observerFragment || seen.interest != 1;
        }
    }
    check(!observerFragment, "B gets no Gathering and no LootTracker state of A (owner only)");
    // B opens the same chest: its own claim.
    std::size_t from = shop.clientB.got.size();
    shop.request(shop.clientB, supply);
    shop.hold(shop.clientB, supply, 2700);
    shop.end_block(shop.clientB);
    check(replies(shop.clientB, from) == std::vector<std::pair<unsigned, unsigned>>{{14U, 1U}} && bag_count(b, "TestOre") == 3
              && tracker_state(shop.clientB, shop.chest(kSupply).actor) == world::chest_rules::kLooted,
          "B opens the chest that A looted: its own claim and items");
    // Both open the elite chest at the same time: two opens, two completions.
    const InteractionBlock elite = shop.open_block(kElite);
    shop.report(shop.clientA, elite);
    shop.report(shop.clientB, elite);
    for (int step = 0; step < 52; ++step) {
        shop.report(shop.clientA, elite);
        shop.report(shop.clientB, elite);
        shop.host.steps(10, {&shop.clientA, &shop.clientB});
    }
    shop.end_block(shop.clientA);
    shop.end_block(shop.clientB);
    check(bag_count(a, "TestArmorT2") == 1 && bag_count(b, "TestArmorT2") == 1 && shop.chests().counts().completions == 4,
          "A and B open the elite chest at the same time: one completion for each");
    // A second session of A (a new client with the same character): the old session ends; the claim stays.
    FakeClient second{27002};
    shop.host.enter(second, shop.a.registration());
    shop.host.steps(30, {&second, &shop.clientB});
    world::Entity* again = shop.entity(kCharacterA);
    check(again != nullptr && again->owner != 0, "the second session of A plays the character");
    InputEntry entry = report_entry(1, kHere.x, kHere.y, kHere.z);
    for (std::uint16_t step = 1; step < 4; ++step) {
        entry = report_entry(step, kHere.x, kHere.y, kHere.z);
        shop.host.datagram(second.peer, second.input_report(entry));
        shop.host.steps(3, {&second, &shop.clientB});
    }
    from = second.got.size();
    entry = report_entry(4, kHere.x, kHere.y, kHere.z);
    entry.interaction = supply;
    shop.host.datagram(second.peer, second.input_report(entry));
    shop.host.steps(5, {&second, &shop.clientB});
    check(replies(second, from) == std::vector<std::pair<unsigned, unsigned>>{{14U, 2U}} && bag_count(*again, "TestOre") == 8,
          "the second session of A: the open of the looted chest is refused (no second claim)");
}

/** LC6: a refused write changes nothing; a full bag keeps the roll. */
void chest_transactions(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    for (const std::string tableName : {"loot_claims", "items"}) {
        const auto path = options.workDirectory / ("chest-failure-" + tableName + ".sqlite3");
        std::filesystem::remove(path);
        ChestShop shop(path.string());
        sqlite3* connection = nullptr;
        const std::string sql = "CREATE TRIGGER refuse_chest BEFORE INSERT ON " + tableName + " BEGIN SELECT RAISE(FAIL, 'chest write failure'); END";
        check(sqlite3_open(path.string().c_str(), &connection) == SQLITE_OK
                  && sqlite3_exec(connection, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK,
              "setup: the database refuses one kind of write");
        sqlite3_close(connection);
        shop.enter();
        world::Entity& a = *shop.entity(kCharacterA);
        const std::size_t from = shop.clientA.got.size();
        shop.request(shop.clientA, shop.open_block(kSupply));
        shop.hold(shop.clientA, shop.open_block(kSupply), 2700);
        const auto gathers = gather_states(shop.clientA, 1, from);
        check(bag_count(a, "TestOre") == 0 && shop.host.store.list_items(kCharacterA).empty() && shop.host.store.list_loot_claims(kCharacterA).empty()
                  && shop.chests().claim_count() == 0 && shop.chests().counts().storeFailures == 1 && count_type(shop.clientA, kServerCancel, from) == 1
                  && !gathers.empty() && gathers.back().status == 2,
              tableName + ": a refused write gives no item, no item row, and no claim; the use ends (3746, Failure)");
        shop.end_block(shop.clientA);
    }
    // A full bag (500 slots: the container limit of the client): the roll stays in the claim.
    ChestShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    check(shop.host.world.inventory().give(kCharacterA, "TestArmorT2", world::kMaxContainerSlots).ok()
              && bag_count(a, "TestArmorT2") == world::kMaxContainerSlots,
          "setup: 500 armor items fill the bag");
    std::size_t from = shop.clientA.got.size();
    shop.request(shop.clientA, shop.open_block(kSupply));
    shop.hold(shop.clientA, shop.open_block(kSupply), 2700);
    shop.end_block(shop.clientA);
    const auto rows = shop.host.store.list_loot_claims(kCharacterA);
    check(bag_count(a, "TestOre") == 0 && rows.size() == 1 && rows[0].state == 1 && rows[0].items.size() == 3 && shop.chests().counts().kept == 1
              && count_type(shop.clientA, kServerCancel, from) == 1 && tracker_state(shop.clientA, shop.chest(kSupply).actor) == world::chest_rules::kRolled,
          "a full bag: no item, the roll stays in the claim (Rolled, 3 items), the use ends, the LootTracker has Rolled");
    std::uint32_t keptGearScore = 0;
    for (const state::ClaimItemRow& item : rows[0].items) {
        keptGearScore = item.itemId == "TestSword" ? item.gearScore.value_or(0) : keptGearScore;
    }
    check(keptGearScore == kFloor, "the kept roll has the sword with its gear score");
    // A second open with the bag still full: the same kept roll, no new roll.
    shop.request(shop.clientA, shop.open_block(kSupply));
    shop.hold(shop.clientA, shop.open_block(kSupply), 2700);
    shop.end_block(shop.clientA);
    check(shop.chests().counts().rolls == 1 && shop.chests().counts().kept == 2 && shop.host.store.list_loot_claims(kCharacterA)[0].items.size() == 3,
          "a second open with a full bag: no new roll");
    // Space in the bag: the next open gives the kept items.
    check(shop.host.world.inventory().remove(kCharacterA, 0).ok() && shop.host.world.inventory().remove(kCharacterA, 1).ok()
              && shop.host.world.inventory().remove(kCharacterA, 2).ok(),
          "setup: three slots free");
    shop.request(shop.clientA, shop.open_block(kSupply));
    shop.hold(shop.clientA, shop.open_block(kSupply), 2700);
    shop.end_block(shop.clientA);
    const world::ItemInstance* sword = nullptr;
    for (const world::BagSlot& slot : a.bag.slots) {
        if (slot.item && slot.item->itemId == "TestSword") {
            sword = &*slot.item;
        }
    }
    check(bag_count(a, "TestOre") == 3 && bag_count(a, "TestHide") == 2 && sword != nullptr && sword->gearScore == keptGearScore
              && shop.chests().counts().rolls == 1 && shop.host.store.list_loot_claims(kCharacterA)[0].state == 2,
          "space in the bag: the kept items (the same gear score), no new roll, the claim Looted");
}

/** LC7: the gates. */
void chest_gates(const Options&) {
    ChestShop shop;
    // A has the achievement of the gate of the chest kGated (the requirement of its Interact component), B has not.
    check(shop.host.store.grant_achievements(kCharacterA, {"Lore_unlock_Test_Page_01"}), "setup: A has the achievement");
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    world::Entity& b = *shop.entity(kCharacterB);
    check(tracker_state(shop.clientA, shop.chest(kLocked).actor) == world::chest_rules::kLocked
              && tracker_state(shop.clientB, shop.chest(kLocked).actor) == world::chest_rules::kLocked,
          "the unavailable chest near each character has the state Locked in its LootTracker (live check L9: no prompt)");
    std::size_t from = shop.clientA.got.size();
    shop.request(shop.clientA, shop.open_block(kLocked));
    shop.end_block(shop.clientA);
    check(replies(shop.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{14U, 2U}}
              && shop.chests().counts().refusals[static_cast<std::size_t>(world::ChestRefusal::unavailable)] == 1,
          "an open of the unavailable chest is refused (its reason)");
    from = shop.clientA.got.size();
    const std::size_t fromB = shop.clientB.got.size();
    shop.request(shop.clientA, shop.open_block(kGated));
    shop.request(shop.clientB, shop.open_block(kGated));
    shop.hold(shop.clientA, shop.open_block(kGated), 2700);
    shop.end_block(shop.clientA);
    shop.end_block(shop.clientB);
    check(replies(shop.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{14U, 1U}} && bag_count(a, "TestOre") == 3
              && replies(shop.clientB, fromB) == std::vector<std::pair<unsigned, unsigned>>{{14U, 2U}} && bag_count(b, "TestOre") == 0,
          "the achievement gate: A (with it) opens the chest, B (without it) is refused");
    from = shop.clientA.got.size();
    shop.request(shop.clientA, shop.open_block(kNoLoot));
    shop.end_block(shop.clientA);
    check(replies(shop.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{14U, 2U}}
              && shop.chests().counts().refusals[static_cast<std::size_t>(world::ChestRefusal::noLoot)] == 1,
          "a chest whose root has no loot table is refused");
}

/**
 * LC8: the load. Limit (written before the run): with 100,000 far chest definitions and 50 characters, 50 world entries with 10
 * claims each, 1,000 moves (each to a new cell of 64 m: a new LootTracker), 100,000 lookups of a chest by its key, and 3,600 world
 * ticks with the 500 resets take 3 s or less; the cells and the chests that the LootTracker looks at, and the due resets that the tick
 * looks at, are the same as with 1,000 far definitions.
 */
void chest_load(const Options&) {
    struct Result {
        world::ChestCounts counts;
        double seconds{0.0};
        std::size_t lookups{0};
    };
    const auto run = [](std::size_t far) {
        ServerHost host;
        check(set_chest_interactions(host.interactions) && host.loot.set(chest_loot()) && set_chest_data(host.chests, far), "setup: the data");
        host.catalog.set(chest_items());
        const auto start = std::chrono::steady_clock::now();
        const std::int64_t wallNow = static_cast<std::int64_t>(host.clock.wall_time_ns(host.clock.now()) / 1000000);
        for (unsigned index = 0; index < 50; ++index) {
            char id[37];
            std::snprintf(id, sizeof(id), "c0ad0000-0000-4000-8000-%012x", index);
            const state::WorldPosition place{100.0F + static_cast<float>(index) * 0.1F, 200.0F, 30.0F};
            add_player(host.store, "7656120225534" + std::to_string(1000 + index), "Tester", id, "Load" + std::to_string(index), place);
            for (unsigned claim = 0; claim < 10; ++claim) {
                state::LootClaimRow row;
                char far[37];
                std::snprintf(far, sizeof(far), "fa000000-0000-4000-8000-%012x", claim);
                row.objectKey = claim < 5 ? chest_definitions()[claim].key : std::string(far);
                row.state = 2;
                row.generation = 1;
                row.rolledAt = row.lootedAt = wallNow;
                row.resetAt = wallNow + 600000 + claim * 1000;
                check(host.store.write_loot_claim(id, row), "setup: a claim row");
            }
            check(host.world.spawn_character(id, place) == world::SpawnError::none, "setup: a character in the world");
        }
        for (unsigned move = 0; move < 1000; ++move) {
            char id[37];
            std::snprintf(id, sizeof(id), "c0ad0000-0000-4000-8000-%012x", move % 50);
            world::Entity* entity = host.world.find_character(id);
            const float x = 100.0F + static_cast<float>((move / 50) % 2) * 70.0F;
            (void)host.world.set_position(entity->id, {x, 200.0F, 30.0F}, false);
        }
        std::size_t found = 0;
        const std::vector<gamedata::ChestDefinition>& chests = host.chests.chests();
        for (std::size_t lookup = 0; lookup < 100000; ++lookup) {
            found += host.chests.find(chests[lookup % chests.size()].key).has_value() ? 1 : 0;
        }
        for (int tick = 0; tick < 3600; ++tick) {
            host.clock.advance(1s);
            host.world.tick();
        }
        Result result;
        result.counts = host.world.chests().counts();
        result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        result.lookups = found;
        return result;
    };
    const Result small = run(1000);
    const Result large = run(100000);
    note("1,000 far: " + std::to_string(small.seconds) + " s, " + std::to_string(small.counts.trackers) + " maps, " + std::to_string(small.counts.lookups.cells)
         + " cells, " + std::to_string(small.counts.lookups.chests) + " chests, " + std::to_string(small.counts.trackerClaims) + " claims, "
         + std::to_string(small.counts.dueLooks) + " due resets, " + std::to_string(small.counts.resets) + " resets");
    note("100,000 far: " + std::to_string(large.seconds) + " s, " + std::to_string(large.counts.trackers) + " maps, "
         + std::to_string(large.counts.lookups.cells) + " cells, " + std::to_string(large.counts.lookups.chests) + " chests, "
         + std::to_string(large.counts.trackerClaims) + " claims, " + std::to_string(large.counts.dueLooks) + " due resets, "
         + std::to_string(large.counts.resets) + " resets");
    check(large.counts.trackers == small.counts.trackers && large.counts.lookups.cells == small.counts.lookups.cells
              && large.counts.lookups.chests == small.counts.lookups.chests && large.counts.trackerClaims == small.counts.trackerClaims
              && large.counts.dueLooks == small.counts.dueLooks && large.counts.resets == 500 && small.counts.resets == 500,
          "the work of the maps and of the due resets is the same with 1,000 and with 100,000 far definitions (500 resets each)");
    check(large.lookups == 100000 && small.lookups == 100000, "100,000 lookups by key find their chests");
    check(large.seconds <= 3.0 && small.seconds <= 3.0, "each run takes 3 s or less");
}

/** LC9: the check of the chest tool, with made-up sources. */
void chest_tool(const Options& options) {
    const std::filesystem::path script = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_chests_tool.py";
    check(std::filesystem::is_regular_file(script), "the check of the chest tool is in the tree");
    std::filesystem::create_directories(options.workDirectory);
    const auto output = options.workDirectory / "chest-tool-check.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + script.lexically_normal().string() + "\" \"" + (options.workDirectory / "chest-tool").string() + "\" > \""
                                + output.string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream log(output);
    std::set<std::string> passed;
    std::set<std::string> failed;
    for (std::string line; std::getline(log, line);) {
        note(line);
        if (line.rfind("ok  ", 0) == 0) {
            passed.insert(line.substr(4, line.find(':') - 4));
        } else if (line.rfind("FAIL", 0) == 0) {
            failed.insert(line);
        }
    }
    check(status == 0 && failed.empty() && passed.size() >= 10, "each case of the check of the chest tool passes (" + std::to_string(passed.size()) + ")");
}

} // namespace

std::vector<Case> chest_cases() {
    return {{"LC1", "chests: the chest file, the loot tables of format 2, no chest data", chest_data_rules},
            {"LC2", "chests: the open, its time, its completion, its ends", chest_open},
            {"LC3", "chests: the context of the roll, the container floor, one seed", chest_roll},
            {"LC4", "chests: the claim in the store, the restart, the resets (elite 82,800 s), the override", chest_persistence},
            {"LC5", "chests: two characters, one chest; owner states; a second session", chest_two_sessions},
            {"LC6", "chests: a refused write changes nothing; a full bag keeps the roll", chest_transactions},
            {"LC7", "chests: an unavailable chest, an achievement gate, a root with no table", chest_gates},
            {"LC8", "chests: the load (100,000 far definitions, 50 characters)", chest_load},
            {"LC9", "chests: the check of the chest tool (runs tools/gamedata/check_chests_tool.py)", chest_tool}};
}

} // namespace soulwarden::backend::tests

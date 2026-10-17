// Cases for the findings of the code review of stage 1.3: a drop request for a locked item,
// the quantity limit of a lock request, a character delete whose database write failed, and a
// spawn near a character with an offset that is not a number. The cases B10 and B11 are the
// two cases of that review that passed: a change whose write fails, and the migration of each
// older database.
//
// The review finding for the item catalog tool (an input that is not complete) has no case
// here: the tool is a Python program. tools/gamedata/item_catalog.py refuses such an input.

#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "core/encoding/base64.h"
#include "server/http/router.h"
#include "server/http/routes.h"
#include "server/rep/item_requests.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/server_host.h"
#include "support/test_players.h"
#include "world/inventory.h"

namespace soulwarden::backend::tests {
namespace {

constexpr const char* kCharacterA = "aaaaaaaa-0000-4000-8000-00000000000a";
constexpr const char* kCharacterB = "bbbbbbbb-0000-4000-8000-00000000000b";

gamedata::ItemDefinition ore() {
    gamedata::ItemDefinition item;
    item.id = "TestOre";
    item.type = "Resource";
    item.itemClass = "Resource";
    item.maxStack = 100;
    return item;
}

struct Sink : world::SessionSink {
    bool lost{false};
    void character_lost(world::LostCause) override {
        lost = true;
    }
};

/** Runs one SQL text on a second connection of a database file. */
bool run_sql(const std::filesystem::path& path, const std::string& sql) {
    sqlite3* connection = nullptr;
    if (sqlite3_open(path.string().c_str(), &connection) != SQLITE_OK) {
        sqlite3_close(connection);
        return false;
    }
    const bool ok = sqlite3_exec(connection, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(connection);
    return ok;
}

int schema_version(const std::filesystem::path& path) {
    sqlite3* connection = nullptr;
    sqlite3_open(path.string().c_str(), &connection);
    sqlite3_stmt* query = nullptr;
    sqlite3_prepare_v2(connection, "PRAGMA user_version", -1, &query, nullptr);
    const int version = sqlite3_step(query) == SQLITE_ROW ? sqlite3_column_int(query, 0) : -1;
    sqlite3_finalize(query);
    sqlite3_close(connection);
    return version;
}

/** L1: a drop request for a locked item, and a lock request with a quantity above the limit (review findings 1 and 4). */
void locked_items(const Options&) {
    using namespace server::rep;
    ServerHost host;
    host.catalog.set({ore()});
    const Player player = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", std::nullopt);
    FakeClient client(27000);
    host.enter(client, player.registration());
    world::Inventory& inventory = host.world.inventory();
    inventory.give(kCharacterA, "TestOre", 150);
    const world::Entity* own = host.world.find_character(kCharacterA);
    const world::InstanceId first = own->bag.slots[0].item->instance;
    const world::InstanceId second = own->bag.slots[1].item->instance;
    const auto drop = [&](std::initializer_list<std::pair<std::uint32_t, world::InstanceId>> lines) {
        server::hub::Writer body;
        body.u64(own->id + 0x40);
        body.u64(own->id);
        body.size(static_cast<std::uint32_t>(lines.size()));
        for (const auto& [slot, instance] : lines) {
            body.u32(slot);
            body.raw(instance);
            body.u32(0xFFFFFFFF);
            body.u32(10);
        }
        host.datagram(client.peer, client.component_message(kTypeDropInventoryItems, own->actor.bytes, body.data()));
        host.steps(2, {&client});
    };
    const auto lock = [&](std::uint32_t slot, const world::InstanceId& instance, std::uint32_t quantity, std::uint8_t locked) {
        server::hub::Writer body;
        body.u64(own->id + 0x41);
        body.u64(own->id);
        body.u32(1);
        body.u32(slot);
        body.raw(instance);
        body.u32(quantity);
        body.u8(locked);
        host.datagram(client.peer, client.component_message(kTypeSetLockState, own->actor.bytes, body.data()));
        host.steps(2, {&client});
    };

    lock(0, first, 0xFFFFFFFF, 1);
    lock(0, first, 10001, 1);
    check(own->bag.slots[0].item->flags == 0 && host.store.list_items(kCharacterA)[0].flags == 0
              && log_count("rep: lock request (type 2549, 45 bytes) does not have the form of the request: no change") == 2,
          "a lock request with a quantity above 10000 changes nothing");
    lock(0, first, 10000, 1);
    check(own->bag.slots[0].item->flags == world::kItemLocked, "a lock request with the quantity 10000 sets the lock");
    lock(0, first, 0, 0);
    lock(0, first, 0, 1);
    check(own->bag.slots[0].item->flags == world::kItemLocked && host.store.list_items(kCharacterA)[0].flags == world::kItemLocked,
          "a lock request with the quantity 0 (the value of the client) clears and sets the lock");

    drop({{0, first}});
    check(own->bag.slots[0].item && own->bag.slots[0].item->quantity == 100 && host.store.list_items(kCharacterA).size() == 2
              && log_count("rep: drop request (type 1838, 1 entry): the item is locked") == 1,
          "a drop request of a client does not remove a stack that has the lock");
    drop({{1, second}, {0, first}});
    check(own->bag.slots[1].item->quantity == 50 && own->bag.slots[0].item->quantity == 100
              && log_count("rep: drop request (type 1838, 2 entries): the item is locked") == 1,
          "a drop request with one entry for a locked item is refused as a whole: the other stack stays too");
    drop({{1, second}});
    check(own->bag.slots[1].item->quantity == 40, "a drop request for the stack with no lock removes its quantity");
    lock(0, first, 0, 0);
    drop({{0, first}});
    check(own->bag.slots[0].item->quantity == 90, "after the unlock, a drop request removes a quantity of the stack");
    check(inventory.set_locked(kCharacterA, 0, nullptr, true).ok() && inventory.remove(kCharacterA, 0).ok() && !own->bag.slots[0].item,
          "a development command (remove) has no lock rule");
}

/** D3: a delete whose database write failed after the world removed the entity (review finding 3). */
void failed_delete(const Options& options) {
    using nlohmann::json;
    const std::filesystem::path path = options.workDirectory / "review-delete.sqlite3";
    std::filesystem::remove(path);
    ManualClock clock;
    state::Store store;
    const bool opened = store.open(path.string());
    const Player player = add_player(store, kSteamIdA, "Tester A", kCharacterA, "Alpha", std::nullopt);
    gamedata::AppearanceTables tables;
    gamedata::ItemCatalog items;
    items.set({ore()});
    world::WorldOptions worldOptions;
    worldOptions.itemCatalog = &items;
    world::World world(clock, store, tables, worldOptions);
    // This thread is the world thread: the task of the route runs in the call.
    world.tick();
    Sink sink;
    const bool entered = world.enter(1, sink, kCharacterA, player.account.personaId).error == world::EnterError::none;
    const bool given = world.inventory().give(kCharacterA, "TestOre", 10).ok();
    check(opened && entered && given
              && run_sql(path, "CREATE TRIGGER refuse_item_delete BEFORE DELETE ON items BEGIN SELECT RAISE(FAIL, 'test write failure'); END"),
          "a character in the world with one item, and a database that refuses the delete of an item row");
    check(store.delete_character("no-such-character") == state::DeleteResult::notFound,
          "the store gives 'not found' for a character id that has no row");
    server::http::Router router;
    const server::http::Services services{store, "127.0.0.1:32210", "test-version", "", tables, world};
    server::http::register_gateway_routes(router, services);
    const auto request_delete = [&] {
        service::HttpRequest request;
        request.method = "DELETE";
        request.host = "gateway.test";
        request.path = std::string("/prod/game/characters/") + kCharacterA;
        request.target = request.path;
        request.headers.emplace_back("x-nw-auth", encoding::local_jwt(json{{"personaId", player.account.personaId}}.dump()));
        std::string route;
        return router.dispatch(request, route);
    };
    service::HttpResponse reply = request_delete();
    note("failed delete: status " + std::to_string(reply.status) + ", body " + reply.body);
    check(reply.status == 500 && reply.body.find("did not complete") != std::string::npos && reply.body.find("not found") == std::string::npos,
          "the delete route gives the reply 500 'did not complete', not 404 'not found'");
    check(store.find_character(kCharacterA).has_value() && store.list_items(kCharacterA).size() == 1 && world.find_character(kCharacterA) == nullptr
              && sink.lost,
          "the rows of the character and of its item stay (one transaction), the entity is gone, and its session lost the character");
    check(log_count("gateway: the database did not accept the delete of character " + std::string(kCharacterA)
                    + ": its rows are there, and its entity is not in the world any more")
                  == 1
              && log_count("nothing deleted") == 0 && log_count("store: no delete of character") == 1,
          "the log says that the delete is not complete, and it does not say 'nothing deleted'");
    check(run_sql(path, "DROP TRIGGER refuse_item_delete") && request_delete().status == 200 && !store.find_character(kCharacterA)
              && store.list_items(kCharacterA).empty() && request_delete().status == 404,
          "when the database accepts the delete again, the same request deletes the character (reply 200), and a third "
          "request gets 404");
}

/** P3: a spawn near a character with an offset that is not a finite number (review finding 5). */
void near_offsets(const Options&) {
    ServerHost host;
    const Player player = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", state::WorldPosition{100.0F, 200.0F, 50.0F});
    add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", std::nullopt);
    FakeClient client(27000);
    host.enter(client, player.registration());
    server::http::Router router;
    const server::http::Services services{host.store, "127.0.0.1:32210", "test-version", "", host.tables, host.world};
    server::http::register_dev_routes(router, services);
    const auto spawn = [&](const std::string& offsets) {
        service::HttpRequest request;
        request.method = "POST";
        request.path = "/v1/dev/world/spawn";
        request.query = std::string("character=") + kCharacterB + "&near=" + kCharacterA + offsets;
        request.target = request.path + "?" + request.query;
        std::string route;
        return router.dispatch(request, route).status;
    };
    check(spawn("&dx=nan&dy=0&dz=0") == 400 && spawn("&dx=1&dy=inf") == 400 && spawn("&dz=-inf") == 400 && spawn("&dx=abc") == 400
              && spawn("&dx=") == 400 && spawn("&dx=1e39") == 400 && host.world.find_character(kCharacterB) == nullptr,
          "a spawn near a character with an offset that is not a finite number (nan, inf, a text, an empty value, a "
          "number that is too large) is refused, and the character does not come into the world");
    check(spawn("&dx=2") == 200 && host.world.find_character(kCharacterB) != nullptr
              && host.world.find_character(kCharacterB)->position.x == 102.0F && host.world.find_character(kCharacterB)->position.y == 200.0F,
          "an offset that the request does not have is 0: with dx=2 only, the entity is 2 m to the east");
}

/** B10: a change of the bag whose write fails (case RV5 of the review). */
void bag_rollback(const Options& options) {
    const std::filesystem::path path = options.workDirectory / "review-rollback.sqlite3";
    std::filesystem::remove(path);
    state::Store store;
    const bool opened = store.open(path.string());
    const Player player = add_player(store, kSteamIdA, "Tester A", kCharacterA, "Alpha", std::nullopt);
    ManualClock clock;
    gamedata::AppearanceTables tables;
    gamedata::ItemCatalog items;
    items.set({ore()});
    world::WorldOptions worldOptions;
    worldOptions.itemCatalog = &items;
    world::World world(clock, store, tables, worldOptions);
    Sink sink;
    const bool entered = world.enter(1, sink, kCharacterA, player.account.personaId).error == world::EnterError::none;
    const bool given = world.inventory().give(kCharacterA, "TestOre", 150).ok();
    check(opened && entered && given
              && run_sql(path, "CREATE TRIGGER refuse_item_insert BEFORE INSERT ON items WHEN NEW.slot=1 BEGIN SELECT RAISE(FAIL, 'test write "
                               "failure'); END"),
          "a character with two stacks, and a database that refuses the second insert of a move");
    const world::ItemResult result = world.inventory().move(kCharacterA, 0, 1);
    const std::vector<state::ItemRow> rows = store.list_items(kCharacterA);
    const world::Bag& bag = world.find_character(kCharacterA)->bag;
    check(result.error == world::ItemError::storeFailed && rows.size() == 2 && rows[0].quantity == 100 && rows[1].quantity == 50
              && bag.slots[0].item->quantity == 100 && bag.slots[1].item->quantity == 50,
          "a move whose write fails after the rows went out: the transaction puts the two rows back, and the entity "
          "keeps its two stacks");
}

/** B11: a database of each older schema version (case RV6 of the review). */
void older_schemas(const Options& options) {
    // The schema texts of the versions 1 to 4, as the backends of those versions made them. The one persona of an
    // old database has a constant id: the migration to version 3 must not depend on its text.
    const char* const steps[] = {
        "CREATE TABLE backend_metadata(key TEXT PRIMARY KEY, value TEXT NOT NULL);"
        "CREATE TABLE characters(character_id TEXT PRIMARY KEY, persona_id TEXT NOT NULL, world_id TEXT NOT NULL, name TEXT NOT NULL,"
        " created_date TEXT NOT NULL, modified_date TEXT NOT NULL, ftue_completed INTEGER NOT NULL DEFAULT 0,"
        " creation_params TEXT NOT NULL DEFAULT '', deleted_date TEXT);"
        "CREATE UNIQUE INDEX characters_active_name ON characters(name COLLATE NOCASE) WHERE deleted_date IS NULL;"
        "INSERT INTO characters(character_id, persona_id, world_id, name, created_date, modified_date)"
        " VALUES('migration-canary', 'old-persona-0001', 'test-world', 'MigrateCanary', '2026-01-01', '2026-01-01');",
        "ALTER TABLE characters ADD COLUMN position_x REAL; ALTER TABLE characters ADD COLUMN position_y REAL;"
        "ALTER TABLE characters ADD COLUMN position_z REAL;",
        "CREATE TABLE accounts(persona_id TEXT PRIMARY KEY, account_id TEXT NOT NULL UNIQUE, steam_id TEXT UNIQUE,"
        " persona_name TEXT NOT NULL DEFAULT '', created_date TEXT NOT NULL, last_login_date TEXT NOT NULL DEFAULT '');"
        "INSERT INTO accounts(persona_id, account_id, created_date) VALUES('old-persona-0001', 'old-account-0001', '2026-01-01');"
        "CREATE INDEX characters_persona ON characters(persona_id);",
        "ALTER TABLE characters ADD COLUMN archetype TEXT NOT NULL DEFAULT '';",
        "CREATE TABLE items(instance_id TEXT PRIMARY KEY, character_id TEXT NOT NULL, container INTEGER NOT NULL,"
        " slot INTEGER NOT NULL, item_id TEXT NOT NULL, quantity INTEGER NOT NULL, gear_score INTEGER NOT NULL DEFAULT 0,"
        " durability INTEGER NOT NULL DEFAULT 0, max_durability INTEGER NOT NULL DEFAULT 0, flags INTEGER NOT NULL DEFAULT 0,"
        " chrono INTEGER NOT NULL DEFAULT 0);"
        "CREATE UNIQUE INDEX items_place ON items(character_id, container, slot);"
        "CREATE TABLE currencies(character_id TEXT NOT NULL, currency_id TEXT NOT NULL, amount INTEGER NOT NULL,"
        " PRIMARY KEY(character_id, currency_id));"
        // An item of a database of version 5 or 6 has no perk column: after the migration it has no perk.
        "INSERT INTO items(instance_id, character_id, container, slot, item_id, quantity)"
        " VALUES('0f0e0d0c0b0a09080706050403020100', 'migration-canary', 1, 0, 'TestOre', 3);",
        "CREATE TABLE character_progress(character_id TEXT PRIMARY KEY, level INTEGER NOT NULL DEFAULT 1,"
        " experience INTEGER NOT NULL DEFAULT 0, constitution INTEGER NOT NULL DEFAULT 0, focus INTEGER NOT NULL DEFAULT 0,"
        " strength INTEGER NOT NULL DEFAULT 0, dexterity INTEGER NOT NULL DEFAULT 0, intelligence INTEGER NOT NULL DEFAULT 0);"
        "CREATE TABLE progressions(character_id TEXT NOT NULL, progression_id INTEGER NOT NULL, name TEXT NOT NULL DEFAULT '',"
        " rank INTEGER NOT NULL DEFAULT 0, points INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(character_id, progression_id));"
        "CREATE TABLE ability_nodes(character_id TEXT NOT NULL, pool_id INTEGER NOT NULL, node_id INTEGER NOT NULL,"
        " name TEXT NOT NULL DEFAULT '', level INTEGER NOT NULL DEFAULT 1, PRIMARY KEY(character_id, pool_id, node_id));"
        "CREATE TABLE ability_bars(character_id TEXT NOT NULL, table_id INTEGER NOT NULL, slot INTEGER NOT NULL,"
        " ability_id INTEGER NOT NULL, name TEXT NOT NULL DEFAULT '', PRIMARY KEY(character_id, table_id, slot));",
        "ALTER TABLE items ADD COLUMN perk1 TEXT NOT NULL DEFAULT ''; ALTER TABLE items ADD COLUMN perk2 TEXT NOT NULL DEFAULT '';"
        "ALTER TABLE items ADD COLUMN perk3 TEXT NOT NULL DEFAULT ''; ALTER TABLE items ADD COLUMN perk4 TEXT NOT NULL DEFAULT '';"
        "ALTER TABLE items ADD COLUMN perk5 TEXT NOT NULL DEFAULT '';",
    };
    for (int version = 0; version <= 7; ++version) {
        const std::filesystem::path path = options.workDirectory / ("review-v" + std::to_string(version) + ".sqlite3");
        std::filesystem::remove(path);
        std::string sql;
        for (int step = 0; step < version; ++step) {
            sql += steps[step];
        }
        sql += "PRAGMA user_version=" + std::to_string(version) + ";";
        const bool made = run_sql(path, sql) && schema_version(path) == version;
        state::Store store;
        const bool opened = store.open(path.string());
        const auto canary = store.find_character("migration-canary");
        const bool kept = version == 0 || (canary && canary->name == "MigrateCanary" && canary->personaId == "old-persona-0001");
        // A database from before version 3 has no account: the migration makes the account of its persona.
        const bool owned = version == 0 || store.find_account("old-persona-0001").has_value();
        const Player player = add_player(store, kSteamIdA, "Tester A", kCharacterA, "Alpha", std::nullopt);
        state::ItemRow worn{"00112233445566778899aabbccddeeff", state::kContainerPaperdoll, 1, "TestChest", 1};
        // Schema version 7 (stage 1.3b): the five perk places of an instance.
        worn.perks = {"PerkID_Test_A", "", "PerkID_Test_Gem", "", "PerkID_Test_B"};
        state::ItemRow carried{"00112233445566778899aabbccddee00", state::kContainerBag, 0, "TestOre", 10};
        const std::vector<state::ItemRow> back = store.write_items(kCharacterA, {}, {worn, carried}) ? store.list_items(kCharacterA)
                                                                                                     : std::vector<state::ItemRow>{};
        const bool written = store.set_currency(kCharacterA, state::kCurrencyCoin, 12345) && back.size() == 2
                             && back[1].container == state::kContainerPaperdoll && back[1].perks == worn.perks
                             && back[0].perks == state::ItemRow{}.perks;
        // The item of the old database: no perk.
        const std::vector<state::ItemRow> old = store.list_items("migration-canary");
        const bool oldItem = version < 5 || (old.size() == 1 && old[0].quantity == 3 && old[0].perks == state::ItemRow{}.perks);
        // The tables of schema version 6: the level and the attributes, and the skills.
        state::ProgressRow progress;
        progress.level = 7;
        progress.experience = 2000;
        progress.spent = {1, 0, 2, 0, 0};
        state::SkillRows skills;
        skills.ranks.push_back({0x11111111, "TestWeapon", 3, 40});
        skills.nodes.push_back({0x11111111, 0x22222222, "TestNode", 1});
        skills.bars.push_back({0x11111111, 0, 0x22222222, "TestNode"});
        const state::SkillRows read = store.write_skills(kCharacterA, skills) ? store.list_skills(kCharacterA) : state::SkillRows{};
        const bool progressed = store.set_progress(kCharacterA, progress) && store.progress(kCharacterA).level == 7
                                && store.progress(kCharacterA).spent[2] == 2 && read.ranks.size() == 1 && read.nodes.size() == 1
                                && read.bars.size() == 1 && read.ranks[0].points == 40 && read.bars[0].ability == 0x22222222;
        // The tables of schema version 8 (stage 1.1b): the entitlements of the account, the achievements and the rewards of
        // the character.
        std::vector<std::string> added;
        const bool owns = store.grant_entitlements(player.account.personaId, {"Test_Entitlement"}, "test", &added) && added.size() == 1
                          && store.grant_achievements(kCharacterA, {"Test_Achievement"}) && store.record_entitlement_reward(kCharacterA, "Test_Entitlement")
                          && store.list_entitlements(player.account.personaId).size() == 1 && store.list_achievements(kCharacterA).size() == 1;
        // The tables of schema version 9 (stage 3.5b.1): a loot claim of the character with a kept item.
        state::LootClaimRow claim;
        claim.objectKey = "0b1ec700-0000-4000-8000-000000000001";
        claim.state = 1;
        claim.generation = 1;
        claim.items.push_back(state::ClaimItemRow{"TestSword", 1, 210, {"PerkA", "", "", "", ""}});
        const bool claimed = store.write_loot_claim(kCharacterA, claim) && store.list_loot_claims(kCharacterA).size() == 1
                             && store.list_loot_claims(kCharacterA)[0].items.size() == 1
                             && store.list_loot_claims(kCharacterA)[0].items[0].perks[0] == "PerkA";
        // The tables of schema version 10 (stage 3.5b.2): the part of a loot bag of the character with one stored instance.
        state::LootBagRow bag;
        bag.bagKey = "ba900000-0000-4000-8000-000000000001";
        bag.kind = 1;
        bag.source = "Test_Wolf";
        bag.position = state::WorldPosition{1.0F, 2.0F, 3.0F};
        bag.expiresAt = 180000;
        // Schema version 12 (stage 2.2, findings LB-R8 and LB-R9 of the code review of stage 3.5b.2): the fast end of an empty bag.
        bag.emptiedAt = 1250;
        bag.items.push_back(state::ItemRow{"00112233445566778899aabbccddee11", 0, 0, "TestSword", 1, 250, 900, 1000, 1, 1,
                                           {"PerkA", "", "", "", ""}});
        const auto bags = store.write_loot_bag(kCharacterA, bag) ? store.list_loot_bags() : std::vector<std::pair<std::string, state::LootBagRow>>{};
        const bool bagged = bags.size() == 1 && bags[0].first == kCharacterA && bags[0].second.items.size() == 1
                            && bags[0].second.items[0].gearScore == 250 && bags[0].second.items[0].durability == 900
                            && bags[0].second.items[0].flags == 1 && bags[0].second.items[0].perks[0] == "PerkA"
                            && bags[0].second.emptiedAt == 1250;
        // The table of schema version 11 (stage 2.2): the position of the day cycle of a world (no character owns it).
        const bool timed = !store.world_time("test_world") && store.set_world_time("test_world", 1234567) && store.world_time("test_world") == 1234567ULL;
        // Schema version 13 (stage 3.6a): the run clock of a world and the depleted gathering nodes.
        state::GatheringNodeRow node;
        node.key = "r_+00_+00#1";
        node.source = "distribution:test";
        node.row = "Test_OreSmall";
        node.generation = 1;
        node.respawnRunNs = 5000;
        const bool gathered = store.set_world_time("test_world", 7654321, 42ULL) && store.world_run_time("test_world") == 42ULL
                              && store.write_gathering_node(node) && store.list_gathering_nodes().size() == 1
                              && store.list_gathering_nodes()[0].respawnRunNs == 5000 && store.delete_gathering_node(node.key)
                              && store.list_gathering_nodes().empty();
        // Schema version 14 (stage 3.7b): the cooldown of a recipe of a character (the delete of the character removes it).
        const bool cooled = store.write_craft_cooldown(kCharacterA, state::CraftCooldownRow{"TestRecipe", 3, 9000})
                            && store.list_craft_cooldowns(kCharacterA).size() == 1 && store.list_craft_cooldowns(kCharacterA)[0].used == 3
                            && store.list_craft_cooldowns(kCharacterA)[0].endRunNs == 9000;
        const bool deleted = store.delete_character(kCharacterA) == state::DeleteResult::deleted && store.list_items(kCharacterA).empty()
                             && store.list_loot_claims(kCharacterA).empty() && store.list_loot_bags().empty()
                             && store.currency(kCharacterA, state::kCurrencyCoin) == 0 && store.progress(kCharacterA).level == 1
                             && store.list_skills(kCharacterA).ranks.empty() && store.list_skills(kCharacterA).nodes.empty()
                             && store.list_skills(kCharacterA).bars.empty() && store.list_achievements(kCharacterA).empty()
                             && store.list_entitlement_rewards(kCharacterA).empty()
                             && store.list_entitlements(player.account.personaId).size() == 1
                             && store.list_craft_cooldowns(kCharacterA).empty();
        check(made && opened && schema_version(path) == 14 && kept && owned && written && oldItem && progressed && owns && claimed && bagged
                  && timed && gathered && cooled && deleted,
              "a database of schema version " + std::to_string(version)
                  + " opens and gets version 14: its character row and its item stay (the item with no perk), its persona has an account, "
                    "the tables take an item of the bag, an item of the paperdoll with its five perk places, the coin, the level with the "
                    "attribute points, the skills, an entitlement of the account, an achievement and a reward of the character, a loot "
                    "claim with a kept item (stage 3.5b.1), the part of a loot bag with a stored instance (stage 3.5b.2) and its fast end, the world time (stage 2.2), "
                    "the run clock and a depleted gathering node (stage 3.6a), the cooldown of a recipe (stage 3.7b), and the delete of a "
                    "character removes the rows of the character (the entitlement of the account stays)");
    }
}

} // namespace

std::vector<Case> review_inventory_cases() {
    return {
        {"L1", "a drop request does not remove a locked item, and a lock request has the quantity limit (review findings 1 and 4 of stage 1.3)", locked_items},
        {"D3", "a character delete whose database write failed is not 'character not found' (review finding 3)", failed_delete},
        {"P3", "a spawn near a character refuses an offset that is not a finite number (review finding 5)", near_offsets},
        {"B10", "a change of the bag whose write fails changes no row and no entity (review case RV5)", bag_rollback},
        {"B11", "a database of each older schema version gets the tables of the items (review case RV6)", older_schemas},
    };
}

} // namespace soulwarden::backend::tests

// Cases for the admin interface of the tool window (stage 1.7): who can use it, the state of
// the characters of an account, each command with a correct and with a wrong value, two
// accounts with their own tokens, the token in the log and in the capture files, and the item
// search. The routes are the routes of the backend program with no development option: the
// test program has no harness gate.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/capture/http_capture.h"
#include "server/http/admin_access.h"
#include "server/http/request_handler.h"
#include "server/http/router.h"
#include "server/http/routes.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/server_host.h"
#include "support/state_decoder.h"
#include "support/test_players.h"
#include "support/test_progression.h"

namespace soulwarden::backend::tests {
namespace {

using nlohmann::json;

constexpr const char* kCharacterA = "aaaaaaaa-0000-4000-8000-00000000000a";
constexpr const char* kCharacterB = "bbbbbbbb-0000-4000-8000-00000000000b";
/** A second character of the account of A. It is not in the world. */
constexpr const char* kCharacterA2 = "a2a2a2a2-0000-4000-8000-0000000000a2";
constexpr const char* kCharacterNone = "dddddddd-0000-4000-8000-00000000000d";
constexpr float kHeight = 50.0F;
/** Made-up tokens: 40 characters each. */
constexpr const char* kTokenA = "token-of-the-start-of-A-0123456789abcdef";
constexpr const char* kTokenB = "token-of-the-start-of-B-fedcba9876543210";
constexpr const char* kTokenWrong = "token-that-no-start-has-0000000000000000";

gamedata::ItemDefinition item(const char* id, const char* type, const char* itemClass, std::uint32_t maxStack) {
    gamedata::ItemDefinition definition;
    definition.id = id;
    definition.nameKey = std::string("@") + id + "_MasterName";
    definition.type = type;
    definition.itemClass = itemClass;
    definition.maxStack = maxStack;
    definition.tier = 1;
    return definition;
}

/** A backend with two accounts, their tokens, and the admin routes: no development route. */
struct AdminHost {
    ServerHost host;
    Player alpha;
    Player beta;
    server::http::AdminAccess access;
    server::http::Services services{host.store, "127.0.0.1:32210", "test-version", "", host.tables, host.world};
    server::http::Router router;

    AdminHost()
        : alpha(add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", state::WorldPosition{100.0F, 200.0F, kHeight})),
          beta(add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", state::WorldPosition{102.0F, 200.0F, kHeight})) {
        host.catalog.set({item("TestSword", "Weapon", "Weapon+Sword", 1), item("TestOre", "Resource", "Resource+Ore", 100),
                          item("TestPotion", "Consumable", "Consumable+Potion", 10), item("TestNoType", "", "", 1),
                          item("Test Ore Large", "Resource", "Resource+Ore", 100)});
        set_progression(host);
        state::Character second;
        second.characterId = kCharacterA2;
        second.personaId = alpha.account.personaId;
        second.worldId = kWorldId;
        second.name = "Alphatwo";
        second.createdDate = "2026-01-02T00:00:00.000Z";
        second.modifiedDate = second.createdDate;
        if (host.store.insert_character(second) != state::InsertResult::inserted || !access.add(kTokenA, kSteamIdA)
            || !access.add(kTokenB, kSteamIdB)) {
            throw std::runtime_error("admin test: the setup failed");
        }
        server::http::register_admin_routes(router, services, access);
        // The thread of the test is the world thread from its first tick on: a route gives the
        // world a task, and the task then runs at once.
        host.tick();
    }

    /** One request through the handler of the backend program (the log line and the capture file). */
    service::HttpResponse send(const char* method,
                               const std::string& path,
                               const std::string& query,
                               const std::string& body,
                               const char* token,
                               const std::string& serviceHost = {},
                               const std::string& origin = {}) {
        service::HttpRequest request;
        request.method = method;
        request.path = path;
        request.query = query;
        request.target = query.empty() ? path : path + "?" + query;
        request.host = serviceHost;
        request.body = body;
        if (token != nullptr) {
            request.headers.emplace_back(server::http::kAdminTokenHeader, token);
        }
        if (!origin.empty()) {
            request.headers.emplace_back("Origin", origin);
        }
        return server::http::handle_request(router, request);
    }

    service::HttpResponse state(const char* token, const std::string& query = {}) {
        return send("GET", "/admin/v1/state", query, {}, token);
    }

    service::HttpResponse command(const char* token, const char* name, const char* character, const json& arguments) {
        return send("POST", "/admin/v1/commands", {}, json{{"command", name}, {"character", character}, {"args", arguments}}.dump(), token);
    }
};

/** @return The rows of the items and the values of a character as one text, for a compare. */
std::string rows_of(state::Store& store, const char* character) {
    std::string text;
    for (const state::ItemRow& row : store.list_items(character)) {
        text += std::to_string(row.slot) + ":" + row.itemId + ":" + std::to_string(row.quantity) + " ";
    }
    const state::ProgressRow progress = store.progress(character);
    text += "| coin " + std::to_string(store.currency(character, state::kCurrencyCoin)) + " level " + std::to_string(progress.level)
            + " experience " + std::to_string(progress.experience) + " spent";
    for (const std::uint32_t points : progress.spent) {
        text += " " + std::to_string(points);
    }
    text += " |";
    for (const state::SkillRows::Rank& rank : store.list_skills(character).ranks) {
        text += " " + rank.name + ":" + std::to_string(rank.rank);
    }
    return text;
}

/** X1: who can use the admin interface. */
void access_rules(const Options&) {
    AdminHost admin;
    const std::string beforeA = rows_of(admin.host.store, kCharacterA);
    const json give = {{"item", "TestOre"}, {"quantity", 5}};

    server::http::AdminAccess none;
    server::http::Router empty;
    service::HttpRequest probe;
    probe.method = "GET";
    probe.path = "/admin/v1/state";
    probe.target = probe.path;
    std::string route;
    check(!none.enabled() && !none.add("short-token", kSteamIdA) && !none.add(kTokenA, "not-a-steam-id") && !none.enabled()
              && empty.dispatch(probe, route).status == 404 && admin.access.enabled(),
          "with no token the backend has no admin route (reply 404); a token of fewer than 32 characters and a token for "
          "a text that is not a Steam id are not accepted");

    service::HttpResponse reply = admin.state(nullptr);
    const bool noToken = reply.status == 401 && reply.body.find("characters") == std::string::npos
                         && reply.body.find("Alpha") == std::string::npos;
    reply = admin.state(kTokenWrong);
    const bool wrongToken = reply.status == 401 && reply.body.find("Alpha") == std::string::npos;
    check(noToken && wrongToken && admin.state("").status == 401
              && admin.send("GET", "/admin/v1/state", std::string("token=") + kTokenA, {}, nullptr).status == 401
              && admin.send("GET", "/admin/v1/catalog/items", "q=ore", {}, nullptr).status == 401
              && admin.send("GET", "/admin/v1/catalog/items", "q=ore", {}, kTokenWrong).status == 401,
          "a read with no token, with an empty token, with a wrong token, and with the token in the query and not in "
          "the header gets the reply 401 and no data");
    check(admin.command(nullptr, "give_item", kCharacterA, give).status == 401
              && admin.command(kTokenWrong, "give_item", kCharacterA, give).status == 401
              && admin.command(nullptr, "set_level", kCharacterA, {{"level", 5}}).status == 401
              && admin.command(kTokenWrong, "set_coin", kCharacterA, {{"amount", 500}}).status == 401
              && rows_of(admin.host.store, kCharacterA) == beforeA,
          "a command with no token and with a wrong token gets the reply 401, and no row of the character changes");
    check(admin.send("GET", "/admin/v1/state", {}, {}, kTokenA, "gateway.test").status == 403
              && admin.send("POST", "/admin/v1/commands", {}, json{{"command", "set_level"}, {"character", kCharacterA}, {"args", {{"level", 5}}}}.dump(),
                            kTokenA, "gateway.test")
                         .status == 403
              && admin.send("GET", "/admin/v1/state", {}, {}, kTokenA, {}, "http://page.test").status == 403
              && admin.send("GET", "/admin/v1/catalog/items", "q=ore", {}, kTokenA, "gateway.test").status == 403
              && rows_of(admin.host.store, kCharacterA) == beforeA,
          "a request with the correct token and a service host (a request that the hook redirects for the game), or with "
          "an Origin header (a page of a browser), gets the reply 403 and changes nothing");
    reply = admin.state(kTokenA);
    check(std::getenv("SOULWARDEN_HARNESS") == nullptr && reply.status == 200 && json::parse(reply.body)["account"] == true
              && admin.command(kTokenA, "give_item", kCharacterA, give).status == 200
              && rows_of(admin.host.store, kCharacterA) != beforeA,
          "with the correct token the routes work in a process with no harness gate (the test program has no "
          "SOULWARDEN_HARNESS), with no development route in the router");
}

/** X2: the state of the characters of the account of a token. */
void state_view(const Options&) {
    AdminHost admin;
    json reply = json::parse(admin.state(kTokenA).body);
    check(reply["account"] == true && reply["catalog"] == true && reply["characters"].size() == 2
              && reply["characters"][0]["id"] == kCharacterA && reply["characters"][0]["name"] == "Alpha"
              && reply["characters"][0]["inWorld"] == false && reply["characters"][0]["level"] == 1
              && reply["characters"][1]["id"] == kCharacterA2 && reply["selected"] == kCharacterA
              && reply.dump().find("Beta") == std::string::npos && reply.dump().find(kCharacterB) == std::string::npos,
          "the state of the token of A: the two characters of the account of A with their names and levels, the first "
          "one selected, and nothing of the character of B");
    FakeClient a(27000);
    admin.host.enter(a, admin.alpha.registration());
    admin.host.world.inventory().give(kCharacterA, "TestOre", 30);
    admin.host.world.inventory().set_coin(kCharacterA, 12345);
    admin.host.world.progress().set_level(kCharacterA, 3);
    admin.host.world.skills().set_rank(kCharacterA, "SwordAbilityTable", gamedata::ProgressionKind::weapon, 4);
    reply = json::parse(admin.state(kTokenA).body);
    const json& items = reply["items"];
    const json& progress = reply["progress"];
    json sword;
    json mining;
    for (const json& weapon : progress["weapons"]) {
        if (weapon["id"] == "SwordAbilityTable") {
            sword = weapon;
        }
    }
    for (const json& skill : progress["tradeSkills"]) {
        if (skill["id"] == "Mining") {
            mining = skill;
        }
    }
    check(reply["selected"] == kCharacterA && reply["characters"][0]["inWorld"] == true && reply["characters"][0]["session"] == 1
              && reply["characters"][0]["level"] == 3 && items["coin"] == 12345 && items["items"].size() == 1
              && items["items"][0]["item"] == "TestOre" && items["items"][0]["quantity"] == 30 && items["items"][0]["slot"] == 0
              && items.contains("equipment") && progress["level"] == 3 && progress["experience"] == 300
              && progress["nextLevelExperience"] == 600 && progress["freeAttributePoints"] == 10
              && progress["attributes"]["strength"]["value"] == 5 && progress["healthMax"].get<int>() > 0
              && sword["level"] == 5 && sword["freePoints"] == 4 && mining["level"] == 0 && mining["maxLevel"] == 5,
          "the state of a character that a session plays: the session, the level, the experience and the limit of the "
          "next level, the attributes, the free points, the health, the coin, the bag, the equipment, each weapon mastery "
          "with its level and its points, and each trade skill");
    reply = json::parse(admin.state(kTokenA, std::string("character=") + kCharacterA2).body);
    check(reply["selected"] == kCharacterA2 && reply["items"]["inWorld"] == false && reply["items"]["items"].empty()
              && reply["progress"]["level"] == 1,
          "a request names a character of the account that is not in the world: the state of its rows");
    check(admin.state(kTokenA, std::string("character=") + kCharacterB).status == 404
              && admin.state(kTokenA, std::string("character=") + kCharacterNone).status == 404
              && admin.state(kTokenA, std::string("character=") + kCharacterB).body.find("Beta") == std::string::npos,
          "the state of a character of a different account, and of a character that is not there: the same reply 404, "
          "with no data");
    reply = json::parse(admin.state(kTokenB).body);
    check(reply["characters"].size() == 1 && reply["characters"][0]["id"] == kCharacterB && reply["selected"] == kCharacterB
              && reply["items"]["coin"] == 0 && reply.dump().find("Alpha") == std::string::npos,
          "the token of B gives only the character of B");
    server::http::AdminAccess other;
    const bool added = other.add(kTokenWrong, "76561202255233010");
    server::http::Router router;
    server::http::register_admin_routes(router, admin.services, other);
    service::HttpRequest request;
    request.method = "GET";
    request.path = "/admin/v1/state";
    request.target = request.path;
    request.headers.emplace_back("x-soulwarden-admin", kTokenWrong);
    std::string route;
    const service::HttpResponse first = router.dispatch(request, route);
    check(added && first.status == 200 && json::parse(first.body)["account"] == false && json::parse(first.body)["characters"].empty()
              && !json::parse(first.body).contains("selected"),
          "a token for a Steam id that has no account yet (no login of this start): the reply 200 with no character; the "
          "header name is read with no case");
}

/** X3: each command with a correct and with a wrong value. */
void commands_each(const Options&) {
    AdminHost admin;
    state::Store& store = admin.host.store;
    FakeClient a(27000);
    admin.host.enter(a, admin.alpha.registration());
    const auto ok = [&](const char* name, const json& arguments, const char* character = kCharacterA) {
        return admin.command(kTokenA, name, character, arguments);
    };
    const auto refused = [&](const char* name, const json& arguments) {
        const std::string before = rows_of(store, kCharacterA);
        const service::HttpResponse reply = admin.command(kTokenA, name, kCharacterA, arguments);
        return reply.status == 400 && json::parse(reply.body).contains("message") && rows_of(store, kCharacterA) == before;
    };

    service::HttpResponse reply = ok("give_item", {{"item", "TestOre"}, {"quantity", 150}});
    check(reply.status == 200 && json::parse(reply.body)["applied"] == "world" && json::parse(reply.body)["slots"] == json::array({0, 1})
              && ok("give_item", {{"item", "TestSword"}}).status == 200
              && ok("give_item", {{"item", "TestPotion"}, {"quantity", "3"}}, kCharacterA2).status == 200
              && rows_of(store, kCharacterA).rfind("0:TestOre:100 1:TestOre:50 2:TestSword:1 |", 0) == 0
              && rows_of(store, kCharacterA2).rfind("0:TestPotion:3 |", 0) == 0,
          "give_item: a quantity that needs two stacks (the reply has the slots and \"applied\":\"world\"), an item with "
          "no quantity, and a quantity as a text for a character that is not in the world");
    check(refused("give_item", {{"item", "NoSuchItem"}}) && refused("give_item", {{"item", "TestOre"}, {"quantity", 0}})
              && refused("give_item", {{"item", "TestOre"}, {"quantity", -5}}) && refused("give_item", {{"item", "TestOre"}, {"quantity", 1.5}})
              && refused("give_item", {{"item", "TestOre"}, {"quantity", 100001}}) && refused("give_item", {{"quantity", 5}})
              && refused("give_item", {{"item", "TestNoType"}}) && refused("give_item", {{"item", "TestOre"}, {"gearScore", 70000}}),
          "give_item refuses an item that the catalog does not have, an item with no type, a quantity that is not a whole "
          "number from 1 to 100000, a request with no item, and a gear score above 65535");
    check(ok("remove_item", {{"slot", 1}, {"quantity", 20}}).status == 200 && ok("remove_item", {{"slot", 2}}).status == 200
              && rows_of(store, kCharacterA).rfind("0:TestOre:100 1:TestOre:30 |", 0) == 0 && refused("remove_item", {{"slot", 7}})
              && refused("remove_item", {{"slot", 500}}) && refused("remove_item", {{"slot", 1}, {"quantity", 31}})
              && refused("remove_item", json::object()),
          "remove_item: a quantity of a stack, and a full stack; it refuses an empty slot, a slot that is not a slot of "
          "the bag, a quantity that is larger than the stack, and a request with no slot");
    check(ok("set_coin", {{"amount", 123456}}).status == 200 && store.currency(kCharacterA, state::kCurrencyCoin) == 123456
              && refused("set_coin", {{"amount", 100000001}}) && refused("set_coin", {{"amount", 1.5}})
              && refused("set_coin", {{"amount", "many"}}) && refused("set_coin", json::object()),
          "set_coin: an amount in hundredths of a coin; it refuses an amount above the wallet limit, a number that is not "
          "whole, a text, and a request with no amount");
    reply = ok("set_level", {{"level", 3}});
    check(reply.status == 200 && json::parse(reply.body)["level"] == 3 && json::parse(reply.body)["freeAttributePoints"] == 10
              && store.progress(kCharacterA).level == 3 && store.progress(kCharacterA).experience == 300
              && refused("set_level", {{"level", 0}}) && refused("set_level", {{"level", 7}}) && refused("set_level", {{"level", "x"}}),
          "set_level: the level of the screen, with the experience at the start of the level; it refuses the level 0, a "
          "level above the largest level of the tables, and a text");
    reply = ok("add_experience", {{"amount", 350}});
    check(reply.status == 200 && json::parse(reply.body)["level"] == 4 && json::parse(reply.body)["levelsGained"] == 1
              && store.progress(kCharacterA).experience == 650 && refused("add_experience", {{"amount", 0}})
              && refused("add_experience", {{"amount", 10000001}}),
          "add_experience: the level goes up at the limit of a level; it refuses the amount 0 and an amount above the limit");
    reply = ok("set_attribute", {{"attribute", "strength"}, {"value", 9}});
    check(reply.status == 200 && json::parse(reply.body)["freeAttributePoints"] == 12
              && store.progress(kCharacterA).spent[world::attribute::kStrength] == 4
              && refused("set_attribute", {{"attribute", "luck"}, {"value", 9}})
              && refused("set_attribute", {{"attribute", "strength"}, {"value", 4}})
              && refused("set_attribute", {{"attribute", "focus"}, {"value", 30}}) && refused("set_attribute", {{"attribute", "focus"}}),
          "set_attribute: the value of one attribute, from the free points; it refuses a name that is not an attribute, a "
          "value below the minimum, a value that needs more points than the character has, and a request with no value");
    reply = ok("set_weapon_mastery", {{"weapon", "sword"}, {"level", 5}});
    check(reply.status == 200 && json::parse(reply.body)["rank"] == 4 && json::parse(reply.body)["freePoints"] == 4
              && ok("set_weapon_mastery", {{"weapon", "BowAbilityTable"}, {"level", 2}, {"experience", 5}}).status == 200
              && rows_of(store, kCharacterA).find("SwordAbilityTable:4") != std::string::npos
              && rows_of(store, kCharacterA).find("BowAbilityTable:1") != std::string::npos
              && refused("set_weapon_mastery", {{"weapon", "sword"}, {"level", 0}})
              && refused("set_weapon_mastery", {{"weapon", "sword"}, {"level", 16}})
              && refused("set_weapon_mastery", {{"weapon", "club"}, {"level", 3}})
              && refused("set_weapon_mastery", {{"weapon", "Mining"}, {"level", 3}})
              && refused("set_weapon_mastery", {{"weapon", "sword"}, {"level", 3}, {"experience", 999}}),
          "set_weapon_mastery: level 5 of the screen is rank 4 with 4 mastery points (one for each level above 1), by the "
          "short name and by the id of the game table; it refuses the level 0, a level above the largest one, a weapon "
          "that is not known, a trade skill, and experience that is not below the limit of the level");
    reply = ok("set_trade_skill", {{"skill", "Mining"}, {"level", 3}});
    check(reply.status == 200 && json::parse(reply.body)["rank"] == 3 && rows_of(store, kCharacterA).find("Mining:3") != std::string::npos
              && refused("set_trade_skill", {{"skill", "Mining"}, {"level", 6}}) && refused("set_trade_skill", {{"skill", "Juggling"}, {"level", 1}})
              && refused("set_trade_skill", {{"skill", "SwordAbilityTable"}, {"level", 1}}) && refused("set_trade_skill", {{"skill", "Mining"}}),
          "set_trade_skill: the level of a trade skill; it refuses a level above the largest one, a skill that is not "
          "known, a weapon, and a request with no level");
    const std::string before = rows_of(store, kCharacterA);
    const auto raw = [&](const std::string& body) { return admin.send("POST", "/admin/v1/commands", {}, body, kTokenA).status; };
    check(refused("fly", {{"height", 100}}) && raw("not json") == 400 && raw("[1,2]") == 400
              && raw(json{{"command", "set_level"}, {"args", {{"level", 2}}}}.dump()) == 400
              && raw(json{{"command", "set_level"}, {"character", ""}, {"args", {{"level", 2}}}}.dump()) == 400
              && raw(json{{"command", "set_level"}, {"character", kCharacterA}, {"args", "level=2"}}.dump()) == 400
              && raw(json{{"command", 7}, {"character", kCharacterA}}.dump()) == 400 && rows_of(store, kCharacterA) == before
              && admin.command(kTokenA, "set_level", kCharacterNone, {{"level", 2}}).status == 404,
          "a command name that the interface does not have, a body that is not a JSON object, a body with no character "
          "or with arguments that are not an object: the reply 400 and no change; a character that is not there: the "
          "reply 404");
    check(log_count("admin: command give_item for character aaaaaaaa: reply 200") >= 1
              && log_count("admin: command set_level for character aaaaaaaa: reply 400") >= 1
              && log_count("http: ok POST /admin/v1/commands -> 200 [admin.commands]") >= 1,
          "each command has a log line with its name, the first characters of the character id, and the status of its reply");
}

/** X4: two accounts with their own tokens, and two sessions. */
void two_accounts(const Options&) {
    AdminHost admin;
    state::Store& store = admin.host.store;
    FakeClient a(27000);
    FakeClient b(27001);
    admin.host.enter(a, admin.alpha.registration());
    admin.host.enter(b, admin.beta.registration());
    admin.host.steps(2, {&a, &b});
    const std::string beforeA = rows_of(store, kCharacterA);
    const std::string beforeB = rows_of(store, kCharacterB);
    const json give = {{"item", "TestOre"}, {"quantity", 5}};

    check(admin.command(kTokenA, "give_item", kCharacterB, give).status == 404
              && admin.command(kTokenA, "set_level", kCharacterB, {{"level", 5}}).status == 404
              && admin.command(kTokenA, "set_coin", kCharacterB, {{"amount", 999}}).status == 404
              && admin.command(kTokenA, "set_weapon_mastery", kCharacterB, {{"weapon", "sword"}, {"level", 5}}).status == 404
              && admin.command(kTokenB, "remove_item", kCharacterA, {{"slot", 0}}).status == 404
              && admin.command(kTokenB, "set_trade_skill", kCharacterA2, {{"skill", "Mining"}, {"level", 2}}).status == 404
              && rows_of(store, kCharacterA) == beforeA && rows_of(store, kCharacterB) == beforeB
              && log_count("admin: command give_item refused: the character is not a character of the account of this start") == 1,
          "a command of the token of A for the character of B, and of the token of B for a character of A: the reply "
          "404, and no row of a character changes");

    std::size_t fromA = a.got.size();
    std::size_t fromB = b.got.size();
    const bool levelA = admin.command(kTokenA, "set_level", kCharacterA, {{"level", 4}}).status == 200;
    admin.host.steps(2, {&a, &b});
    const std::size_t toA = slot_updates(a, kProgressionSlot, fromA).fragments.size();
    const std::size_t toB = slot_updates(b, kProgressionSlot, fromB).fragments.size();
    check(levelA && toA == 1 && toB == 0 && store.progress(kCharacterA).level == 4 && rows_of(store, kCharacterB) == beforeB,
          "two sessions: a level command of the token of A changes the character of A, the session of A gets its "
          "progression state, and the session of B gets nothing and keeps its values");
    fromA = a.got.size();
    fromB = b.got.size();
    const bool commandsB = admin.command(kTokenB, "set_level", kCharacterB, {{"level", 2}}).status == 200
                           && admin.command(kTokenB, "give_item", kCharacterB, give).status == 200
                           && admin.command(kTokenB, "set_coin", kCharacterB, {{"amount", 777}}).status == 200;
    admin.host.steps(2, {&a, &b});
    check(commandsB && slot_updates(b, kProgressionSlot, fromB).fragments.size() == 1
              && slot_updates(a, kProgressionSlot, fromA).fragments.empty() && store.progress(kCharacterB).level == 2
              && store.currency(kCharacterB, state::kCurrencyCoin) == 777 && store.progress(kCharacterA).level == 4
              && store.currency(kCharacterA, state::kCurrencyCoin) == 0 && store.list_items(kCharacterA).empty()
              && store.list_items(kCharacterB).size() == 1,
          "the commands of the token of B change only the character of B, and only the session of B gets the state");
    const json stateA = json::parse(admin.state(kTokenA).body);
    const json stateB = json::parse(admin.state(kTokenB).body);
    check(stateA["selected"] == kCharacterA && stateA["progress"]["level"] == 4 && stateA["items"]["coin"] == 0
              && stateB["selected"] == kCharacterB && stateB["progress"]["level"] == 2 && stateB["items"]["coin"] == 777
              && stateA["characters"].size() == 2 && stateB["characters"].size() == 1,
          "each token reads the state of its own account");
}

/** @return True when a file of a folder (with its subfolders) has the text. */
bool folder_has(const std::filesystem::path& folder, const std::string& text, std::size_t* files = nullptr) {
    bool found = false;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (files != nullptr) {
            ++*files;
        }
        std::ifstream input(entry.path(), std::ios::binary);
        const std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        found = found || content.find(text) != std::string::npos;
    }
    return found;
}

/** X5: the token is in no log line and in no capture file. */
void token_secrecy(const Options& options) {
    const std::filesystem::path folder = options.workDirectory / "admin-captures";
    std::filesystem::remove_all(folder);
    capture::enable(folder.string());
    AdminHost admin;
    const std::size_t linesBefore = log_lines().size();
    const bool polled = admin.state(kTokenA).status == 200 && admin.state(kTokenB).status == 200
                        && admin.send("GET", "/admin/v1/catalog/items", "q=ore", {}, kTokenA).status == 200;
    std::size_t files = 0;
    (void)folder_has(folder, kTokenA, &files);
    check(polled && log_lines().size() == linesBefore && files == 0,
          "a state request and a search request with the reply 200 make no log line and no capture file: the tool window "
          "sends them at an interval");
    const bool sent = admin.command(kTokenA, "give_item", kCharacterA, {{"item", "TestOre"}, {"quantity", 2}}).status == 200
                      && admin.command(kTokenB, "set_level", kCharacterB, {{"level", 2}}).status == 200
                      && admin.command(kTokenWrong, "set_level", kCharacterB, {{"level", 3}}).status == 401
                      && admin.state(kTokenWrong).status == 401
                      && admin.send("GET", "/admin/v1/state", std::string("character=") + kCharacterB, {}, kTokenA).status == 404;
    bool inLog = false;
    for (const std::string& line : log_lines()) {
        inLog = inLog || line.find(kTokenA) != std::string::npos || line.find(kTokenB) != std::string::npos
                || line.find(kTokenWrong) != std::string::npos;
    }
    files = 0;
    const bool inFiles = folder_has(folder, kTokenA, &files) || folder_has(folder, kTokenB) || folder_has(folder, kTokenWrong);
    check(sent && log_lines().size() >= linesBefore + 5 && !inLog && log_count("http: ok POST /admin/v1/commands -> 200 [admin.commands]") == 2
              && log_count("-> 401 [admin.") == 2,
          "the log has a line for each command and for each refused request, and no line has a token (also not a wrong one)");
    check(files == 5 && !inFiles && folder_has(folder, "X-Soulwarden-Admin: (not recorded)") && folder_has(folder, "\"command\":\"give_item\"")
              && folder_has(folder, "ROUTE admin.commands"),
          "the capture folder has one file for each of these 5 requests, with the route and the body of the command; the "
          "header of the token has the text \"(not recorded)\" in place of its value, and no file has a token");
}

/** X6: the item search. */
void item_search(const Options&) {
    AdminHost admin;
    const auto search = [&](const std::string& query) { return json::parse(admin.send("GET", "/admin/v1/catalog/items", query, {}, kTokenA).body); };
    const auto ids = [](const json& reply) {
        std::string text;
        for (const json& item : reply["items"]) {
            text += item["id"].get<std::string>() + ",";
        }
        return text;
    };
    json reply = search("q=ore");
    check(reply["loaded"] == true && reply["total"] == 2 && ids(reply) == "TestOre,Test Ore Large," && reply["items"][0]["type"] == "Resource"
              && reply["items"][0]["maxStack"] == 100 && reply["items"][0]["tier"] == 1 && reply["items"][0]["class"] == "Resource+Ore",
          "a search text finds the items whose id has the text, with the type, the class, the tier, and the stack limit "
          "of each one");
    check(ids(search("q=SWORD")) == "TestSword," && ids(search("q=consumable")) == "TestPotion," && ids(search("q=test+large")) == "Test Ore Large,"
              && ids(search("q=ore%20large")) == "Test Ore Large," && ids(search("q=weapon+ore")).empty()
              && search("q=notype")["total"] == 0 && search("q=")["total"] == 4 && search("")["total"] == 4,
          "the compare ignores the letter case, a word can be in the type or in the class, each word of the text must be "
          "in the item (\"+\" and \"%20\" are a space), an item with no type is not in the result, and an empty text finds "
          "each item that the world can give");
    reply = search("q=test&limit=2&offset=1");
    check(reply["total"] == 4 && ids(reply) == "TestOre,TestPotion," && reply["offset"] == 1 && ids(search("q=test&limit=1")) == "TestSword,"
              && admin.send("GET", "/admin/v1/catalog/items", "q=test&limit=0", {}, kTokenA).status == 400
              && admin.send("GET", "/admin/v1/catalog/items", "q=test&limit=201", {}, kTokenA).status == 400
              && admin.send("GET", "/admin/v1/catalog/items", "q=test&offset=x", {}, kTokenA).status == 400,
          "the limit and the offset select a part of the result, and the reply has the number of all found items; a limit "
          "that is not 1 to 200 and an offset that is not a number get the reply 400");
    ServerHost bare;
    const server::http::Services services{bare.store, "127.0.0.1:32210", "test-version", "", bare.tables, bare.world};
    server::http::AdminAccess access;
    const bool added = access.add(kTokenA, kSteamIdA);
    server::http::Router router;
    server::http::register_admin_routes(router, services, access);
    service::HttpRequest request;
    request.method = "GET";
    request.path = "/admin/v1/catalog/items";
    request.query = "q=ore";
    request.target = request.path + "?" + request.query;
    request.headers.emplace_back(server::http::kAdminTokenHeader, kTokenA);
    const json none = json::parse(server::http::handle_request(router, request).body);
    check(added && none["loaded"] == false && none["total"] == 0 && none["items"].empty(),
          "a backend with no item catalog: the reply says that the catalog is not loaded, and it has no item");
}

/** X7: the query of an admin request is in no log line and in no capture file (review finding 5 of stage 1.7). */
void query_secrecy(const Options& options) {
    const std::filesystem::path folder = options.workDirectory / "admin-query-captures";
    std::filesystem::remove_all(folder);
    capture::enable(folder.string());
    AdminHost admin;
    // A sender puts the token in the query by mistake, with no header.
    const service::HttpResponse refused = admin.send("GET", "/admin/v1/state", std::string("token=") + kTokenA, {}, nullptr);
    const service::HttpResponse refusedCommand =
        admin.send("POST", "/admin/v1/commands", std::string("X-Soulwarden-Admin=") + kTokenB,
                   json{{"command", "set_level"}, {"character", kCharacterB}, {"args", {{"level", 5}}}}.dump(), nullptr);
    // A path below /admin that the router does not have.
    const service::HttpResponse unknown = admin.send("GET", "/admin/v2/state", std::string("token=") + kTokenA, {}, nullptr);
    bool inLog = false;
    for (const std::string& line : log_lines()) {
        inLog = inLog || line.find(kTokenA) != std::string::npos || line.find(kTokenB) != std::string::npos;
    }
    std::size_t files = 0;
    const bool inFiles = folder_has(folder, kTokenA, &files) || folder_has(folder, kTokenB);
    check(refused.status == 401 && refusedCommand.status == 401 && unknown.status == 404
              && admin.host.store.progress(kCharacterB).level == 1,
          "a token in the query does not open the admin interface: reply 401 (404 for a path that is not there), and "
          "nothing changes");
    check(!inLog && log_count("/admin/v1/state?(not recorded) -> 401") == 1
              && log_count("/admin/v1/commands?(not recorded) -> 401") == 1 && log_count("/admin/v2/state?(not recorded) -> 404") == 1,
          "the log line of each of these requests has the text \"(not recorded)\" in place of the query, and no line has "
          "a token");
    check(files == 3 && !inFiles && folder_has(folder, "GET /admin/v1/state?(not recorded)")
              && folder_has(folder, "POST /admin/v1/commands?(not recorded)"),
          "the capture file of each of these requests has the same text in place of the query, and no file has a token");
    // A query with no secret is not recorded too: the rule is for each admin request.
    const std::size_t linesBefore = log_lines().size();
    const bool other = admin.send("GET", "/admin/v1/state", std::string("character=") + kCharacterB, {}, kTokenA).status == 404;
    bool idInLog = false;
    for (std::size_t index = linesBefore; index < log_lines().size(); ++index) {
        idInLog = idInLog || log_lines()[index].find(kCharacterB) != std::string::npos;
    }
    check(other && !idInLog && !folder_has(folder, std::string("character=") + kCharacterB),
          "the query of each admin request that gets a log line is not recorded (a character id of a refused request)");
    // A request that is not for the admin interface keeps its query.
    service::HttpRequest request;
    request.method = "GET";
    request.path = "/administrator";
    request.query = "a=1";
    request.target = "/administrator?a=1";
    service::HttpRequest exact;
    exact.method = "GET";
    exact.path = "/admin";
    exact.query = "token=x";
    exact.target = "/admin?token=x";
    check(capture::recorded_target(request) == "/administrator?a=1" && capture::recorded_target(exact) == "/admin?(not recorded)",
          "a path that only starts with the letters of /admin keeps its query, and the path /admin does not");
}

/** X8: an argument of a command with a wrong JSON form changes nothing (review finding 2 of stage 1.7). */
void argument_forms(const Options&) {
    for (const bool inWorld : {false, true}) {
        for (const json& wrong : {json(), json::array({1}), json::object({{"value", 1}})}) {
            AdminHost admin;
            FakeClient a(27000);
            if (inWorld) {
                admin.host.enter(a, admin.alpha.registration());
            }
            const std::string form = wrong.dump() + (inWorld ? ", character in the world" : ", character not in the world");
            const bool given = admin.command(kTokenA, "give_item", kCharacterA, {{"item", "TestOre"}, {"quantity", 10}}).status == 200;
            const std::string before = rows_of(admin.host.store, kCharacterA);
            const service::HttpResponse removed =
                admin.command(kTokenA, "remove_item", kCharacterA, {{"slot", 0}, {"quantity", wrong}});
            check(given && removed.status == 400 && rows_of(admin.host.store, kCharacterA) == before
                      && before.find("0:TestOre:10") != std::string::npos,
                  "remove_item with the quantity " + form + ": reply 400, and the stack of 10 items stays");
            // The other arguments that are not necessary: the same rule.
            const int giveQuantity =
                admin.command(kTokenA, "give_item", kCharacterA, {{"item", "TestOre"}, {"quantity", wrong}}).status;
            const int giveGearScore = admin.command(kTokenA, "give_item", kCharacterA,
                                                    {{"item", "TestSword"}, {"quantity", 1}, {"gearScore", wrong}})
                                          .status;
            const int giveAction = admin.command(kTokenA, "give_item", kCharacterA,
                                                 {{"item", "TestOre"}, {"quantity", 1}, {"action", wrong}})
                                       .status;
            const int masteryExperience = admin.command(kTokenA, "set_weapon_mastery", kCharacterA,
                                                        {{"weapon", "sword"}, {"level", 3}, {"experience", wrong}})
                                              .status;
            const int tradeExperience = admin.command(kTokenA, "set_trade_skill", kCharacterA,
                                                      {{"skill", "Mining"}, {"level", 3}, {"experience", wrong}})
                                            .status;
            check(giveQuantity == 400 && giveGearScore == 400 && giveAction == 400 && masteryExperience == 400
                      && tradeExperience == 400 && rows_of(admin.host.store, kCharacterA) == before,
                  "give_item (quantity, gearScore, action), set_weapon_mastery and set_trade_skill (experience) with the "
                  "value " + form + ": reply 400, and no row changes");
        }
    }
    // With no such argument, and with a correct one, each command works as before.
    AdminHost admin;
    const bool plain = admin.command(kTokenA, "give_item", kCharacterA, {{"item", "TestOre"}}).status == 200
                       && admin.command(kTokenA, "give_item", kCharacterA, {{"item", "TestOre"}, {"quantity", "4"}}).status == 200
                       && admin.command(kTokenA, "remove_item", kCharacterA, {{"slot", 0}, {"quantity", 2}}).status == 200
                       && rows_of(admin.host.store, kCharacterA).find("0:TestOre:3") != std::string::npos
                       && admin.command(kTokenA, "remove_item", kCharacterA, {{"slot", 0}}).status == 200
                       && admin.host.store.list_items(kCharacterA).empty()
                       && admin.command(kTokenA, "set_weapon_mastery", kCharacterA, {{"weapon", "sword"}, {"level", 3}}).status == 200;
    check(plain, "with no such argument, and with a correct one (a number or a text), each command works: a remove with no "
                 "quantity removes the full stack");
}

} // namespace

std::vector<Case> admin_cases() {
    return {
        {"X1", "admin interface: no token, a wrong token, a request of the game, a request of a browser; no harness gate", access_rules},
        {"X2", "admin interface: the state of the characters of the account of a token, and nothing of a different account", state_view},
        {"X3", "admin interface: each command with a correct and with a wrong value", commands_each},
        {"X4", "admin interface: two accounts with their own tokens and two sessions change only their own characters", two_accounts},
        {"X5", "admin interface: the token is in no log line and in no capture file", token_secrecy},
        {"X6", "admin interface: the item search of the catalog", item_search},
        {"X7", "admin interface: the query of an admin request is in no log line and in no capture file", query_secrecy},
        {"X8", "admin interface: an argument with a wrong JSON form gives the reply 400 and changes nothing", argument_forms},
    };
}

} // namespace soulwarden::backend::tests

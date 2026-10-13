// Cases for the findings of the code review of stage 0.6b: the stop of the HTTP listener, a
// world task that did not start in its time limit, a position that is not a position of the
// world, and the size limit of a state bundle. Case T11 is the correct-path case of that
// review.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <future>
#include <limits>
#include <thread>

#include <nlohmann/json.hpp>

#include "core/encoding/base64.h"
#include "server/http/router.h"
#include "server/http/routes.h"
#include "server/rep/replication.h"
#include "service/http_server.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/server_host.h"
#include "support/state_decoder.h"
#include "support/test_players.h"

namespace soulwarden::backend::tests {

std::string hex_text(const Bytes& data);

namespace {

constexpr const char* kCharacterA = "aaaaaaaa-0000-4000-8000-00000000000a";
constexpr const char* kCharacterB = "bbbbbbbb-0000-4000-8000-00000000000b";
/** Largest payload of a bundle that the client reads (protocol notes 4.1). */
constexpr std::size_t kClientBundleLimit = 256000;

/** @return A loopback port that no socket has now. */
std::uint16_t free_port() {
    const int probe = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(probe, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    socklen_t length = sizeof(address);
    getsockname(probe, reinterpret_cast<sockaddr*>(&address), &length);
    close(probe);
    return ntohs(address.sin_port);
}

/** H1: the stop of the HTTP listener waits for a handler that runs (review finding 1). */
void http_stop(const Options&) {
    // The start of the backend program: a write to a socket that the peer closed gives an
    // error, not a signal.
    net::startup();
    std::promise<void> entered;
    std::promise<void> release;
    const std::shared_future<void> released = release.get_future().share();
    std::atomic<bool> handlerEnded{false};
    std::atomic<bool> stopReturned{false};
    auto server = std::make_unique<service::HttpServer>();
    const std::uint16_t port = free_port();
    const bool started = server->start("127.0.0.1", port, [&](const service::HttpRequest&) {
        entered.set_value();
        released.wait();
        // The handler uses objects of its caller after its wait, as a route uses the world.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        handlerEnded = true;
        return service::HttpResponse{};
    });
    check(started, "the HTTP listener starts on a loopback port");
    if (!started) {
        return;
    }
    const int client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    const bool connected = connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
    const std::string request = "GET /healthz HTTP/1.1\r\nHost: localhost\r\n\r\n";
    const bool sent = connected && send(client, request.data(), request.size(), 0) == static_cast<ssize_t>(request.size());
    check(sent && entered.get_future().wait_for(std::chrono::seconds(5)) == std::future_status::ready,
          "a request is in its handler, and the handler waits");

    std::thread stopper([&] {
        server->stop();
        stopReturned = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    check(!stopReturned && server->connection_count() == 1,
          "the stop does not return while the handler runs: the connection thread is alive, and the caller cannot "
          "destroy the server");
    release.set_value();
    stopper.join();
    check(stopReturned && handlerEnded && server->connection_count() == 0,
          "the stop returns after the end of the handler, and no connection thread is alive");
    server.reset();
    close(client);
    check(true, "the server is destroyed after the stop with no thread that uses it");

    // A connection that ended before the stop: the accept loop joins its thread, and a stop
    // with no connection returns at once.
    service::HttpServer second;
    const std::uint16_t secondPort = free_port();
    std::atomic<int> requests{0};
    if (second.start("127.0.0.1", secondPort, [&](const service::HttpRequest&) {
            ++requests;
            return service::HttpResponse{};
        })) {
        for (int round = 0; round < 3; ++round) {
            const int quick = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            address.sin_port = htons(secondPort);
            if (connect(quick, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
                send(quick, request.data(), request.size(), 0);
                char reply[256];
                (void)recv(quick, reply, sizeof(reply), 0);
            }
            close(quick);
        }
        for (int attempt = 0; attempt < 2000 && second.connection_count() != 0; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto begin = std::chrono::steady_clock::now();
        second.stop();
        const auto elapsed = std::chrono::steady_clock::now() - begin;
        check(requests == 3 && second.connection_count() == 0 && elapsed < std::chrono::seconds(2),
              "three connections that ended before the stop: the stop joins their threads and returns at once");
    } else {
        check(false, "the second HTTP listener starts");
    }
}

/** D2: a delete request whose world task did not start in the time limit (review finding 2). */
void late_delete(const Options&) {
    ServerHost host;
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", state::WorldPosition{100.0F, 200.0F, 50.0F});
    FakeClient client(27000);
    host.enter(client, alpha.registration());
    const server::http::Services services{host.store, "127.0.0.1:32210", "test-version", "", host.tables, host.world};
    server::http::Router router;
    server::http::register_gateway_routes(router, services);
    server::http::register_dev_routes(router, services);
    service::HttpRequest request;
    request.method = "DELETE";
    request.host = "gateway.test";
    request.path = std::string("/prod/game/characters/") + kCharacterA;
    request.target = request.path;
    request.headers.emplace_back("x-nw-auth", encoding::local_jwt(nlohmann::json{{"personaId", alpha.account.personaId}}.dump()));
    int status = 0;
    // The route runs in a second thread, as in an HTTP thread. The world gets no tick while
    // the route waits (2 s).
    std::thread http([&] {
        std::string name;
        status = router.dispatch(request, name).status;
    });
    http.join();
    check(status == 409 && host.store.find_character(kCharacterA).has_value(),
          "a delete request whose task did not start in the time limit: reply 409, and the row is there");
    check(log_count("world: a task of a different thread did not start in 2000 ms: it was cancelled") == 1,
          "the world cancelled the task, and the log has the line");
    host.tick();
    host.steps(3, {&client});
    check(host.store.find_character(kCharacterA).has_value() && host.world.find_character(kCharacterA) != nullptr,
          "the next ticks of the world do not run the cancelled task: the row and the entity are there");
    check(!host.session(1).closing(), "the session of the character does not get the message 'kicked' later");

    // The same for a development command: reply 503, and no change later.
    service::HttpRequest give;
    give.method = "POST";
    give.path = "/v1/dev/world/touch";
    give.query = std::string("character=") + kCharacterA;
    const world::Revision revision = host.world.revision();
    std::thread dev([&] {
        std::string name;
        status = router.dispatch(give, name).status;
    });
    dev.join();
    host.tick();
    check(status == 503 && host.world.revision() == revision,
          "a development command that did not start: reply 503, and the next tick does not run it");

    // A task that started is the truth: the caller waits for its end, also after the limit.
    std::atomic<bool> inTask{false};
    std::optional<int> value;
    world::CallStatus callStatus = world::CallStatus::notStarted;
    std::thread caller([&] {
        value = host.world.call<int>(
            [&](world::World&) {
                inTask = true;
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                return 7;
            },
            std::chrono::milliseconds(50),
            &callStatus);
    });
    for (int attempt = 0; attempt < 2000 && !inTask; ++attempt) {
        host.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    caller.join();
    check(value.has_value() && *value == 7 && callStatus == world::CallStatus::done,
          "a task that started and needs more time than the limit: the caller waits for it and gets its value");
    std::optional<int> failed;
    std::thread thrower([&] {
        failed = host.world.call<int>([](world::World&) -> int { throw std::runtime_error("test"); }, std::chrono::milliseconds(2000), &callStatus);
    });
    for (int attempt = 0; attempt < 2000 && callStatus != world::CallStatus::failed; ++attempt) {
        host.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    thrower.join();
    check(!failed.has_value() && callStatus == world::CallStatus::failed,
          "a task that started and stopped with an exception has the status 'failed', not 'did not start'");
}

/** P2: a position that is not a position of the world (review finding 3). */
void bad_positions(const Options&) {
    ServerHost host;
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", state::WorldPosition{100.0F, 200.0F, 50.0F});
    add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", std::nullopt);
    host.tick();
    const server::http::Services services{host.store, "127.0.0.1:32210", "test-version", "", host.tables, host.world};
    server::http::Router router;
    server::http::register_dev_routes(router, services);
    const auto send = [&](const std::string& path, const std::string& query) {
        service::HttpRequest request;
        request.method = "POST";
        request.path = path;
        request.query = query;
        std::string route;
        return router.dispatch(request, route).status;
    };
    const std::string forB = std::string("character=") + kCharacterB;
    check(send("/v1/dev/world/spawn", forB + "&x=nan&y=0&z=50") == 400 && send("/v1/dev/world/spawn", forB + "&x=inf&y=0&z=50") == 400
              && send("/v1/dev/world/spawn", forB + "&x=3e38&y=0&z=50") == 400
              && send("/v1/dev/world/spawn", forB + "&x=100&y=-2000000&z=50") == 400
              && send("/v1/dev/world/spawn", forB + "&x=100&y=200&z=1e9") == 400 && host.world.find_character(kCharacterB) == nullptr,
          "the spawn route refuses a position with a number that is not finite or that is outside the limits of the "
          "world (reply 400), and the world gets no entity");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    check(host.world.spawn_character(kCharacterB, state::WorldPosition{nan, 0.0F, 0.0F}) == world::SpawnError::badPosition
              && host.world.spawn_character(kCharacterB, state::WorldPosition{0.0F, 3.0e38F, 0.0F}) == world::SpawnError::badPosition
              && host.world.entities().empty(),
          "the world itself refuses such a position for a new entity, for each caller");

    FakeClient client(27000);
    host.enter(client, alpha.registration());
    check(send("/v1/dev/world/spawn", forB + "&near=" + kCharacterA + "&dx=3e38&dy=0&dz=0") == 400
              && send("/v1/dev/world/spawn", forB + "&near=" + kCharacterA + "&dx=900000&dy=900000&dz=0") == 200,
          "the spawn route checks the sum for 'near' too: a distance of 3e38 is refused, and a position in the limits is "
          "accepted");
    const world::Entity* entity = host.world.find_character(kCharacterA);
    const world::EntityId id = entity->id;
    check(!host.world.set_position(id, state::WorldPosition{nan, 200.0F, 50.0F}, false)
              && !host.world.set_position(id, state::WorldPosition{100.0F, std::numeric_limits<float>::infinity(), 50.0F}, false)
              && !host.world.set_position(id, state::WorldPosition{2.0e6F, 200.0F, 50.0F}, false) && entity->position.x == 100.0F
              && entity->position.y == 200.0F && log_count("is not a position of the world: the entity keeps its position") == 1,
          "the world refuses such a position for an entity that is there: the entity keeps its position, and the log has "
          "one line");
    // The position of a client: an input report with a camera number that is not finite.
    host.datagram(client.peer, client.input(1, nan, 196.0F, 51.65F));
    host.datagram(client.peer, client.input(2, 3.0e38F, 196.0F, 51.65F));
    host.steps(3, {&client});
    check(std::isfinite(entity->position.x) && std::isfinite(entity->position.y) && std::abs(entity->position.x) <= world::kMaxHorizontalPosition
              && log_count("has a camera number that is not finite: not used") == 1,
          "an input report of a client with a camera that is not finite, or that is very far away, gives the entity no "
          "position outside the limits");
    host.datagram(client.peer, client.input_at(40, 130.0F, 200.0F, 50.0F));
    host.steps(3, {&client});
    check(entity->position.x == 130.0F, "a correct input report after that moves the entity");

    check(send("/v1/dev/world/fragment", std::string("character=") + kCharacterA + "&slot=1.5&type=11&state=00") == 400
              && send("/v1/dev/world/fragment", std::string("character=") + kCharacterA + "&slot=-1&type=11&state=00") == 400
              && send("/v1/dev/world/fragment", std::string("character=") + kCharacterA + "&slot=4096&type=11&state=00") == 400
              && send("/v1/dev/world/fragment", std::string("character=") + kCharacterA + "&slot=61&type=65536&state=00") == 400
              && send("/v1/dev/world/fragment", std::string("character=") + kCharacterA + "&slot=61&type=nan&state=00") == 400
              && send("/v1/dev/world/fragment", std::string("character=") + kCharacterA + "&slot=61&type=1755&state=0101000000") == 200,
          "the fragment route reads the slot and the type as whole numbers with limits");
}

/** F1: the size of a state bundle (review finding 4). */
void bundle_limits(const Options&) {
    ServerHost host;
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", std::nullopt);
    FakeClient client(27000);
    host.enter(client, alpha.registration());
    const server::http::Services services{host.store, "127.0.0.1:32210", "test-version", "", host.tables, host.world};
    server::http::Router router;
    server::http::register_dev_routes(router, services);
    const auto fragment = [&](unsigned slot, std::size_t textSize) {
        // A state of the player component with one long text: the decoder of the tests reads it.
        server::rep::StateWriter state(3);
        state.field(2, 1).string(std::string(textSize, 'X'));
        service::HttpRequest request;
        request.method = "POST";
        request.path = "/v1/dev/world/fragment";
        request.query = std::string("character=") + kCharacterA + "&slot=" + std::to_string(slot) + "&type=3935&state=" + hex_text(state.finish());
        std::string route;
        return router.dispatch(request, route).status;
    };
    check(fragment(50, 29995) == 400, "the fragment route refuses a state of 30,000 bytes (16,384 bytes at most)");
    bool accepted = true;
    for (unsigned slot = 50; slot < 59; ++slot) {
        accepted = accepted && fragment(slot, 15995) == 200;
        host.steps(1, {&client});
    }
    check(accepted, "the fragment route accepts nine fragments of 16,000 bytes for nine slots");
    // A fragment that no route accepts: the world function has no limit, the sender has.
    const world::EntityId id = host.world.find_character(kCharacterA)->id;
    host.world.set_raw_fragment(id, 59, 3935, std::vector<std::uint8_t>(300000, 0));
    host.steps(1, {&client});
    check(log_count("the fragment for slot 59, type 3935 (300000 bytes) is larger than the client permits for one bundle (256000 bytes): not sent") == 1,
          "a fragment that is larger than the bundle limit of the client does not go out, and the log has the line");

    // A newer session of the character gets all stored fragments in its first record.
    FakeClient next(27001);
    host.enter(next, alpha.registration());
    host.steps(2, {&next});
    std::size_t largest = 0;
    std::size_t total = 0;
    unsigned bundles = 0;
    unsigned large = 0;
    bool complete = true;
    std::uint64_t sequence = 0;
    bool gap = false;
    for (const Sent& sent : next.got) {
        if (sent.type != 8) {
            continue;
        }
        const Bundle bundle = read_bundle(sent.body);
        complete = complete && bundle.complete;
        gap = gap || bundle.sequence != ++sequence;
        server::hub::Reader reader(sent.body);
        reader.u8();
        reader.size64();
        reader.raw(4);
        const std::size_t payload = reader.size();
        largest = std::max(largest, payload);
        total += payload;
        ++bundles;
        if (payload > 1024) {
            ++large;
            // A bundle above one carrier message has one fragment only: it is cut at a fragment border.
            complete = complete && bundle.records.size() == 1 && bundle.records[0].fragments.size() == 1;
        }
    }
    note("first record of the newer session: " + std::to_string(total) + " payload bytes in " + std::to_string(bundles)
         + " bundles, largest payload " + std::to_string(largest) + " bytes");
    check(total > 144000 && large == 9 && largest <= kClientBundleLimit && largest < 17000,
          "the first record of the newer session has the nine stored fragments: each one in its own bundle, and no bundle "
          "is larger than the client permits (256,000 bytes)");
    check(complete && !gap, "each bundle decodes to its last byte, each large bundle has one fragment, and the sequences have no gap");
    check(log_count("larger than the client permits for one bundle") == 2 && !host.session(2).closing(),
          "the fragment of 300,000 bytes does not go to the newer session, and the session continues");
    const std::size_t messages = next.got.size();
    host.steps(30, {&next});
    bool repeated = false;
    for (std::size_t index = messages; index < next.got.size(); ++index) {
        if (next.got[index].type == 8) {
            repeated = repeated || read_bundle(next.got[index].body).records.at(0).fragments.size() != 1;
        }
    }
    check(!repeated, "the record counts as sent after its last bundle: the next ticks send only the time anchor");
}

/** T11: sessions of one account, three owners of one character, and 101 stops (case RVC of the review). */
void sessions_and_stops(const Options&) {
    service::RepServerOptions options;
    options.observePlayers = true;
    ServerHost host(options);
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", state::WorldPosition{100.0F, 200.0F, 50.0F});
    state::Character second = *host.store.find_character(kCharacterA);
    second.characterId = kCharacterB;
    second.name = "Beta";
    check(host.store.insert_character(second) == state::InsertResult::inserted
              && host.store.set_character_position(kCharacterB, state::WorldPosition{100.0F, 200.0F, 50.0F}),
          "a second character of the same account, near the first one");
    Registration beta = alpha.registration();
    beta.characterId = kCharacterB;
    FakeClient one(27000);
    FakeClient two(27001);
    FakeClient three(27002);
    FakeClient four(27003);
    host.enter(one, alpha.registration());
    host.enter(two, beta);
    check(host.world.entities().size() == 2 && !host.session(1).closing() && !host.session(2).closing(),
          "two characters of one account have separate sessions");
    host.enter(three, alpha.registration());
    host.enter(four, alpha.registration());
    check(host.session(1).closing().has_value() && host.session(3).closing().has_value() && !host.session(4).closing(),
          "three owners of one character, one after the other: only the newest session is active");
    host.datagram(one.peer, one.input_at(1, 999.0F, 999.0F, 50.0F));
    host.datagram(three.peer, three.input_at(1, 999.0F, 999.0F, 50.0F));
    check(host.world.find_character(kCharacterA)->owner == 4 && host.world.find_character(kCharacterA)->position.x == 100.0F,
          "the inputs of the two replaced sessions do not move the entity");
    host.datagram(one.peer, one.carrier_close());
    host.datagram(three.peer, three.carrier_close());
    check(host.world.entities().size() == 2 && host.world.find_character(kCharacterA)->owner == 4
              && host.world.find_character(kCharacterB)->owner == 2,
          "the end of the replaced sessions keeps the two entities");
    host.datagram(four.peer, four.input_at(1, 100.0F, 200.0F, 50.0F));
    host.steps(60, {&two, &four});
    std::vector<std::string> ids;
    for (unsigned number = 0; number < 101; ++number) {
        state::Character character = second;
        character.characterId = "made-up-remote-" + std::to_string(number);
        character.name = "Remote " + std::to_string(number);
        if (host.store.insert_character(character) != state::InsertResult::inserted
            || host.world.spawn_character(character.characterId, state::WorldPosition{102.0F, 202.0F, 50.0F}) != world::SpawnError::none) {
            throw std::runtime_error("no remote entity");
        }
        ids.push_back(character.characterId);
    }
    host.steps(60, {&two, &four});
    check(host.session(4).observer().known().size() == 103, "the newest owner observes its own entity, Beta, and 101 remote entities");
    const std::size_t from = four.got.size();
    for (const std::string& id : ids) {
        host.world.remove_character(id, world::LostCause::deleted);
    }
    host.steps(1, {&two, &four});
    unsigned count = 0;
    unsigned controlBundles = 0;
    bool equal = true;
    for (std::size_t index = from; index < four.got.size(); ++index) {
        if (four.got[index].type != 8) {
            continue;
        }
        const Bundle bundle = read_bundle(four.got[index].body);
        if (bundle.stops.empty()) {
            continue;
        }
        equal = equal && bundle.pauses.empty() && bundle.stops.size() <= 100;
        count += static_cast<unsigned>(bundle.stops.size());
        ++controlBundles;
    }
    check(equal && count == 101 && controlBundles == 2 && host.session(4).observer().known().size() == 2,
          "101 stops go out in two bundles, with 100 numbers at most in each one");
}

} // namespace

std::vector<Case> review_cases() {
    return {
        {"H1", "HTTP stop: the stop waits for each connection thread (review finding 1 of stage 0.6b)", http_stop},
        {"D2", "a world task that did not start in its time limit is cancelled: no late delete (review finding 2)", late_delete},
        {"P2", "a position that is not a position of the world is refused for each source (review finding 3)", bad_positions},
        {"F1", "state bundles: cut at a fragment border, the bundle limit of the client, sent after the last bundle (review finding 4)", bundle_limits},
        {"T11", "sessions of one account, three owners of one character, 101 stops in two bundles (review case RVC)", sessions_and_stops},
    };
}

} // namespace soulwarden::backend::tests

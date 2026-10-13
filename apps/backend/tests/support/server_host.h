#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "gamedata/appearance_tables.h"
#include "gamedata/combat_tables.h"
#include "gamedata/creature_data.h"
#include "gamedata/height_maps.h"
#include "gamedata/item_catalog.h"
#include "gamedata/progression_tables.h"
#include "gamedata/travel_places.h"
#include "service/rep_server.h"
#include "state/store.h"
#include "support/fake_client.h"
#include "support/fake_transport.h"
#include "support/manual_clock.h"

namespace soulwarden::backend::tests {

/**
 * The game server of the tree with no socket and no thread: a manual clock, a transport that
 * keeps the datagrams, and a database in memory. The test calls the handler functions that
 * the DTLS listener calls in the backend program.
 */
/** The terrain of a test: the height that a case gives. With no function, no place has a height. */
struct TestTerrain final : gamedata::Terrain {
    std::function<std::optional<float>(float, float)> source;

    [[nodiscard]] std::optional<float> height(float x, float y) override {
        return source ? source(x, y) : std::nullopt;
    }
};

struct ServerHost {
    ManualClock clock;
    state::Store store;
    gamedata::AppearanceTables tables;
    /** The item catalog of the world. It is empty until a case sets it: the world then gives no item. */
    gamedata::ItemCatalog catalog;
    /** The progression tables of the world. They are empty until a case sets them: the world then has no rule of a table. */
    gamedata::ProgressionTables progression;
    /** The creature data of the world. It is empty until a case sets it: the world then has no creature. */
    gamedata::CreatureData creatures;
    /** The combat tables of the world. They are empty until a case sets them: an attack then has no effect. */
    gamedata::CombatTables combat;
    /** The travel places of the world. They are empty until a case sets them: the travel command then takes only numbers. */
    gamedata::TravelPlaces places;
    /** The terrain of the world. It has no height until a case sets one: no creature then leaves its place. */
    TestTerrain terrain;
    world::World world{clock, store, tables, with_game_data(catalog, progression, creatures, combat, places, terrain)};
    FakeTransport transport;
    std::unique_ptr<service::RepServer> server;
    /** Time of the last datagram that each peer sent. */
    std::map<std::uint64_t, world::WorldClock::time_point> lastDatagram;

    static world::WorldOptions with_game_data(const gamedata::ItemCatalog& catalog,
                                              const gamedata::ProgressionTables& progression) {
        world::WorldOptions options;
        options.itemCatalog = &catalog;
        options.progressionTables = &progression;
        return options;
    }
    static world::WorldOptions with_game_data(const gamedata::ItemCatalog& catalog,
                                              const gamedata::ProgressionTables& progression,
                                              const gamedata::CreatureData& creatures) {
        world::WorldOptions options = with_game_data(catalog, progression);
        options.creatureData = &creatures;
        return options;
    }
    static world::WorldOptions with_game_data(const gamedata::ItemCatalog& catalog,
                                              const gamedata::ProgressionTables& progression,
                                              const gamedata::CreatureData& creatures,
                                              const gamedata::CombatTables& combat) {
        world::WorldOptions options = with_game_data(catalog, progression, creatures);
        options.combatTables = &combat;
        return options;
    }

    static world::WorldOptions with_game_data(const gamedata::ItemCatalog& catalog,
                                              const gamedata::ProgressionTables& progression,
                                              const gamedata::CreatureData& creatures,
                                              const gamedata::CombatTables& combat,
                                              const gamedata::TravelPlaces& places) {
        world::WorldOptions options = with_game_data(catalog, progression, creatures, combat);
        options.travelPlaces = &places;
        return options;
    }

    static world::WorldOptions with_game_data(const gamedata::ItemCatalog& catalog,
                                              const gamedata::ProgressionTables& progression,
                                              const gamedata::CreatureData& creatures,
                                              const gamedata::CombatTables& combat,
                                              const gamedata::TravelPlaces& places,
                                              gamedata::Terrain& terrain) {
        world::WorldOptions options = with_game_data(catalog, progression, creatures, combat, places);
        options.terrain = &terrain;
        return options;
    }

    explicit ServerHost(const std::string& serverVersion = "test-version") {
        service::RepServerOptions options;
        options.serverVersion = serverVersion;
        start(options);
    }
    explicit ServerHost(const service::RepServerOptions& options) {
        start(options);
    }

    void start(const service::RepServerOptions& options) {
        if (!store.open(":memory:")) {
            throw std::runtime_error("test host: the store did not open");
        }
        server = std::make_unique<service::RepServer>(transport, world, options);
    }

    void connect(std::uint64_t peer) {
        server->on_connected(peer);
    }
    /** What the DTLS listener does for a peer that the game server closed, or that closed itself. */
    void disconnect(std::uint64_t peer) {
        server->on_disconnected(peer);
    }
    void datagram(std::uint64_t peer, std::span<const std::uint8_t> data) {
        lastDatagram[peer] = clock.now();
        server->on_datagram(peer, data);
    }
    /** @return The time from the last datagram of a peer to now. */
    std::chrono::nanoseconds silence(std::uint64_t peer) {
        return clock.now() - lastDatagram[peer];
    }
    void tick() {
        server->on_tick();
    }
    void advance(std::chrono::milliseconds duration) {
        clock.advance(duration);
    }
    std::vector<Bytes> take(std::uint64_t peer) {
        return transport.take(peer);
    }

    /** @return The session with this id. The test stops with an exception when it is not there. */
    const server::rep::RepSession& session(std::uint32_t id) {
        const server::rep::RepSession* found = server->session(id);
        if (found == nullptr) {
            throw std::runtime_error("test host: no session " + std::to_string(id));
        }
        return *found;
    }

    /** A client connects, registers with the character of a player, and reads the replies. */
    void enter(FakeClient& client, const Registration& fields) {
        client.connect(*this);
        steps(2, {&client});
        datagram(client.peer, client.registration(fields));
        steps(2, {&client});
    }

    /** `count` times: 10 ms pass, the server ticks, each client reads and acknowledges. */
    void steps(int count, std::initializer_list<FakeClient*> clients) {
        for (int index = 0; index < count; ++index) {
            advance(std::chrono::milliseconds(10));
            tick();
            for (FakeClient* client : clients) {
                client->exchange(*this);
            }
        }
    }
};

} // namespace soulwarden::backend::tests

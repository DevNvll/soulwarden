#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include "core/capture/http_capture.h"
#include "core/logging/log.h"
#include "gamedata/appearance_tables.h"
#include "gamedata/combat_tables.h"
#include "gamedata/creature_data.h"
#include "gamedata/item_catalog.h"
#include "gamedata/progression_tables.h"
#include "gamedata/slice_ids.h"
#include "gamedata/travel_places.h"
#include "server/http/admin_access.h"
#include "server/http/request_handler.h"
#include "server/http/router.h"
#include "server/http/routes.h"
#include "service/dtls_server.h"
#include "service/http_server.h"
#include "service/rep_server.h"
#include "state/identity.h"
#include "state/store.h"
#include "world/world.h"

// Standalone local backend for the New World client. It listens on loopback only.

namespace {

std::atomic<bool> g_stop{false};

void on_signal(int) {
    g_stop = true;
}

struct Options {
    std::string dataDirectory;
    /** Directory of the tables that a tool makes from the game copy. Default: DATA_DIR/gamedata. */
    std::string gameDataDirectory;
    std::string httpHost{"127.0.0.1"};
    std::uint16_t httpPort{32200};
    bool capture{false};
    std::string repAddress{"127.0.0.1:32210"};
    std::string worldVersion{"[RETAIL].Javelin.1.400.6031.6004151"};
    /** Development aid: an account with no character gets this character at its login. */
    std::string seedCharacter;
    /** Research switch: leave the game server port free for an external tool. */
    bool repListener{true};
    /** Research switch for live checks of the session end messages (server/rep/research.h). */
    soulwarden::backend::server::rep::ResearchSwitch research;
    bool researchOption{false};
    /** Each session observes the entities of other characters near its character. */
    bool observePlayers{false};
    /** Development commands for the world (server/http/dev_routes.cpp). */
    bool devCommands{false};
    /** The client reports its melee contacts (server/http/routes.h, AwsOptions). */
    bool meleeReport{true};
};

/**
 * Automation and diagnostics need the harness gate: the process has SOULWARDEN_HARNESS=1 in its
 * environment. The launcher gives it only to a run with the harness.
 */
bool harness_gate() {
    const char* value = std::getenv("SOULWARDEN_HARNESS");
    return value != nullptr && std::strcmp(value, "1") == 0;
}

/**
 * Reads the token of the admin interface that the launcher made for this start, and the Steam
 * id of the game of this start (the fallback identity when the launcher gave none). The token
 * comes in the environment, not as an option: other users of the computer can read the
 * options of a process. The variable leaves the environment after the read.
 * @return A message for the log. It never has the token.
 */
std::string read_admin_access(soulwarden::backend::server::http::AdminAccess& access) {
    using namespace soulwarden::backend;
    const char* tokenText = std::getenv("SOULWARDEN_ADMIN_TOKEN");
    const std::string token = tokenText != nullptr ? tokenText : "";
    const char* steamText = std::getenv("SOULWARDEN_ADMIN_STEAM_ID");
    const std::string steamId = steamText != nullptr && steamText[0] != '\0' ? steamText : state::local_identity().fallbackSteamId;
#ifdef _WIN32
    _putenv_s("SOULWARDEN_ADMIN_TOKEN", "");
#else
    unsetenv("SOULWARDEN_ADMIN_TOKEN");
#endif
    if (token.empty()) {
        return "backend: the admin interface is off: the launcher gave no token (SOULWARDEN_ADMIN_TOKEN). The tool "
               "window of the game gets no data";
    }
    if (!access.add(token, steamId)) {
        return "backend: the admin interface is off: the token is shorter than " + std::to_string(server::http::kAdminTokenMinimum)
               + " characters, or SOULWARDEN_ADMIN_STEAM_ID is not a Steam id";
    }
    return "backend: the admin interface is on (routes /admin/v1) for the account of Steam id " + state::masked_steam_id(steamId);
}

bool parse(int argc, char** argv, Options& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        const auto value = [&]() -> const char* { return index + 1 < argc ? argv[++index] : nullptr; };
        if (argument == "--data-dir") {
            const char* text = value();
            if (text == nullptr) return false;
            options.dataDirectory = text;
        } else if (argument == "--gamedata-dir") {
            const char* text = value();
            if (text == nullptr) return false;
            options.gameDataDirectory = text;
        } else if (argument == "--http-host") {
            const char* text = value();
            if (text == nullptr) return false;
            options.httpHost = text;
        } else if (argument == "--http-port") {
            const char* text = value();
            if (text == nullptr) return false;
            options.httpPort = static_cast<std::uint16_t>(std::strtoul(text, nullptr, 10));
        } else if (argument == "--rep-address") {
            const char* text = value();
            if (text == nullptr) return false;
            options.repAddress = text;
        } else if (argument == "--seed-character") {
            const char* text = value();
            if (text == nullptr) return false;
            options.seedCharacter = text;
        } else if (argument == "--world-version") {
            const char* text = value();
            if (text == nullptr) return false;
            options.worldVersion = text;
        } else if (argument == "--no-rep-listener") {
            options.repListener = false;
        } else if (argument == "--research-session") {
            const char* text = value();
            if (text == nullptr || !soulwarden::backend::server::rep::read_research_option(text, options.research)) return false;
            options.researchOption = true;
        } else if (argument == "--observe-players") {
            options.observePlayers = true;
        } else if (argument == "--dev-commands") {
            options.devCommands = true;
        } else if (argument == "--no-melee-report") {
            options.meleeReport = false;
        } else if (argument == "--capture") {
            options.capture = true;
        } else {
            return false;
        }
    }
    return !options.dataDirectory.empty();
}

/**
 * Gives the free memory of the allocator back to the system, where the C library can do that.
 * The start of the backend reads large files: without this call the process keeps the memory
 * of their parsed form for its whole run.
 */
void release_free_memory() {
#if defined(__GLIBC__)
    malloc_trim(0);
#endif
}

} // namespace

int main(int argc, char** argv) {
    using namespace soulwarden::backend;

    Options options;
    if (!parse(argc, argv, options)) {
        std::fprintf(stderr,
                     "usage: soulwarden-backend --data-dir DIR [--gamedata-dir DIR] [--http-host HOST] [--http-port PORT] "
                     "[--rep-address HOST:PORT] [--no-rep-listener] [--seed-character NAME] [--world-version TEXT] "
                     "[--capture] [--observe-players] [--no-melee-report]\n"
                     "only with SOULWARDEN_HARNESS=1 in the environment: --dev-commands, --research-session "
                     "registration-result=N | reject=N | kick=N | kick-delay=SECONDS | refuse-equipment=1\n");
        return 2;
    }
    if ((options.researchOption || options.devCommands) && !harness_gate()) {
        // Before each listener: a backend that is started directly must not get diagnostics
        // that the launcher gives only to a run with the harness.
        std::fprintf(stderr,
                     "soulwarden-backend: --research-session and --dev-commands need SOULWARDEN_HARNESS=1 in the environment. "
                     "The backend was not started.\n");
        return 2;
    }
    if (options.httpHost.rfind("127.", 0) != 0) {
        std::fprintf(stderr, "soulwarden-backend: the listener address must be a loopback address.\n");
        return 2;
    }
    const std::filesystem::path dataDirectory = options.dataDirectory;
    std::filesystem::create_directories(dataDirectory / "logs");
    log::open((dataDirectory / "logs" / "backend.log").string());
    if (options.capture) {
        capture::enable((dataDirectory / "captures" / "http").string());
    }
    if (!net::startup()) {
        log::write("backend: socket startup failed");
        return 1;
    }

    state::Store store;
    if (!store.open((dataDirectory / "soulwarden.sqlite3").string())) {
        log::write("backend: the player database did not open");
        return 1;
    }
    // Game data stays outside the repository. Without the tables the backend cannot convert
    // the appearance names of a character to the indexes of the world state.
    const std::filesystem::path gameDataDirectory =
        options.gameDataDirectory.empty() ? dataDirectory / "gamedata" : std::filesystem::path(options.gameDataDirectory);
    const std::string appearanceTablesPath = (gameDataDirectory / gamedata::kAppearanceTablesFile).string();
    gamedata::AppearanceTables appearanceTables;
    if (appearanceTables.load(appearanceTablesPath)) {
        log::write("backend: appearance tables loaded from %s", appearanceTablesPath.c_str());
    } else {
        log::write("backend: no appearance tables at %s: each character gets the default appearance in the "
                   "world, and a character with no creation data has no body in the menu. Make the file with "
                   "tools/gamedata/appearance.sh",
                   appearanceTablesPath.c_str());
    }
    // The ids of the player slice: with them the game server checks the target of each
    // component call of a client.
    const std::string sliceIdsPath = (gameDataDirectory / gamedata::kSliceIdsFile).string();
    gamedata::SliceIds playerSlice;
    if (playerSlice.load(sliceIdsPath)) {
        log::write("backend: ids of the player slice loaded from %s (%zu components)",
                   sliceIdsPath.c_str(),
                   playerSlice.component_count());
    } else {
        log::write("backend: no ids of the player slice at %s: the game server does not check the target of a "
                   "component call. Make the file with tools/gamedata/slice_ids.sh",
                   sliceIdsPath.c_str());
    }
    // The item catalog: the index of each item on the wire, and the values of a new item.
    const std::string itemCatalogPath = (gameDataDirectory / gamedata::kItemCatalogFile).string();
    gamedata::ItemCatalog itemCatalog;
    if (itemCatalog.load(itemCatalogPath)) {
        log::write("backend: item catalog loaded from %s (%zu items, the item list of the client has %u entries, "
                   "the items of %zu backstories)",
                   itemCatalogPath.c_str(),
                   itemCatalog.item_count(),
                   itemCatalog.list_size(),
                   itemCatalog.backstory_count());
        if (itemCatalog.backstory_count() == 0 || !itemCatalog.has_creation_backstories()) {
            log::write("backend: the item catalog has no backstory, or no list of the backstories of the character "
                       "creation: a new character gets no clothing. Make the file again with tools/gamedata/items.sh");
        }
    } else {
        log::write("backend: no item catalog at %s: the backend gives no item, and each bag is empty for a client. "
                   "Make the file with tools/gamedata/items.sh",
                   itemCatalogPath.c_str());
    }
    // The progression tables: the rules of the level, the attributes, and the skills.
    const std::string progressionTablesPath = (gameDataDirectory / gamedata::kProgressionTablesFile).string();
    gamedata::ProgressionTables progressionTables;
    if (progressionTables.load(progressionTablesPath)) {
        log::write("backend: progression tables loaded from %s (%zu levels, the largest level of a player is %u, %zu "
                   "weapon masteries and trade skills, %zu nodes of the weapon trees)",
                   progressionTablesPath.c_str(),
                   progressionTables.data().levels.size(),
                   progressionTables.level_cap(),
                   progressionTables.progressions().size(),
                   progressionTables.nodes().size());
    } else {
        log::write("backend: no progression tables at %s: the backend sends the stored level and skills of a "
                   "character, and it gives no level, no attribute point, and no mastery point. Make the file with "
                   "tools/gamedata/progression.sh",
                   progressionTablesPath.c_str());
    }
    // The creature data: the spawn points of the creatures and the record data of each kind.
    const std::string creatureDataPath = (gameDataDirectory / gamedata::kCreatureDataFile).string();
    gamedata::CreatureData creatureData;
    if (creatureData.load(creatureDataPath)) {
        log::write("backend: creature data loaded from %s (%zu spawn points, %zu kinds, %zu slices)",
                   creatureDataPath.c_str(),
                   creatureData.spawn_points().size(),
                   creatureData.kinds().size(),
                   creatureData.slices().size());
    } else {
        log::write("backend: no creature data at %s (%s): the world has no creature. Make the file with "
                   "tools/gamedata/creatures.sh",
                   creatureDataPath.c_str(),
                   creatureData.error().c_str());
    }
    // The combat tables: the damage of an attack of a player on a creature.
    const std::string combatTablesPath = (gameDataDirectory / gamedata::kCombatTablesFile).string();
    gamedata::CombatTables combatTables;
    if (combatTables.load(combatTablesPath)) {
        log::write("backend: combat tables loaded from %s (%zu items with a weapon, %zu weapons, %zu damage rows, the "
                   "defense values of %zu vitals rows)",
                   combatTablesPath.c_str(),
                   combatTables.data().items.size(),
                   combatTables.data().weapons.size(),
                   combatTables.data().damageRows.size(),
                   combatTables.data().vitals.size());
    } else {
        log::write("backend: no combat tables at %s (%s): an attack of a player on a creature has no effect. Make the "
                   "file with tools/gamedata/combat.sh",
                   combatTablesPath.c_str(),
                   combatTables.error().c_str());
    }
    // The travel places: the places of the travel command.
    const std::string travelPlacesPath = (gameDataDirectory / gamedata::kTravelPlacesFile).string();
    gamedata::TravelPlaces travelPlaces;
    if (travelPlaces.load(travelPlacesPath)) {
        log::write("backend: travel places loaded from %s (%zu places in %zu territories)",
                   travelPlacesPath.c_str(),
                   travelPlaces.places().size(),
                   travelPlaces.territory_count());
    } else {
        log::write("backend: no travel places at %s (%s): the travel command takes only a position in numbers. Make "
                   "the file with tools/gamedata/places.sh",
                   travelPlacesPath.c_str(),
                   travelPlaces.error().c_str());
    }
    // The readers of the game data files are done: the memory of the parsed documents (the
    // creature data of the whole world is a file of some megabytes) goes back to the system.
    release_free_memory();
    // The local ticket of the game hook names the Steam account of a login. No code proves it.
    log::write("backend: stub authentication, the Steam ticket of a login is not validated");
    // One thread owns the world. With the game server listener, it is the listener thread of
    // the DTLS server: each tick of the game server is one tick of the world. With no
    // listener, the world has its own thread, so that a task of an HTTP thread runs.
    world::SystemClock clock;
    world::WorldOptions worldOptions;
    worldOptions.itemCatalog = &itemCatalog;
    worldOptions.progressionTables = &progressionTables;
    worldOptions.creatureData = &creatureData;
    worldOptions.combatTables = &combatTables;
    worldOptions.travelPlaces = &travelPlaces;
    world::World world(clock, store, appearanceTables, worldOptions);
    const server::http::Services services{store,
                                          options.repAddress,
                                          options.worldVersion,
                                          options.seedCharacter,
                                          appearanceTables,
                                          world};

    service::TlsIdentity identity;
    if (!service::load_or_create_identity((dataDirectory / "tls").string(), identity)) {
        return 1;
    }

    server::http::Router router;
    // The game hook reads the local certificate here and adds it to the client trust list.
    router.add("GET", "", "/v1/tls/ca.pem", "management.tls_ca", [&identity](const server::http::RouteContext& context) {
        service::HttpResponse response;
        if (!context.request.host.empty()) {
            return server::http::json_reply(403, R"({"message":"forbidden"})");
        }
        response.contentType = "application/x-pem-file";
        response.body = identity.certificatePem;
        return response;
    });
    router.add("GET", "", "/healthz", "management.health", [](const server::http::RouteContext&) {
        return server::http::json_reply(200, R"({"status":"ok"})");
    });
    router.add("POST", "", "/v1/shutdown", "management.shutdown", [](const server::http::RouteContext& context) {
        // Game routes carry a service host. The management route does not accept them.
        if (!context.request.host.empty()) {
            return server::http::json_reply(403, R"({"message":"forbidden"})");
        }
        g_stop = true;
        return server::http::json_reply(200, R"({"status":"stopping"})");
    });
    server::http::register_channel_routes(router);
    server::http::register_omni_routes(router, services);
    server::http::register_gateway_routes(router, services);
    server::http::AwsOptions awsOptions;
    awsOptions.meleeReport = options.meleeReport;
    server::http::register_aws_routes(router, awsOptions);
    if (options.meleeReport) {
        log::write("backend: the melee contact report of the client is on (the remote configuration document %s)",
                   server::http::kGameplayConfigurationPath);
    } else {
        log::write("backend: the melee contact report of the client is OFF (--no-melee-report): the client gets no "
                   "remote configuration document, and a melee attack of a player has no effect");
    }
    if (options.devCommands) {
        server::http::register_dev_routes(router, services);
        log::write("backend: DEVELOPMENT COMMANDS are on (routes /v1/dev/world, /v1/dev/items, /v1/dev/progress, and "
                   "/v1/dev/creatures)");
    }
    // The admin interface of the tool window: each start has it when the launcher gave a token.
    server::http::AdminAccess adminAccess;
    log::write("%s", read_admin_access(adminAccess).c_str());
    if (adminAccess.enabled()) {
        server::http::register_admin_routes(router, services, adminAccess);
    }

    service::HttpServer httpServer;
    const auto handler = [&router](const service::HttpRequest& request) {
        return server::http::handle_request(router, request);
    };
    if (!httpServer.start(options.httpHost, options.httpPort, handler)) {
        log::write("backend: HTTP listener failed on %s:%u", options.httpHost.c_str(), options.httpPort);
        return 1;
    }
    service::DtlsServer dtlsServer;
    service::RepServerOptions repOptions;
    repOptions.serverVersion = options.worldVersion;
    repOptions.captureDirectory = options.capture ? (dataDirectory / "captures" / "rep").string() : std::string{};
    repOptions.research = options.research;
    repOptions.observePlayers = options.observePlayers;
    repOptions.playerSlice = &playerSlice;
    if (options.observePlayers) {
        log::write("backend: each session observes the other characters near its character");
    }
    service::RepServer repServer(dtlsServer, world, repOptions);
    const std::size_t repColon = options.repAddress.rfind(':');
    const std::string repHost = options.repAddress.substr(0, repColon);
    const auto repPort = static_cast<std::uint16_t>(
        repColon == std::string::npos ? 0 : std::strtoul(options.repAddress.c_str() + repColon + 1, nullptr, 10));
    if (!options.repListener) {
        log::write("backend: game server listener is off");
        world.start_thread();
    } else if (repHost.rfind("127.", 0) != 0 || repPort == 0
               || !dtlsServer.start(repHost, repPort, identity, &repServer)) {
        log::write("backend: game server listener failed on %s", options.repAddress.c_str());
        return 1;
    }
    log::write("backend: ready on http://%s:%u, game server udp %s",
               options.httpHost.c_str(),
               options.httpPort,
               options.repAddress.c_str());

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    while (!g_stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    // The HTTP listener first: its stop waits for each connection thread, and a route can
    // wait for a task of the world. The world thread runs until that stop is complete.
    httpServer.stop();
    dtlsServer.stop();
    world.stop_thread();
    // The listener thread ended: the world has no world thread now.
    world.forget_thread();
    // No thread calls the game server now. The world saves the position of each character.
    repServer.stop();
    log::write("backend: stopped");
    return 0;
}

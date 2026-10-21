#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "gamedata/ability_data.h"
#include "gamedata/appearance_tables.h"
#include "gamedata/combat_tables.h"
#include "gamedata/creature_data.h"
#include "gamedata/height_maps.h"
#include "gamedata/chest_data.h"
#include "gamedata/gathering_data.h"
#include "gamedata/interaction_data.h"
#include "gamedata/item_catalog.h"
#include "gamedata/perk_data.h"
#include "gamedata/projectile_data.h"
#include "gamedata/stamina_data.h"
#include "gamedata/weapon_actions.h"
#include "gamedata/spell_data.h"
#include "gamedata/loot_tables.h"
#include "gamedata/progression_tables.h"
#include "gamedata/quest_data.h"
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
    /**
     * The navigation data of the world. It has no file until a case sets its folder: a creature
     * then walks on the straight line on the terrain. The world thread reads the tiles itself
     * (no thread of the navigation): each tick of a case gives the same result.
     */
    gamedata::NavigationData navigation;
    /** The loot tables of the world. They are empty until a case sets them: a kill then gives nothing. */
    gamedata::LootTables loot;
    /** The ability data of the world (stage 3.3a). It is empty until a case sets it: each ability press is then refused. */
    gamedata::AbilityData abilities;
    /** The perk data of the world (stage 1.3b). It is empty until a case sets it: an item then has no perk with an effect. */
    gamedata::PerkData perks;
    /** The spell entity data (stage 3.3b-S2a). It is empty until a case sets it: a spell then has its hits and no record. */
    gamedata::SpellData spells;
    /** The projectile data (stage 3.3b-S3). It is empty until a case sets it: each shot is then refused. */
    gamedata::ProjectileData projectiles;
    /** The weapon actions (stage 3.3b-M1). They are empty until a case sets them: a melee hit then uses the rule of stage 3.2a. */
    gamedata::WeaponActions weaponActions;
    /** The interaction data (stage 2.5a). It is empty until a case sets it: each request of an interaction is then refused. */
    gamedata::InteractionData interactions;
    /** The stamina data (stage 3.4f-B). It is empty until a case sets it: a character then keeps its full stamina and has no dodge rule. */
    gamedata::StaminaData stamina;
    /** The chest data (stage 3.5b.1). It is empty until a case sets it: each open of a chest is then refused. */
    gamedata::ChestData chests;
    /** The gathering data (stage 3.6a). It is empty until a case sets it: each gather is then refused. */
    gamedata::GatheringData gathering;
    /** The crafting data (stage 3.7a). It is empty until a case sets it: each craft request is then refused. */
    gamedata::CraftingData crafting;
    /** The quest data (stage 1.8a). It is empty until a case loads it: no quest, and no quest state. */
    gamedata::QuestData quests;
    world::World world{clock, store, tables,
                       with_quests(with_crafting(with_gathering(with_chests(with_stamina(with_interactions(with_game_data(catalog, progression, creatures,
                                                                                                              combat, places, terrain, navigation,
                                                                                                              loot, abilities, perks, spells,
                                                                                                              projectiles, weaponActions),
                                                                                               interactions),
                                                                             stamina),
                                                                chests),
                                                    gathering),
                                     crafting),
                                     quests)};
    FakeTransport transport;
    std::unique_ptr<service::RepServer> server;
    /** Time of the last datagram that each peer sent. */
    std::map<std::uint64_t, world::WorldClock::time_point> lastDatagram;

    static world::WorldOptions with_game_data(const gamedata::ItemCatalog& catalog,
                                              const gamedata::ProgressionTables& progression) {
        world::WorldOptions options;
        options.itemCatalog = &catalog;
        options.progressionTables = &progression;
        // A case of a different rule starts its fight directly after the world entry or a
        // travel: no shelter time. The cases of the shelter rule set the time of the product.
        options.shelterTime = std::chrono::milliseconds(0);
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

    static world::WorldOptions with_game_data(const gamedata::ItemCatalog& catalog,
                                              const gamedata::ProgressionTables& progression,
                                              const gamedata::CreatureData& creatures,
                                              const gamedata::CombatTables& combat,
                                              const gamedata::TravelPlaces& places,
                                              gamedata::Terrain& terrain,
                                              const gamedata::NavigationData& navigation) {
        world::WorldOptions options = with_game_data(catalog, progression, creatures, combat, places, terrain);
        options.navigationData = &navigation;
        return options;
    }

    static world::WorldOptions with_game_data(const gamedata::ItemCatalog& catalog,
                                              const gamedata::ProgressionTables& progression,
                                              const gamedata::CreatureData& creatures,
                                              const gamedata::CombatTables& combat,
                                              const gamedata::TravelPlaces& places,
                                              gamedata::Terrain& terrain,
                                              const gamedata::NavigationData& navigation,
                                              const gamedata::LootTables& loot) {
        world::WorldOptions options = with_game_data(catalog, progression, creatures, combat, places, terrain, navigation);
        options.lootTables = &loot;
        return options;
    }
    static world::WorldOptions with_game_data(const gamedata::ItemCatalog& catalog,
                                              const gamedata::ProgressionTables& progression,
                                              const gamedata::CreatureData& creatures,
                                              const gamedata::CombatTables& combat,
                                              const gamedata::TravelPlaces& places,
                                              gamedata::Terrain& terrain,
                                              const gamedata::NavigationData& navigation,
                                              const gamedata::LootTables& loot,
                                              const gamedata::AbilityData& abilities) {
        world::WorldOptions options = with_game_data(catalog, progression, creatures, combat, places, terrain, navigation, loot);
        options.abilityData = &abilities;
        return options;
    }
    static world::WorldOptions with_game_data(const gamedata::ItemCatalog& catalog,
                                              const gamedata::ProgressionTables& progression,
                                              const gamedata::CreatureData& creatures,
                                              const gamedata::CombatTables& combat,
                                              const gamedata::TravelPlaces& places,
                                              gamedata::Terrain& terrain,
                                              const gamedata::NavigationData& navigation,
                                              const gamedata::LootTables& loot,
                                              const gamedata::AbilityData& abilities,
                                              const gamedata::PerkData& perks) {
        world::WorldOptions options = with_game_data(catalog, progression, creatures, combat, places, terrain, navigation, loot, abilities);
        options.perkData = &perks;
        return options;
    }
    static world::WorldOptions with_game_data(const gamedata::ItemCatalog& catalog,
                                              const gamedata::ProgressionTables& progression,
                                              const gamedata::CreatureData& creatures,
                                              const gamedata::CombatTables& combat,
                                              const gamedata::TravelPlaces& places,
                                              gamedata::Terrain& terrain,
                                              const gamedata::NavigationData& navigation,
                                              const gamedata::LootTables& loot,
                                              const gamedata::AbilityData& abilities,
                                              const gamedata::PerkData& perks,
                                              const gamedata::SpellData& spells) {
        world::WorldOptions options =
            with_game_data(catalog, progression, creatures, combat, places, terrain, navigation, loot, abilities, perks);
        options.spellData = &spells;
        return options;
    }
    static world::WorldOptions with_game_data(const gamedata::ItemCatalog& catalog,
                                              const gamedata::ProgressionTables& progression,
                                              const gamedata::CreatureData& creatures,
                                              const gamedata::CombatTables& combat,
                                              const gamedata::TravelPlaces& places,
                                              gamedata::Terrain& terrain,
                                              const gamedata::NavigationData& navigation,
                                              const gamedata::LootTables& loot,
                                              const gamedata::AbilityData& abilities,
                                              const gamedata::PerkData& perks,
                                              const gamedata::SpellData& spells,
                                              const gamedata::ProjectileData& projectiles) {
        world::WorldOptions options =
            with_game_data(catalog, progression, creatures, combat, places, terrain, navigation, loot, abilities, perks, spells);
        options.projectileData = &projectiles;
        return options;
    }
    static world::WorldOptions with_game_data(const gamedata::ItemCatalog& catalog,
                                              const gamedata::ProgressionTables& progression,
                                              const gamedata::CreatureData& creatures,
                                              const gamedata::CombatTables& combat,
                                              const gamedata::TravelPlaces& places,
                                              gamedata::Terrain& terrain,
                                              const gamedata::NavigationData& navigation,
                                              const gamedata::LootTables& loot,
                                              const gamedata::AbilityData& abilities,
                                              const gamedata::PerkData& perks,
                                              const gamedata::SpellData& spells,
                                              const gamedata::ProjectileData& projectiles,
                                              const gamedata::WeaponActions& weaponActions) {
        world::WorldOptions options =
            with_game_data(catalog, progression, creatures, combat, places, terrain, navigation, loot, abilities, perks, spells, projectiles);
        options.weaponActions = &weaponActions;
        return options;
    }

    static world::WorldOptions with_interactions(world::WorldOptions options, const gamedata::InteractionData& interactions) {
        options.interactionData = &interactions;
        return options;
    }
    static world::WorldOptions with_stamina(world::WorldOptions options, const gamedata::StaminaData& stamina) {
        options.staminaData = &stamina;
        return options;
    }
    static world::WorldOptions with_chests(world::WorldOptions options, const gamedata::ChestData& chests) {
        options.chestData = &chests;
        return options;
    }
    static world::WorldOptions with_gathering(world::WorldOptions options, const gamedata::GatheringData& gathering) {
        options.gatheringData = &gathering;
        return options;
    }
    static world::WorldOptions with_crafting(world::WorldOptions options, const gamedata::CraftingData& crafting) {
        options.craftingData = &crafting;
        return options;
    }
    static world::WorldOptions with_quests(world::WorldOptions options, const gamedata::QuestData& quests) {
        options.questData = &quests;
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
    /** A host with a database file (stage 2.5a: a new start of the backend with the same database). */
    ServerHost(const service::RepServerOptions& options, const std::string& database) {
        start(options, database);
    }

    void start(const service::RepServerOptions& options, const std::string& database = ":memory:") {
        if (!store.open(database)) {
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

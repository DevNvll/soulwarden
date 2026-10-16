#include <algorithm>
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
#include "gamedata/ability_data.h"
#include "gamedata/projectile_data.h"
#include "gamedata/weapon_actions.h"
#include "gamedata/spell_data.h"
#include "gamedata/loot_tables.h"
#include "gamedata/creature_data.h"
#include "gamedata/height_maps.h"
#include "gamedata/item_catalog.h"
#include "gamedata/perk_data.h"
#include "gamedata/navigation_data.h"
#include "gamedata/progression_tables.h"
#include "gamedata/slice_ids.h"
#include "gamedata/interaction_data.h"
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
    /** The creatures have their behavior: a target, the way to it, and attacks (world/behavior.h). */
    bool creatureBehavior{true};
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
        } else if (argument == "--no-creature-behavior") {
            options.creatureBehavior = false;
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
                     "[--capture] [--observe-players] [--no-melee-report] [--no-creature-behavior]\n"
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
        const gamedata::ExpansionRow* expansion = progressionTables.owned_expansion();
        log::write("backend: progression tables loaded from %s (%zu levels, the largest level of a player is %u, %zu "
                   "weapon masteries and trade skills, %zu nodes of the weapon trees)",
                   progressionTablesPath.c_str(),
                   progressionTables.data().levels.size(),
                   progressionTables.level_cap(),
                   progressionTables.progressions().size(),
                   progressionTables.nodes().size());
        if (expansion != nullptr) {
            log::write("backend: each player owns the expansion %s and the DLC packs (%zu entitlements of each account): the "
                       "largest level %u, equip gear score %u, craft gear score %u, trade skill level %u",
                       expansion->id.c_str(),
                       progressionTables.entitlements().size(),
                       expansion->maxLevel,
                       expansion->maxEquipGearScore,
                       expansion->maxCraftGearScore,
                       expansion->maxTradeSkillLevel);
        } else {
            log::write("backend: the progression tables have no expansion (a file from before stage 1.1b): the players have "
                       "the limits of the game with no expansion, and the client locks the expansion. Make the file again "
                       "with tools/gamedata/progression.sh");
        }
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
        const auto capsules = std::count_if(creatureData.slices().begin(), creatureData.slices().end(),
                                            [](const gamedata::CreatureSlice& slice) { return slice.capsule.has_value(); });
        log::write("backend: creature data loaded from %s (%zu spawn points, %zu kinds, %zu slices, %zu with the capsule of their "
                   "character%s)",
                   creatureDataPath.c_str(),
                   creatureData.spawn_points().size(),
                   creatureData.kinds().size(),
                   creatureData.slices().size(),
                   static_cast<std::size_t>(capsules),
                   capsules == 0 ? ": a file of a tool from before stage 3.3b-S3; a projectile uses the default capsule" : "");
    } else {
        log::write("backend: no creature data at %s (%s): the world has no creature. Make the file with "
                   "tools/gamedata/creatures.sh",
                   creatureDataPath.c_str(),
                   creatureData.error().c_str());
    }
    // The terrain heights: the ground of a creature that walks. The reader opens no file here:
    // it reads the tile of a place when a creature needs the height there.
    const std::string heightsPath = (gameDataDirectory / gamedata::kHeightMapsFolder).string();
    gamedata::HeightMaps heightMaps;
    if (heightMaps.set_directory(heightsPath)) {
        log::write("backend: terrain heights at %s", heightsPath.c_str());
    } else {
        log::write("backend: no terrain heights at %s (tools/gamedata/heights.sh makes the files)", heightsPath.c_str());
    }
    // The navigation data: the floors and the walls of the world for the paths and the sight of
    // the creatures. The reader reads only the header of each region file here: the world reads
    // the tiles near the players, in a thread of its own.
    const std::string navigationPath = (gameDataDirectory / gamedata::kNavigationFolder).string();
    gamedata::NavigationData navigationData;
    if (navigationData.set_directory(navigationPath)) {
        const gamedata::NavAgent& agent = navigationData.agent();
        log::write("backend: navigation data at %s: %zu region(s), cells of %.2f m (agent: radius %.2f m, height %.2f m, step %.2f m, "
                   "slope %.0f degrees, wade %.2f m). A creature goes on a path of the data and does not see through a wall",
                   navigationPath.c_str(),
                   navigationData.regions(),
                   static_cast<double>(agent.cell),
                   static_cast<double>(agent.radius),
                   static_cast<double>(agent.height),
                   static_cast<double>(agent.step),
                   static_cast<double>(agent.slope),
                   static_cast<double>(agent.wade));
    } else if (heightMaps.available()) {
        log::write("backend: no navigation data at %s: a creature walks on the straight line on the terrain height, through "
                   "each wall, and it sees through a wall. Make the files with tools/gamedata/navigation.sh",
                   navigationPath.c_str());
    } else {
        log::write("backend: no navigation data at %s and no terrain heights: no creature leaves its place. Make the files "
                   "with tools/gamedata/navigation.sh",
                   navigationPath.c_str());
    }
    if (!navigationData.problem().empty()) {
        log::write("backend: a navigation file is not correct and is not used: %s", navigationData.problem().c_str());
    }
    if (creatureData.loaded()) {
        // The numbers of the behavior data: which creatures fight.
        if (creatureData.fighting_slices() == 0) {
            log::write("backend: the creature data has no behavior data (a file of a tool from before stage 3.4b): the "
                       "creatures stand at their spawn points. Make the file again with tools/gamedata/creatures.sh");
        } else {
            log::write("backend: creature behavior data: %zu of %zu slices and %zu of %zu kinds have an attack (%zu of %zu "
                       "spawn points). A creature walks when it stands on a free floor of the navigation data",
                       creatureData.fighting_slices(),
                       creatureData.slices().size(),
                       creatureData.fighting_kinds(),
                       creatureData.kinds().size(),
                       creatureData.fighting_points(),
                       creatureData.spawn_points().size());
        }
        // The patrol and wander data (stage 3.4e): a creature roams only near a character.
        if (creatureData.routes().empty() && creatureData.wandering_kinds() == 0) {
            log::write("backend: the creature data has no patrol routes and no wander values (a file of a tool from before "
                       "stage 3.4e): the creatures stand at their places when they have no target. Make the file again with "
                       "tools/gamedata/creatures.sh");
        } else {
            log::write("backend: creature patrol data: %zu routes for %zu of %zu spawn points, %zu kinds that wander",
                       creatureData.routes().size(),
                       creatureData.routed_points(),
                       creatureData.spawn_points().size(),
                       creatureData.wandering_kinds());
        }
    }
    if (options.creatureBehavior) {
        log::write("backend: the behavior of the creatures is on");
    } else {
        log::write("backend: the behavior of the creatures is OFF (--no-creature-behavior): the creatures stand at their "
                   "spawn points");
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
        if (combatTables.data().armor.empty()) {
            log::write("backend: the combat tables have no armor values (a file of a tool from before stage 3.4b): a "
                       "character has no armor against an attack of a creature. Make the file again with "
                       "tools/gamedata/combat.sh");
        } else {
            log::write("backend: armor values of %zu items, and %zu level differences with a damage value",
                       combatTables.data().armor.size(),
                       combatTables.data().levelDamage.size());
        }
        const std::size_t impacts = static_cast<std::size_t>(
            std::count_if(combatTables.data().damageRows.begin(), combatTables.data().damageRows.end(),
                          [](const gamedata::DamageRow& row) { return row.impact.has_value(); }));
        if (impacts == 0) {
            log::write("backend: the combat tables have no impacts (a file of a tool from before stage 3.3b-S4): a hit "
                       "moves no creature. Make the file again with tools/gamedata/combat.sh");
        } else {
            log::write("backend: %zu damage rows with an impact (the pull and the push of a hit)", impacts);
        }
    } else {
        log::write("backend: no combat tables at %s (%s): an attack of a player on a creature has no effect. Make the "
                   "file with tools/gamedata/combat.sh",
                   combatTablesPath.c_str(),
                   combatTables.error().c_str());
    }
    // The loot tables and the kill rewards: the experience, the mastery, the loot, and the coin of a kill.
    const std::string lootTablesPath = (gameDataDirectory / gamedata::kLootTablesFile).string();
    gamedata::LootTables lootTables;
    if (lootTables.load(lootTablesPath)) {
        std::size_t masteries = 0;
        for (const gamedata::WeaponRow& weapon : combatTables.data().weapons) {
            masteries += weapon.mastery.empty() ? 0 : 1;
        }
        log::write("backend: loot tables loaded from %s (%zu reward levels, %zu creature types, %zu vitals rows, %zu loot "
                   "tables, %zu loot buckets; %zu weapons with a mastery in the combat tables)",
                   lootTablesPath.c_str(),
                   lootTables.data().rewards.size(),
                   lootTables.data().modifiers.size(),
                   lootTables.data().vitals.size(),
                   lootTables.data().tables.size(),
                   lootTables.data().buckets.size(),
                   masteries);
        if (masteries == 0) {
            log::write("backend: the combat tables have no weapon mastery (a file of a tool from before stage 3.5a): a "
                       "kill gives no mastery experience. Make the file again with tools/gamedata/combat.sh");
        }
    } else {
        log::write("backend: no loot tables at %s (%s): a kill gives nothing. Make the file with tools/gamedata/loot.sh",
                   lootTablesPath.c_str(),
                   lootTables.error().c_str());
    }
    // The ability data (stage 3.3a): the cooldowns, the status effects, and the spells of the abilities.
    const std::string abilityDataPath = (gameDataDirectory / gamedata::kAbilityDataFile).string();
    gamedata::AbilityData abilityData;
    if (abilityData.load(abilityDataPath)) {
        log::write("backend: ability data loaded from %s (%zu cooldowns, %zu status effects, %zu spells)",
                   abilityDataPath.c_str(),
                   abilityData.tables().cooldowns.size(),
                   abilityData.tables().statusEffects.size(),
                   abilityData.tables().spells.size());
    } else {
        log::write("backend: no ability data at %s (%s): each ability press is refused. Make the file with "
                   "tools/gamedata/abilities.sh",
                   abilityDataPath.c_str(),
                   abilityData.error().c_str());
    }
    // The spell entity data (stage 3.3b-S2a): the slices of the spells, for the record of a spell entity.
    const std::string spellDataPath = (gameDataDirectory / gamedata::kSpellDataFile).string();
    gamedata::SpellData spellData;
    if (spellData.load(spellDataPath)) {
        log::write("backend: spell data loaded from %s (%zu spells, %zu slices)",
                   spellDataPath.c_str(),
                   spellData.tables().spells.size(),
                   spellData.tables().slices.size());
    } else {
        log::write("backend: no spell data at %s (%s): a spell (the vortex of Gravity Well) has its hits and no entity that a client "
                   "sees. Make the file with tools/gamedata/spells.sh",
                   spellDataPath.c_str(),
                   spellData.error().c_str());
    }
    // The projectile data (stage 3.3b-S3): the ranged attack profiles, the projectile slices, and the shots of the basic attacks.
    const std::string projectileDataPath = (gameDataDirectory / gamedata::kProjectileDataFile).string();
    gamedata::ProjectileData projectileData;
    if (projectileData.load(projectileDataPath)) {
        log::write("backend: projectile data loaded from %s (%zu ranged attacks, %zu projectile slices, %zu weapons with a profile, %zu "
                   "shots of basic attacks)",
                   projectileDataPath.c_str(),
                   projectileData.tables().attacks.size(),
                   projectileData.tables().slices.size(),
                   projectileData.tables().weapons.size(),
                   projectileData.tables().shots.size());
    } else {
        log::write("backend: no projectile data at %s (%s): each shot of a bow, a musket, or a magic weapon is refused. Make the "
                   "file with tools/gamedata/projectiles.sh",
                   projectileDataPath.c_str(),
                   projectileData.error().c_str());
    }
    // The perk data (stage 1.3b): the perks of an item instance and their effects.
    const std::string perkDataPath = (gameDataDirectory / gamedata::kPerkDataFile).string();
    gamedata::PerkData perkData;
    if (perkData.load(perkDataPath)) {
        log::write("backend: perk data loaded from %s (%u entries of the perk list, %zu perk buckets, %zu items with perk "
                   "places, %zu consumables)",
                   perkDataPath.c_str(),
                   perkData.list_size(),
                   perkData.tables().buckets.size(),
                   perkData.tables().items.size(),
                   perkData.tables().consumables.size());
    } else {
        log::write("backend: no perk data at %s (%s): an item has no perk, no perk has an effect, and no item can be used. "
                   "Make the file with tools/gamedata/perks.sh",
                   perkDataPath.c_str(),
                   perkData.error().c_str());
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
    // The interaction objects of the world (stage 2.5a): the lore pages, the persons, and each other object of the level data
    // that a player can use. With no file the backend refuses each request of an interaction.
    const std::string interactionDataPath = (gameDataDirectory / gamedata::kInteractionDataFile).string();
    gamedata::InteractionData interactionData;
    if (interactionData.load(interactionDataPath)) {
        log::write("backend: interaction data loaded from %s (%zu objects with %zu bounds, %zu options, %zu achievements)",
                   interactionDataPath.c_str(), interactionData.objects().size(), interactionData.bound_count(),
                   interactionData.options().size(), interactionData.achievements().size());
    } else {
        log::write("backend: no interaction data at %s (%s): each request of an interaction is refused, and a read lore page is "
                   "not kept. Make the file with tools/gamedata/interactions.sh",
                   interactionDataPath.c_str(), interactionData.error().c_str());
    }
    // The readers of the game data files are done: the memory of the parsed documents (the
    // creature data of the whole world is a file of some megabytes) goes back to the system.
    release_free_memory();
    // The local ticket of the game hook names the Steam account of a login. No code proves it.
    log::write("backend: stub authentication, the Steam ticket of a login is not validated");
    // One thread owns the world. With the game server listener, it is the listener thread of
    // the DTLS server: each tick of the game server is one tick of the world. With no
    // The basic attacks of the melee weapons (stage 3.3b-M1): the attack graph of each weapon, its strikes and their damage rows.
    const std::string weaponActionsPath = (gameDataDirectory / gamedata::kWeaponActionsFile).string();
    gamedata::WeaponActions weaponActions;
    if (weaponActions.load(weaponActionsPath)) {
        std::size_t fragments = 0;
        for (const gamedata::WeaponActionSet& weapon : weaponActions.tables().weapons) {
            fragments += weapon.fragments.size();
        }
        log::write("backend: weapon actions loaded from %s (%zu melee weapon tag sets, %zu fragments)", weaponActionsPath.c_str(),
                   weaponActions.tables().weapons.size(), fragments);
    } else {
        log::write("backend: no weapon actions at %s (%s): each melee hit uses the light or the heavy row of its weapon (the rule of "
                   "stage 3.2a). Make the file with tools/gamedata/weapon-actions.sh",
                   weaponActionsPath.c_str(),
                   weaponActions.error().c_str());
    }
    // listener, the world has its own thread, so that a task of an HTTP thread runs.
    world::SystemClock clock;
    world::WorldOptions worldOptions;
    worldOptions.itemCatalog = &itemCatalog;
    worldOptions.progressionTables = &progressionTables;
    worldOptions.creatureData = &creatureData;
    worldOptions.combatTables = &combatTables;
    worldOptions.travelPlaces = &travelPlaces;
    worldOptions.interactionData = &interactionData;
    worldOptions.lootTables = &lootTables;
    worldOptions.abilityData = &abilityData;
    worldOptions.spellData = &spellData;
    worldOptions.projectileData = &projectileData;
    worldOptions.weaponActions = &weaponActions;
    worldOptions.perkData = &perkData;
    worldOptions.terrain = heightMaps.available() ? &heightMaps : nullptr;
    worldOptions.navigationData = &navigationData;
    worldOptions.navigationThread = true;
    worldOptions.creatureBehavior = options.creatureBehavior;
    // The random numbers of the world (the wander of the creatures): a different sequence in each start.
    worldOptions.randomSeed = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
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
    server::http::register_entitlement_routes(router, services);
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

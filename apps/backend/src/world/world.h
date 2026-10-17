#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "core/logging/log.h"
#include "gamedata/ability_data.h"
#include "gamedata/appearance_tables.h"
#include "gamedata/combat_tables.h"
#include "gamedata/creature_data.h"
#include "gamedata/item_catalog.h"
#include "gamedata/perk_data.h"
#include "gamedata/progression_tables.h"
#include "gamedata/travel_places.h"
#include "state/store.h"
#include "world/abilities.h"
#include "world/combat.h"
#include "world/consumables.h"
#include "world/creatures.h"
#include "world/entity.h"
#include "world/gear.h"
#include "world/health.h"
#include "world/ids.h"
#include "world/chests.h"
#include "world/gathering.h"
#include "world/loot_bags.h"
#include "world/interactions.h"
#include "world/inventory.h"
#include "world/load_meter.h"
#include "world/navigation.h"
#include "world/ownership.h"
#include "world/passives.h"
#include "world/position_index.h"
#include "world/position_report.h"
#include "world/progress.h"
#include "world/rewards.h"
#include "world/runes.h"
#include "world/skills.h"
#include "world/projectiles.h"
#include "world/spells.h"
#include "world/stamina.h"
#include "world/statuses.h"
#include "world/world_clock.h"
#include "world/world_time.h"

// The world object. It has the clock of the world, the task queue, the timers, and the entity
// table: each character that is in the world is one entity, with the character id as its key
// (roadmap, rule MP-1). The world gives each entity its ids, keeps its position in a position
// index, and writes its position row. The creatures of the world are entities of the same
// table, with no character and no session (world/creatures.h).
//
// A session of the game server is one observer of the world and the owner of one entity. The
// world knows a session only by its id and by the SessionSink interface.
//
// One thread owns the world: the world thread. In the backend program this is the listener
// thread of the game server (RepServer::on_tick calls World::tick). With no listener, the
// world starts its own thread (start_thread). A different thread (an HTTP route, an admin
// command) never reads or changes world state: it gives the world a task (post, call).

namespace soulwarden::backend::world {

/** How the client of a character shows a travel (World::travel). */
enum class TravelScreen {
    /** The session decides from the distance: a loading screen for a place that is far. */
    automatic,
    /** No loading screen: the movement state only. */
    off,
    /** A loading screen for each distance. */
    on,
};

/** One travel of a character: what the session of its owner gets (SessionSink::traveled). */
struct Travel {
    state::WorldPosition from;
    state::WorldPosition to;
    /** The distance of the two places, in meters. */
    float distance{0.0F};
    TravelScreen screen{TravelScreen::automatic};
};

enum class TravelError {
    none,
    /** The character has no entity: it is not in the world. */
    notInWorld,
    /** The place is not a place for a character (travel_position). */
    badPosition,
    /** The database did not accept the position row of the character. Nothing changed: the character is at its place. */
    storage,
};

struct TravelResult {
    TravelError error{TravelError::none};
    /** The entity of the character. Null when `error` is not `none`. */
    Entity* entity{nullptr};
    float distance{0.0F};
};

/** Why a session lost its character. */
enum class LostCause {
    /** A newer session registered the character (a character is in one session at most). */
    replaced,
    /** The character was deleted. */
    deleted,
};

/** What the world needs from a session. The game server session implements it. */
class SessionSink {
public:
    /**
     * The world took the character of this session away. The session tells its client and
     * ends. The world calls it one time, and the session is not the owner of an entity after
     * the call.
     */
    virtual void character_lost(LostCause cause) = 0;

    /**
     * The world moved the character of this session to a different place (World::travel). The
     * session tells its client what the client needs for the change of the place, and it
     * starts the check of the position reports of its client again from the new place. The
     * movement state of the entity has the new position and a new teleport number: it goes
     * out with the next record of the session. A sink with no client does nothing.
     */
    virtual void traveled(const Travel& travel) {
        (void)travel;
    }

    /**
     * @return The teleport number that the client of this session has for its character: the
     *         number of the last full movement state that the session sent to it, and 0 before
     *         the first one (the start number of a client). World::travel counts the next
     *         number from it. No value for a sink with no client.
     */
    [[nodiscard]] virtual std::optional<std::uint8_t> client_teleport() const {
        return std::nullopt;
    }

    /**
     * The character of this session did hurt an entity (world/combat.h). The session tells
     * its client, which shows the damage number at the target. The world calls it in the
     * claim of the session, after the change of the target. A sink with no client does nothing.
     */
    virtual void damage_dealt(const DamageDealt& damage) {
        (void)damage;
    }

    /**
     * The character of this session took damage (world/health.h). The session tells its
     * client, which shows a mark with the direction of the damage. The health of the character
     * goes out with its vitals state. A sink with no client does nothing.
     */
    virtual void damage_taken(const DamageTaken& damage) {
        (void)damage;
    }

    /**
     * The character of this session healed a different character (stage 3.3b-S6, world/health.h): the session tells its client the
     * gain (message 4298, VitalsComponentClientFacet_OnHealedPlayer; live check L3: a green number at the healed character). The
     * health of the target goes out with its vitals state. A sink with no client does nothing.
     */
    virtual void healed_player(EntityId target, float amount) {
        (void)target;
        (void)amount;
    }

    /**
     * The character of this session died (world/health.h). The session tells its client, which
     * shows the death screen after `respawnSeconds`. The dead mark goes out with the vitals
     * state of the character. A sink with no client does nothing.
     */
    virtual void died(float respawnSeconds) {
        (void)respawnSeconds;
    }

    /**
     * The world ended the committed interaction of the character of this session (stage 2.5a, world/interactions.h: a command,
     * the death): the session tells its client (message 3746; the client then sends 2473). A sink with no client does nothing.
     */
    virtual void interaction_cancelled() {}

    /**
     * Development aid (tools/world message): the session sends its client one message with a
     * type and a body of the caller. It is for a live check of a message that has no code yet.
     * A sink with no client does nothing.
     */
    virtual void development_message(std::uint32_t type, const std::vector<std::uint8_t>& body) {
        (void)type;
        (void)body;
    }

protected:
    ~SessionSink() = default;
};

enum class EnterError {
    none,
    /** The store has no character with this id. */
    unknownCharacter,
    /** The character is a character of a different account. */
    wrongAccount,
};

struct EnterResult {
    EnterError error{EnterError::none};
    /** The entity of the character. Null when `error` is not `none`. */
    Entity* entity{nullptr};
    /** The session that had the character and lost it. 0: no session had it. */
    SessionId replaced{0};
};

enum class SpawnError {
    none,
    unknownCharacter,
    /** The character has an entity already. */
    inWorld,
    /** The position is not a position of the world (world/position_index.h). */
    badPosition,
};

/** The load of the backend: the real time of its ticks (world/load_meter.h). */
struct WorldLoad {
    /** One tick of the world: the tasks, the timers, the creature system, and the position saves. */
    LoadMeter worldTick;
    /** One tick of the game server: the tick of the world and the tick of each session. */
    LoadMeter serverTick;
};

/** What became of a task that a different thread gave to the world (World::call). */
enum class CallStatus {
    /** The task ran, and the caller has its value. */
    done,
    /** The task did not start in the time limit. It was cancelled: it will not run, and nothing changed. */
    notStarted,
    /** The task started and stopped with an exception. A part of its change can be in the world. */
    failed,
};

struct WorldOptions {
    /**
     * Interest distances (horizontal, meters). A session starts to observe an entity that is
     * `interestEnter` or less from its character, and it stops when the entity is more than
     * `interestLeave` away. The first value is `m_sliceSpawnRadius` of the player slice of
     * the client (100); the second one is a decision of this project.
     */
    float interestEnter{100.0F};
    float interestLeave{120.0F};
    /** The item catalog (game data). Null: the world gives no item, and each bag is empty for a client. */
    const gamedata::ItemCatalog* itemCatalog{nullptr};
    /**
     * The progression tables (game data). Null: the world sends the stored level and skills
     * of a character, and it refuses a command or a request that needs a rule of a table.
     */
    const gamedata::ProgressionTables* progressionTables{nullptr};
    /** The creature data (game data). Null: the world has no creature. */
    const gamedata::CreatureData* creatureData{nullptr};
    /** The combat tables (game data). Null: an attack of a player has no effect. */
    const gamedata::CombatTables* combatTables{nullptr};
    /** The travel places (game data). Null: the travel command takes only a position in numbers. */
    const gamedata::TravelPlaces* travelPlaces{nullptr};
    /** The loot tables and the kill rewards (game data). Null: a kill gives nothing. */
    const gamedata::LootTables* lootTables{nullptr};
    /** The ability data (game data, stage 3.3a). Null: the backend refuses each ability press. */
    const gamedata::AbilityData* abilityData{nullptr};
    /**
     * The interaction objects of the world (game data, stage 2.5a). Null or not loaded: the backend refuses each request of an
     * interaction (3743 result 2), and the lock of a conversation works as before.
     */
    const gamedata::InteractionData* interactionData{nullptr};
    /** The chests of the world (game data, stage 3.5b.1). Null or not loaded: no chest opens. */
    const gamedata::ChestData* chestData{nullptr};
    /** The gathering data (game data, stage 3.6a). Null or not loaded: each gather is refused. */
    const gamedata::GatheringData* gatheringData{nullptr};
    /** The perk data (game data, stage 1.3b). Null: an item has no perk, and no perk has an effect. */
    const gamedata::PerkData* perkData{nullptr};
    /** The spell entity data (game data, stage 3.3b-S2a). Null or not loaded: a spell entity has its hits and no record. */
    const gamedata::SpellData* spellData{nullptr};
    /** The projectile data (game data, stage 3.3b-S3). Null or not loaded: each shot of a ranged or magic basic attack is refused. */
    const gamedata::ProjectileData* projectileData{nullptr};
    /**
     * The basic attacks of the melee weapons (game data, stage 3.3b-M1). Null or not loaded: each melee hit uses the light or the
     * heavy row of the weapon table (the rule of stage 3.2a).
     */
    const gamedata::WeaponActions* weaponActions{nullptr};
    /**
     * The stamina rules of a player and the dodge (game data, stage 3.4f-B). Null or not loaded: a character keeps its full stamina,
     * and a dodge has no immune window.
     */
    const gamedata::StaminaData* staminaData{nullptr};
    /**
     * The terrain heights (game data): the ground for a creature that walks. Null: no creature
     * leaves its place. The world asks it only in the world thread.
     */
    gamedata::Terrain* terrain{nullptr};
    /**
     * The navigation data (game data): the floors and the walls of the world for the paths and
     * the sight of the creatures. Null, or an object with no file: a creature walks on the
     * straight line on the terrain height.
     */
    const gamedata::NavigationData* navigationData{nullptr};
    /**
     * True: a thread of its own reads the tiles of the navigation data (the backend). False:
     * the world thread reads a tile when it needs it (a test: each tick gives the same result).
     */
    bool navigationThread{false};
    /**
     * The shelter of a character that came to a place (world/behavior.h): the time in which
     * the vision of no creature takes it as a target, and the latest end of that time for a
     * client that reports no position. 0: no shelter (a test of a different rule).
     */
    std::chrono::milliseconds shelterTime{behavior_rules::kShelterTime};
    std::chrono::milliseconds shelterLongest{behavior_rules::kShelterLongest};
    /**
     * False: the creatures have no behavior. They stand at their spawn points, as before stage
     * 3.4b (the backend option --no-creature-behavior).
     */
    bool creatureBehavior{true};
    /**
     * The spawn point rule (horizontal, meters; a decision of this project). The world makes
     * the creature of a spawn point when a player is `creatureSpawnEnter` or less from the
     * point, and it removes the creature when no player is `creatureSpawnLeave` or less from
     * it. The first value is larger than each interest distance of a creature: a creature is
     * in the world before a session can observe it.
     */
    float creatureSpawnEnter{150.0F};
    float creatureSpawnLeave{200.0F};
    /**
     * Interest distances of a creature (horizontal, meters). A session starts to observe a
     * creature at the radius of its slice (`m_sliceSpawnRadius` of the Aoi component, game
     * data), which is not less than `creatureInterestMinimum` and not more than
     * `interestEnter`. It stops when the creature is `creatureInterestMargin` farther than
     * that. The two values are decisions of this project.
     */
    float creatureInterestMinimum{20.0F};
    float creatureInterestMargin{20.0F};
    /**
     * The start value of the random numbers of the world (World::random_unit): the places of the
     * wander of a creature, the waits at a waypoint, a waypoint of a random route. The backend
     * program gives a value of the time of its start; a test gives a value of its own.
     */
    std::uint64_t randomSeed{0x5357'4F55'4C57'4152ULL};
};

class World {
public:
    using Task = std::function<void(World&)>;

    World(const WorldClock& clock,
          state::Store& store,
          const gamedata::AppearanceTables& appearanceTables,
          WorldOptions options = {});
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    [[nodiscard]] const WorldClock& clock() const {
        return m_clock;
    }
    [[nodiscard]] const WorldOptions& options() const {
        return m_options;
    }
    /** @return The perk data (game data, stage 1.3b), or null when the backend has none. */
    [[nodiscard]] const gamedata::PerkData* perk_data() const {
        return m_options.perkData != nullptr && m_options.perkData->loaded() ? m_options.perkData : nullptr;
    }
    /**
     * Turns the behavior of the creatures on or off (WorldOptions::creatureBehavior): the start
     * option of the backend, and a test. World thread only. Off: a creature takes no target,
     * and a creature that has one keeps its values and does nothing.
     */
    void set_creature_behavior(bool on) {
        m_options.creatureBehavior = on;
    }
    /** Sets the shelter time of a character that comes to a place (WorldOptions::shelterTime): a test. World thread only. */
    void set_shelter(std::chrono::milliseconds time, std::chrono::milliseconds longest) {
        m_options.shelterTime = time;
        m_options.shelterLongest = longest;
    }
    /** Sets the start value of the random numbers (WorldOptions::randomSeed): a test. World thread only. */
    void set_random_seed(std::uint64_t seed) {
        m_random = seed;
    }
    /**
     * @return A random number from 0 (included) to 1 (not included): the one generator of the
     *         world (splitmix64). World thread only.
     */
    [[nodiscard]] float random_unit() {
        std::uint64_t value = (m_random += 0x9E3779B97F4A7C15ULL);
        value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
        value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
        value ^= value >> 31;
        return static_cast<float>(value >> 40) * (1.0F / 16777216.0F);
    }

    /** Any thread. The task runs in the world thread at the start of the next tick. */
    void post(Task task);

    /**
     * Any thread. Runs the task in the world thread and waits for its value. In the world
     * thread the task runs at once.
     *
     * The time limit is for the start of the task. A task that did not start in the limit (the
     * world has no thread that ticks, or the thread is busy) is cancelled: it does not run
     * later, and nothing changed. A task that started is the truth: the caller waits for its
     * end with no limit.
     * @param status When not null: what became of the task.
     * @return No value when the task was cancelled, or when it stopped with an exception.
     */
    template <class Result>
    [[nodiscard]] std::optional<Result> call(std::function<Result(World&)> task,
                                             std::chrono::milliseconds limit,
                                             CallStatus* status = nullptr);

    /** @return True in the thread that did the last tick. */
    [[nodiscard]] bool in_world_thread() const {
        return m_thread.load() == std::this_thread::get_id();
    }

    /**
     * World thread only. Runs the tasks of other threads, then the timers that are due, then
     * the position saves that are due. An exception of a task or of a timer goes to the log,
     * and the tick continues.
     */
    void tick();

    /**
     * World thread only. Runs the task in a tick when the world clock is `delay` later. A
     * timer that is due in the tick that adds it runs in the next tick.
     */
    void after(std::chrono::milliseconds delay, Task task);

    /**
     * Starts a thread that ticks the world. Use it only when no game server listener ticks
     * the world. @param period Real time between two ticks.
     */
    void start_thread(std::chrono::milliseconds period = std::chrono::milliseconds(10));

    /**
     * Stops the thread of start_thread. No effect when it does not run. A task of that thread
     * can call it: the thread then ends after its tick. A call from a different thread waits
     * for the end of the thread.
     */
    void stop_thread();

    /**
     * The thread that ticks the world calls it when it ticks the world no more. The world then
     * has no world thread until the next tick: an id of a thread that ended can go to a new
     * thread, and that thread must not run a task of World::call directly.
     */
    void release_thread();

    /**
     * The same for the owner of a thread that ticked the world and ended (the listener thread
     * of the game server). Call it only when that thread does not run.
     */
    void forget_thread();

    // The entity table. World thread only, from here.

    /**
     * A session enters the world with a character. The store must have the character, and
     * the character must be a character of the account `personaId`: if not, the result has
     * the error and the world does not change. A character has one entity at most. When the
     * character has an entity, the entity stays, and the session that was its owner loses it
     * (SessionSink::character_lost with the cause `replaced`): the newer session wins.
     */
    [[nodiscard]] EnterResult enter(SessionId session,
                                    SessionSink& sink,
                                    const std::string& characterId,
                                    const std::string& personaId);

    /**
     * A session ends. When it is the owner of an entity, the world writes the position of
     * the entity and removes the entity. A session that lost its character changes nothing.
     * @return True when the entity of the session was removed.
     */
    bool leave(SessionId session);

    /**
     * Puts a character of the store into the world as an entity with no session, at an exact
     * position. The position does not go to the store. Development aid, and the base of each
     * later entity that has no client. A position that is not a position of the world
     * (valid_position) gives the error `badPosition`, and the world does not change.
     */
    [[nodiscard]] SpawnError spawn_character(const std::string& characterId, const state::WorldPosition& position);

    /**
     * Removes the entity of a character. A session that owns it loses the character first.
     * The position does not go to the store.
     * @return False when the character has no entity.
     */
    bool remove_character(const std::string& characterId, LostCause cause);

    [[nodiscard]] Entity* find(EntityId id);
    /** @return The entity of a character, or null. It never gives a creature. */
    [[nodiscard]] Entity* find_character(const std::string& characterId);
    /** @return The entities of the characters of an account that are in the world (stage 1.1b: a grant of the account). */
    [[nodiscard]] std::vector<Entity*> characters_of_account(const std::string& personaId);
    /** @return The entity that the session owns, or null. */
    [[nodiscard]] Entity* entity_of(SessionId session);
    [[nodiscard]] const std::map<EntityId, Entity>& entities() const {
        return m_entities;
    }

    /**
     * A session reports the position of its character (the position field of an input report
     * of its client). The world finds the entity from the session, not from a value of the
     * report, and it checks the report (world/position_report.h): the sequence of the input
     * numbers, the numbers, and the speed. An accepted report sets the exact position of the
     * entity, with the position index and the save rule of set_position. A report that the
     * check refuses changes no value of the world.
     * @param track The values of the client of the session for the check. The session owns them.
     */
    PositionVerdict report_position(SessionId session, const PositionReport& report, PositionTrack& track);

    /**
     * Moves a character that is in the world to a different place (a command). The world
     * writes the position row of the character first. Then it sets the position of the entity,
     * gives it a teleport number, and marks its movement state. The session of the owner gets
     * SessionSink::traveled, and its client gets the new position with its next record (a
     * position of the server, which the client takes: the teleport number is not the number
     * that the client has). A session that observes the entity
     * compares its interest distances at once (last_travel): it gets the new position, or the
     * stop of the entity. The spawn point rule runs in the same tick: the creatures of the new
     * place are in the world before a session looks for them.
     *
     * A travel whose row the database did not accept changes nothing (the error `storage`):
     * the entity keeps its place and its teleport number, and the session gets nothing.
     *
     * The teleport number is one above the number that the client of the owner has
     * (SessionSink::client_teleport), from 15 to 1: each travel before the next movement state
     * of the owner has the same number. An entity with no client gets one above its own number.
     *
     * The position is a place on the ground, or some distance above it: the client lets the
     * character fall to the ground (kSpawnLift).
     */
    TravelResult travel(const std::string& characterId,
                        const state::WorldPosition& position,
                        TravelScreen screen = TravelScreen::automatic);

    /** @return The world revision of the last travel of a character. 0: none. */
    [[nodiscard]] Revision last_travel() const {
        return m_lastTravel;
    }

    /** @return The load of the backend, for the log and for the development tools. */
    [[nodiscard]] WorldLoad& load() {
        return m_load;
    }

    /**
     * Sets the position of an entity to an estimate (the camera of the owner). It changes the
     * position index and the position revision. It does not mark the movement state: the
     * client of the owner moves its character itself, and it must not get the estimate of the
     * backend as a new position.
     * @param moving True while the character moves (the save rule then needs a larger distance).
     * @return False when the world has no such entity, or when the position is not a position
     *         of the world (valid_position): a number that is not finite, or that is outside
     *         the limits. The entity then keeps its position. This applies to each source of a
     *         position: a client, a system, and a development command.
     */
    bool set_position(EntityId id, const state::WorldPosition& position, bool moving);
    /**
     * A system moves a character that no client plays (stage 3.3b-S4: the dash of an ability of a remote test character):
     * the exact position, the position index, and the save rule. A character that a client plays is not moved here: its
     * position is the position that its client reports. Its observers get the position only from the state `motion`.
     * @return False for a character with a session, a dead character, or a position that is not a position of the world.
     */
    bool move_character(Entity& entity, const state::WorldPosition& position);

    /**
     * Marks a component state of an entity as changed: each observer gets the state again.
     * @return The world revision of the change. 0: the world has no such entity.
     */
    Revision touch(EntityId id, ComponentKey key);
    /** Marks each component state of an entity as changed: each observer gets its full record again. */
    void touch_all(EntityId id);
    /**
     * Development aid: gives an entity a state fragment with bytes of the caller. Each session
     * that has the entity in its interest table sends it one time. A second fragment for the
     * same slot replaces the first one.
     */
    void set_raw_fragment(EntityId id, std::uint32_t slot, std::uint32_t type, std::vector<std::uint8_t> state);

    /**
     * Makes the entity of a creature. Only the creature system calls it (world/creatures.h): a
     * command or a different system gives its request to that system.
     * @param creature The kind, the slice, the level, the direction, and the spawn point.
     * @param health The maximum health of the creature, which is its health at the start.
     */
    Entity& make_creature(const Creature& creature, const state::WorldPosition& position, float health);
    /** Removes the entity of a creature. Only the creature system calls it. No effect for a different entity. */
    void remove_creature(EntityId id);
    /**
     * Puts a creature at a different place: the position index follows it. Only the creature
     * system calls it. A creature has no row: nothing goes to the store.
     */
    void place_creature(Entity& entity, const state::WorldPosition& position);

    /**
     * Development aid (tools/world probe, live check L4 of stage 3.3b-S2a): makes an entity of a slice whose states are
     * the fragments of the command (Entity::rawFragments). Its actor id comes from the id allocator. Each session that
     * is `body.interestRadius` or less from it gets its record. @return The entity, or null for a position that is not a
     * position of the world.
     */
    Entity* make_probe(const ProbeBody& body, const state::WorldPosition& position, std::vector<RawFragment> fragments);
    /** Removes a probe entity: each session that observes it sends the stop. @return False: no probe with this id. */
    bool remove_probe(EntityId id);

    /**
     * Makes the entity of an object of the level data (stage 2.5a). Only the interaction system calls it (world/interactions.h).
     * Its actor id is the managed actor id of its placement (game data), not an id of the allocator: the client has the object
     * from its level data, and it applies the states of the record to that object (live check L2). The entity id is G of
     * that actor; it has an odd first byte, and an id of the allocator has an even one.
     */
    Entity& make_object(const ObjectBody& body, const ActorId& actor, const state::WorldPosition& position);
    /**
     * Makes the entity of a server child (stage 2.5b: a station of a settlement, a camp, a fixture). Only the interaction system calls
     * it. Its actor id comes from the id allocator: the client shows the child only after its record (the metadata state and the
     * PositionInTheWorld state of its slice: live check L4). The caller checks the position (valid_position).
     */
    Entity& spawn_object(const ObjectBody& body, const state::WorldPosition& position);
    /** Removes the entity of a server child: each session that observes it sends the stop. Only the interaction system calls it. */
    void remove_object(EntityId id);

    /**
     * Makes the entity of a spell (stage 3.3b-S2a). Only the spell system calls it (world/spells.h). Its actor id comes from
     * the id allocator; the sessions near it look for it in the same tick (last_spell).
     */
    Entity& make_spell(const SpellBody& body, const state::WorldPosition& position);
    /** Removes the entity of a spell: each session that observes it sends the stop. Only the spell system calls it. */
    void remove_spell(EntityId id);
    /**
     * Moves the entity of a spell (stage 3.3b-S2b: an aura that follows its attachment): the position index and the interest of the
     * sessions follow it. Only the spell system calls it. No effect for a different entity or a position that is not of the world.
     */
    void place_spell(Entity& entity, const state::WorldPosition& position);
    /**
     * @return True when two characters are friends for a friendly effect (stage 3.3b-S2b: the Beacon): the same character (a group
     *         of one: the backend has no group; a decision for the user), or a pair of a test fixture (set_test_friends).
     */
    [[nodiscard]] bool friendly(const Entity& a, const Entity& b) const;
    /**
     * @return True when two entities are enemies (stage 3.3b-M2: the projectile shield of Maelstrom ends the projectiles of an enemy): a
     *         creature and a character (or a spell of a character). Two characters are no enemies: the backend has no fight of players
     *         (a decision of an agent).
     */
    [[nodiscard]] bool hostile(const Entity& a, const Entity& b) const;
    /**
     * Test fixture: the two characters are friends (the friend relation of a two-session test of a friendly effect; stage 3.3b-S2b).
     * Stage 3.3b-S6: also the development command `tools/world friends A B` (a development route, only with the harness gate), for a
     * live test of a heal of a friend. A start of the user has no friend pair: a group of one.
     */
    void set_test_friends(const std::string& characterA, const std::string& characterB);
    /** @return The world revision of the last new spell entity: a session compares its interest distances at once. */
    [[nodiscard]] Revision last_spell() const {
        return m_lastSpell;
    }
    /**
     * Makes the entity of a projectile of a creature (stage 3.4f-A). Only the projectile system calls it (world/projectiles.h). Its actor
     * id comes from the id allocator; the sessions near it look for it in the same tick (last_projectile: a flight lives some tenths of
     * a second, and a record that waits for the interval of the interest scan comes when the arrow is far along its path: live check
     * L2a). The caller checks the position (valid_position).
     */
    Entity& make_projectile(const ProjectileBody& body, const state::WorldPosition& position);
    /** Removes the entity of a projectile: each session that observes it sends the stop. Only the projectile system calls it. */
    void remove_projectile(EntityId id);
    /** Moves the entity of a projectile to its place in the flight (the position index, the interest). No state change. */
    void place_projectile(Entity& entity, const state::WorldPosition& position);
    /** @return The world revision of the last new projectile entity (stage 3.4f-A): a session looks for it at once. */
    [[nodiscard]] Revision last_projectile() const {
        return m_lastProjectile;
    }
    /**
     * @return The velocity of a character (stage 3.4f-A: the lead of a creature shot), from its accepted positions (World::place): the
     *         change of the place over at least kVelocitySample, 0 when the last change is older than kVelocityKeep or faster than
     *         kVelocityLargest (a travel). A rule of this project: the client reports no velocity that the backend reads.
     */
    [[nodiscard]] std::array<float, 3> velocity_of(const Entity& entity) const;
    static constexpr std::chrono::milliseconds kVelocitySample{60};
    static constexpr std::chrono::milliseconds kVelocityKeep{300};
    static constexpr float kVelocityLargest = 15.0F;

    /** @return The distance at which a session starts to observe an entity (WorldOptions). */
    [[nodiscard]] float interest_enter(const Entity& entity) const;
    /** @return The distance above which a session stops to observe an entity. */
    [[nodiscard]] float interest_leave(const Entity& entity) const;

    /** Calls `visit(Entity&)` for each entity that is `range` or less from `center` (horizontal). */
    template <class Visit>
    void for_each_near(const state::WorldPosition& center, float range, Visit&& visit);

    /**
     * Calls `visit(const Entity&)` for the entity of each character that is in the world, in
     * the sequence of the character ids. A creature is not in this list.
     */
    template <class Visit>
    void for_each_character(Visit&& visit) const;

    /** @return The value of the revision counter: it is higher after each change of an entity. */
    [[nodiscard]] Revision revision() const {
        return m_revision;
    }

    /** @return The actor id of the player manager of the world. */
    [[nodiscard]] ActorId player_manager() const {
        return m_ids.player_manager();
    }
    /** @return A new actor id for the client actor of a session. */
    [[nodiscard]] ActorId next_session_actor() {
        return m_ids.next(ActorKind::session);
    }

    /** @return The account of a persona id, from the store. */
    [[nodiscard]] std::optional<state::Account> account_of(const std::string& personaId) {
        return m_store.find_account(personaId);
    }
    [[nodiscard]] state::Store& store() {
        return m_store;
    }
    /** @return The inventory system: the items and the coin of each character. */
    [[nodiscard]] Inventory& inventory() {
        return m_inventory;
    }
    /** @return The progress system: the level, the experience, and the attributes of each character. */
    [[nodiscard]] Progress& progress() {
        return m_progress;
    }
    /** @return The skill system: the weapon masteries, the ability bars, and the trade skills of each character. */
    [[nodiscard]] Skills& skills() {
        return m_skills;
    }
    /** @return The ownership system (stage 1.1b): the entitlements of each account and the achievements of each character. */
    [[nodiscard]] Ownership& ownership() {
        return m_ownership;
    }
    /** @return The creature system: the creatures of the world and the spawn points. */
    [[nodiscard]] Creatures& creatures() {
        return m_creatures;
    }
    /** @return The combat system: the hit claims of the clients and the damage. */
    [[nodiscard]] Combat& combat() {
        return m_combat;
    }
    /** @return The health system: the damage that a character takes, its death, and its respawn. */
    [[nodiscard]] Health& health() {
        return m_health;
    }
    /** @return The rewards of a kill: the credit, the experience, the mastery, the loot, and the coin (a part of the creature system). */
    [[nodiscard]] Rewards& rewards() {
        return m_rewards;
    }
    /** @return The abilities: the casts, the cooldowns, and the effects of the abilities of each character. */
    [[nodiscard]] Abilities& abilities() {
        return m_abilities;
    }
    /** @return The gear system: what the equipment and the effects give a character (stage 1.3b). */
    [[nodiscard]] Gear& gear() {
        return m_gear;
    }
    [[nodiscard]] const Gear& gear() const {
        return m_gear;
    }
    /** @return The evaluator of the passive rules of the data (stage 3.3b-S5): the nodes of the weapon trees and the perks. */
    [[nodiscard]] Passives& passives() {
        return m_passives;
    }
    [[nodiscard]] const Passives& passives() const {
        return m_passives;
    }
    /** @return The consumable system: the use of a potion or a food, and the effects on a character (stage 1.3b). */
    [[nodiscard]] Consumables& consumables() {
        return m_consumables;
    }
    /** @return The heart rune system: the charge and the activation of the heart rune of a character (stage 1.3b). */
    [[nodiscard]] Statuses& statuses() {
        return m_statuses;
    }
    [[nodiscard]] const Statuses& statuses() const {
        return m_statuses;
    }
    [[nodiscard]] Runes& runes() {
        return m_runes;
    }
    /** @return The interaction system (stage 2.5a): the use of the objects of the level data, the state of each object. */
    [[nodiscard]] Interactions& interactions() {
        return m_interactions;
    }
    [[nodiscard]] const Interactions& interactions() const {
        return m_interactions;
    }
    /** @return The chests (stage 3.5b.1): the open of a chest and the loot claims of each character. */
    [[nodiscard]] Chests& chests() {
        return m_chests;
    }
    [[nodiscard]] const Chests& chests() const {
        return m_chests;
    }
    /** @return The gathering (stage 3.6a): the nodes, the work of the characters, the respawns. */
    [[nodiscard]] Gathering& gathering() {
        return m_gathering;
    }
    [[nodiscard]] const Gathering& gathering() const {
        return m_gathering;
    }
    /** @return The personal loot bags (stage 3.5b.2): the bag of a kill and of a discard, its parts, the pickups. */
    [[nodiscard]] LootBags& loot_bags() {
        return m_lootBags;
    }
    [[nodiscard]] const LootBags& loot_bags() const {
        return m_lootBags;
    }
    /** @return The spell entities of the world (stage 3.3b-S2a): the vortex of Gravity Well. */
    [[nodiscard]] Spells& spells() {
        return m_spells;
    }
    [[nodiscard]] const Spells& spells() const {
        return m_spells;
    }
    /** @return The stamina of the characters and the dodge (stage 3.4f-B). */
    [[nodiscard]] Stamina& stamina() {
        return m_stamina;
    }
    [[nodiscard]] const Stamina& stamina() const {
        return m_stamina;
    }
    /** @return The projectiles of the players (stage 3.3b-S3): the shots, the flights, and the hits. */
    [[nodiscard]] Projectiles& projectiles() {
        return m_projectiles;
    }
    [[nodiscard]] const Projectiles& projectiles() const {
        return m_projectiles;
    }
    /** @return The world time (stage 2.2): the day cycle of the world, its phase, and its record for the clients. */
    [[nodiscard]] WorldTime& world_time() {
        return m_worldTime;
    }
    /** @return The navigation: the ground, the next step, and the sight of a creature. */
    [[nodiscard]] Navigation& navigation() {
        return m_navigation;
    }
    [[nodiscard]] const Navigation& navigation() const {
        return m_navigation;
    }
    /**
     * Gives a session the damage that its character took (SessionSink::damage_taken), and the
     * death of its character (SessionSink::died). Only the health system calls them. No effect
     * for a session that the world does not know: an entity with no session.
     */
    void damage_taken(SessionId session, const DamageTaken& damage);
    void died(SessionId session, float respawnSeconds);
    /**
     * Gives a session the damage that its character did (SessionSink::damage_dealt). Only the
     * combat system calls it. No effect for a session that the world does not know: an entity
     * with no session.
     */
    void damage_dealt(SessionId session, const DamageDealt& damage);
    /**
     * Gives the session of a healer the notice of a heal of a different character (SessionSink::healed_player; stage 3.3b-S6). Only
     * the health system calls it. No effect for a session that the world does not know.
     */
    void healed_player(SessionId session, EntityId target, float amount);
    /** Gives a session the end of the committed interaction of its character (SessionSink::interaction_cancelled; stage 2.5a). */
    void interaction_cancelled(SessionId session);
    /**
     * Development aid: gives the session of a character one message for its client
     * (SessionSink::development_message).
     * @return False when the character is not in the world, or when no session plays it.
     */
    bool development_message(const std::string& characterId, std::uint32_t type, const std::vector<std::uint8_t>& body);

private:
    void run(Task& task, const char* kind);
    Entity& make_entity(const state::Character& character);
    void remove_entity(EntityId id);
    /**
     * Writes the position of an entity when it is `minimumDistance` or more from the saved
     * position. A position whose write failed keeps its mark (Entity::positionDirty): the next
     * interval tries again. @return True when the row was written.
     */
    bool save_position(Entity& entity, float minimumDistance);
    void save_due_positions();
    /** Gives an entity a position that passed its check: the index, the revision, and the save mark. */
    void place(Entity& entity, const state::WorldPosition& position, bool estimated, bool moving);
    [[nodiscard]] state::AppearanceIndexes appearance_of(const state::Character& character) const;

    const WorldClock& m_clock;
    state::Store& m_store;
    const gamedata::AppearanceTables& m_appearanceTables;
    WorldOptions m_options;
    std::mutex m_mutex;
    /** Tasks of other threads for the next tick. Protected by the mutex. */
    std::vector<Task> m_tasks;
    /** Timers in the sequence of their times. World thread only. */
    std::multimap<WorldClock::time_point, Task> m_timers;
    std::atomic<std::thread::id> m_thread{};
    std::atomic<bool> m_running{false};
    std::thread m_worker;

    // World thread only.
    IdAllocator m_ids;
    Revision m_revision{0};
    Revision m_lastTravel{0};
    /** The state of the random numbers (random_unit). */
    std::uint64_t m_random{0};
    WorldLoad m_load;
    /** The address of an entity stays the same until its removal. */
    std::map<EntityId, Entity> m_entities;
    std::map<std::string, EntityId> m_characters;
    PositionIndex m_index;
    /** Session to the entity that it owns, and to its sink. */
    std::map<SessionId, EntityId> m_owned;
    std::map<SessionId, SessionSink*> m_sinks;
    /** Entities whose position is not the position of the row. */
    std::set<EntityId> m_dirtyPositions;
    Inventory m_inventory;
    Progress m_progress;
    Skills m_skills;
    Ownership m_ownership;
    Navigation m_navigation;
    Creatures m_creatures;
    Combat m_combat;
    Health m_health;
    Rewards m_rewards;
    Abilities m_abilities;
    Gear m_gear;
    Passives m_passives;
    Consumables m_consumables;
    Runes m_runes;
    /** The status effects of the characters (stage 3.3b-S1). */
    Statuses m_statuses;
    /** The spell entities (stage 3.3b-S2a). */
    Spells m_spells;
    /** The friend pairs of a test fixture (set_test_friends), each pair in both sequences. */
    std::set<std::pair<std::string, std::string>> m_testFriends;
    Projectiles m_projectiles;
    /** The stamina of the characters and the dodge (stage 3.4f-B). */
    Stamina m_stamina;
    /** The interactions of the characters and the states of the objects (stage 2.5a). */
    Interactions m_interactions;
    /** The chests and the loot claims (stage 3.5b.1): after the interactions (it registers its handler there). */
    Chests m_chests;
    /** The personal loot bags (stage 3.5b.2): after the interaction system (its handler and its server children). */
    LootBags m_lootBags;
    Revision m_lastSpell{0};
    Revision m_lastProjectile{0};
    /** The world time (stage 2.2): one day cycle for the whole world. */
    WorldTime m_worldTime;
    /**
     * The gathering (stage 3.6a): after the chests (its handler of GatherOption gives a chest to the chest system) and after the world
     * time (the run clock of the respawns).
     */
    Gathering m_gathering;
};

template <class Result>
std::optional<Result> World::call(std::function<Result(World&)> task,
                                  std::chrono::milliseconds limit,
                                  CallStatus* status) {
    const auto report = [status](CallStatus value) {
        if (status != nullptr) {
            *status = value;
        }
    };
    if (in_world_thread()) {
        report(CallStatus::done);
        return task(*this);
    }
    // The caller and the world thread decide together if the task runs: the first one that
    // changes the state from "waiting" wins. Thus a task that the caller gave up does not run
    // later, and a caller does not give up a task that runs.
    struct Shared {
        enum : int { waiting, started, cancelled };
        std::atomic<int> state{waiting};
        std::promise<Result> promise;
    };
    auto shared = std::make_shared<Shared>();
    std::future<Result> future = shared->promise.get_future();
    post([shared, task = std::move(task)](World& world) {
        int expected = Shared::waiting;
        if (!shared->state.compare_exchange_strong(expected, Shared::started)) {
            return;
        }
        try {
            shared->promise.set_value(task(world));
        } catch (...) {
            shared->promise.set_exception(std::current_exception());
        }
    });
    if (future.wait_for(limit) != std::future_status::ready) {
        int expected = Shared::waiting;
        if (shared->state.compare_exchange_strong(expected, Shared::cancelled)) {
            log::write("world: a task of a different thread did not start in %lld ms: it was cancelled",
                       static_cast<long long>(limit.count()));
            report(CallStatus::notStarted);
            return std::nullopt;
        }
        // The task runs now. Its result is the truth: wait for it.
        future.wait();
    }
    try {
        Result value = future.get();
        report(CallStatus::done);
        return value;
    } catch (const std::exception& error) {
        log::write("world: a task of a different thread stopped with an exception: %s", error.what());
    } catch (...) {
        log::write("world: a task of a different thread stopped with an unknown exception");
    }
    report(CallStatus::failed);
    return std::nullopt;
}

template <class Visit>
void World::for_each_near(const state::WorldPosition& center, float range, Visit&& visit) {
    m_index.for_each_candidate(center, range, [&](EntityId id) {
        const auto found = m_entities.find(id);
        if (found != m_entities.end() && horizontal_distance(center, found->second.position) <= range) {
            visit(found->second);
        }
    });
}

template <class Visit>
void World::for_each_character(Visit&& visit) const {
    for (const auto& [character, id] : m_characters) {
        const auto found = m_entities.find(id);
        if (found != m_entities.end()) {
            visit(found->second);
        }
    }
}

} // namespace soulwarden::backend::world

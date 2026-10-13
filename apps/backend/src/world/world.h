#pragma once

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
#include "gamedata/appearance_tables.h"
#include "gamedata/combat_tables.h"
#include "gamedata/creature_data.h"
#include "gamedata/item_catalog.h"
#include "gamedata/progression_tables.h"
#include "state/store.h"
#include "world/combat.h"
#include "world/creatures.h"
#include "world/entity.h"
#include "world/ids.h"
#include "world/inventory.h"
#include "world/position_index.h"
#include "world/position_report.h"
#include "world/progress.h"
#include "world/skills.h"
#include "world/world_clock.h"

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
     * The character of this session did hurt an entity (world/combat.h). The session tells
     * its client, which shows the damage number at the target. The world calls it in the
     * claim of the session, after the change of the target. A sink with no client does nothing.
     */
    virtual void damage_dealt(const DamageDealt& damage) {
        (void)damage;
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

    /** @return The distance at which a session starts to observe an entity (WorldOptions). */
    [[nodiscard]] float interest_enter(const Entity& entity) const;
    /** @return The distance above which a session stops to observe an entity. */
    [[nodiscard]] float interest_leave(const Entity& entity) const;

    /** Calls `visit(Entity&)` for each entity that is `range` or less from `center` (horizontal). */
    template <class Visit>
    void for_each_near(const state::WorldPosition& center, float range, Visit&& visit);

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
    /** @return The creature system: the creatures of the world and the spawn points. */
    [[nodiscard]] Creatures& creatures() {
        return m_creatures;
    }
    /** @return The combat system: the hit claims of the clients and the damage. */
    [[nodiscard]] Combat& combat() {
        return m_combat;
    }
    /**
     * Gives a session the damage that its character did (SessionSink::damage_dealt). Only the
     * combat system calls it. No effect for a session that the world does not know: an entity
     * with no session.
     */
    void damage_dealt(SessionId session, const DamageDealt& damage);

private:
    void run(Task& task, const char* kind);
    Entity& make_entity(const state::Character& character);
    void remove_entity(EntityId id);
    /**
     * Writes the position of an entity when it is `minimumDistance` or more from the saved
     * position. @return True when the row was written.
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
    Creatures m_creatures;
    Combat m_combat;
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

} // namespace soulwarden::backend::world

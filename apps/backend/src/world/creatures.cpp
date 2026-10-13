#include "world/creatures.h"

#include <algorithm>
#include <cmath>

#include "core/logging/log.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

/**
 * @return The slice of a kind for one creature. An alias with more than one slice gives each
 *         slice its part of the creatures (the percent values of the alias): the selector is a
 *         count, so that the sequence is the same in each run.
 */
const gamedata::CreatureSlice* select_body(const gamedata::CreatureData& data,
                                           const gamedata::CreatureKind& kind,
                                           std::uint32_t selector) {
    std::uint64_t total = 0;
    for (const gamedata::CreatureBody& body : kind.bodies) {
        total += body.percent;
    }
    if (total == 0) {
        return nullptr;
    }
    // A step that is not a divisor of a small total: each slice of two equal parts gets its turn.
    std::uint64_t place = (static_cast<std::uint64_t>(selector) * 2654435761ULL >> 7) % total;
    for (const gamedata::CreatureBody& body : kind.bodies) {
        if (place < body.percent) {
            return &data.slices()[body.slice];
        }
        place -= body.percent;
    }
    return &data.slices()[kind.bodies.front().slice];
}

} // namespace

void SpawnGrid::build(const std::vector<gamedata::SpawnPoint>& points) {
    m_cells.clear();
    m_sorted.clear();
    for (std::size_t index = 0; index < points.size(); ++index) {
        m_cells[key_of(cell(points[index].position.x), cell(points[index].position.y))].push_back(
            static_cast<std::uint32_t>(index));
    }
    m_sorted.reserve(m_cells.size());
    for (const auto& [key, cellPoints] : m_cells) {
        const auto x = static_cast<std::int64_t>(static_cast<std::int32_t>(key >> 32));
        const auto y = static_cast<std::int64_t>(static_cast<std::int32_t>(key & 0xFFFFFFFFULL));
        m_sorted.push_back({{x, y}, &cellPoints});
    }
    std::sort(m_sorted.begin(), m_sorted.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
}

Creatures::Creatures(World& world, const gamedata::CreatureData* data) : m_world(world), m_data(data) {
    load_points();
}

void Creatures::set_waiting(std::uint32_t point, bool waiting) {
    m_points[point].waiting = waiting;
    if (waiting) {
        m_waiting.insert(point);
    } else {
        m_waiting.erase(point);
    }
}

void Creatures::load_points() {
    // The backend program loads the creature data before it makes the world. A test can set the
    // data later: the table of the spawn points then has the size of the data at the first use.
    if (!has_data() || m_points.size() == m_data->spawn_points().size()) {
        return;
    }
    const std::vector<gamedata::SpawnPoint>& points = m_data->spawn_points();
    m_points.assign(points.size(), SpawnPointState{});
    m_waiting.clear();
    m_grid.build(points);
}

Entity* Creatures::find(EntityId id) {
    Entity* entity = m_world.find(id);
    return entity != nullptr && entity->kind == EntityKind::creature ? entity : nullptr;
}

Entity* Creatures::make(const gamedata::CreatureKind& kind,
                        std::uint32_t level,
                        const state::WorldPosition& position,
                        float yaw,
                        std::optional<std::uint32_t> spawnPoint,
                        std::uint32_t selector) {
    const gamedata::CreatureSlice* body = select_body(*m_data, kind, selector);
    const float health = m_data->health(kind, level);
    if (body == nullptr || !(health > 0.0F)) {
        return nullptr;
    }
    Creature creature;
    creature.kind = &kind;
    creature.body = body;
    creature.level = level;
    creature.yaw = yaw;
    creature.spawnPoint = spawnPoint;
    creature.madeAt = m_world.clock().now();
    Entity& entity = m_world.make_creature(creature, position, health);
    m_creatures.insert(entity.id);
    m_counts.largest = std::max(m_counts.largest, m_creatures.size());
    if (body->spawnAction) {
        // The client shows the body and the name plate of this slice only after its spawn
        // action. The action has no end: a session that observes the creature later gets it
        // with the time from its start, and its client then shows the creature at once.
        start_action(entity, *body->spawnAction);
    }
    return &entity;
}

CreatureResult Creatures::spawn(const std::string& kindId,
                                const state::WorldPosition& position,
                                float yaw,
                                std::optional<std::uint32_t> level) {
    CreatureResult result;
    if (!has_data()) {
        result.error = CreatureError::noData;
        return result;
    }
    load_points();
    const gamedata::CreatureKind* kind = m_data->find_kind(kindId);
    if (kind == nullptr) {
        result.error = CreatureError::unknownKind;
        return result;
    }
    if (!valid_position(position) || !std::isfinite(yaw)) {
        result.error = CreatureError::badPosition;
        return result;
    }
    const std::uint32_t creatureLevel = level.value_or(kind->level);
    if (!m_data->has_level(creatureLevel)) {
        result.error = CreatureError::badLevel;
        return result;
    }
    result.entity = make(*kind, creatureLevel, position, yaw, std::nullopt, m_commandSpawns++);
    if (result.entity == nullptr) {
        result.error = CreatureError::badLevel;
        return result;
    }
    ++m_counts.spawnedByCommand;
    return result;
}

CreatureResult Creatures::remove(EntityId id) {
    CreatureResult result;
    Entity* entity = find(id);
    if (entity == nullptr) {
        result.error = CreatureError::unknownCreature;
        return result;
    }
    if (entity->creature.spawnPoint) {
        // A command took the creature away: the distance rule must not make it again.
        set_waiting(*entity->creature.spawnPoint, true);
    }
    ++m_counts.removedByCommand;
    m_world.remove_creature(id);
    return result;
}

CreatureResult Creatures::set_health(EntityId id, float health) {
    CreatureResult result;
    Entity* entity = find(id);
    if (entity == nullptr) {
        result.error = CreatureError::unknownCreature;
        return result;
    }
    if (entity->creature.dead) {
        result.error = CreatureError::dead;
        return result;
    }
    if (!std::isfinite(health) || health < 0.0F || health > entity->vitals.healthMax) {
        result.error = CreatureError::badHealth;
        return result;
    }
    entity->vitals.health = health;
    if (health == 0.0F) {
        entity->creature.dead = true;
        entity->creature.diedAt = m_world.clock().now();
        ++m_counts.deaths;
        if (entity->creature.spawnPoint) {
            // The place of the respawn time (stage 3.5a): `respawnAt` gets the time of the death
            // plus the respawn time of the spawner. With no time the point waits for a command.
            set_waiting(*entity->creature.spawnPoint, true);
            m_points[*entity->creature.spawnPoint].respawnAt.reset();
        }
        log::write("world: creature %016llx (%s) is dead",
                   static_cast<unsigned long long>(entity->id),
                   entity->creature.kind->id.c_str());
        // The death action of the slice: the body falls and stays on the ground. The client
        // starts this action only when the server starts it.
        const gamedata::CreatureSlice& body = *entity->creature.body;
        if (body.deathAction) {
            start_action(*entity, *body.deathAction);
        }
    }
    m_world.touch(id, ComponentKey::vitals);
    result.entity = entity;
    return result;
}

CreatureResult Creatures::hurt(EntityId id, float damage) {
    CreatureResult result;
    Entity* entity = find(id);
    if (entity == nullptr) {
        result.error = CreatureError::unknownCreature;
        return result;
    }
    if (entity->creature.dead) {
        result.error = CreatureError::dead;
        return result;
    }
    if (!std::isfinite(damage) || damage < 0.0F) {
        result.error = CreatureError::badHealth;
        return result;
    }
    // One path with the health command and the kill command: the health 0 is the death.
    result = set_health(id, std::max(entity->vitals.health - damage, 0.0F));
    if (result.error == CreatureError::none && !entity->creature.dead) {
        const gamedata::CreatureSlice& body = *entity->creature.body;
        if (body.hitAction) {
            start_action(*entity, *body.hitAction, kReactionTime);
        }
    }
    return result;
}

CreatureResult Creatures::play(EntityId id, const std::string& action, std::optional<float> seconds) {
    CreatureResult result;
    Entity* entity = find(id);
    if (entity == nullptr) {
        result.error = CreatureError::unknownCreature;
        return result;
    }
    if (entity->creature.dead) {
        result.error = CreatureError::dead;
        return result;
    }
    const std::optional<std::uint32_t> index = entity->creature.body->find_action(action);
    if (!index) {
        result.error = CreatureError::unknownAction;
        return result;
    }
    start_action(*entity, *index,
                 seconds ? std::optional<std::chrono::milliseconds>(
                               std::chrono::milliseconds(static_cast<long long>(std::max(*seconds, 0.0F) * 1000.0F)))
                         : std::nullopt);
    result.entity = entity;
    return result;
}

void Creatures::start_action(Entity& entity, std::uint32_t action, std::optional<std::chrono::milliseconds> length) {
    const gamedata::CreatureSlice& body = *entity.creature.body;
    if (action >= body.actions.size() || body.actions[action].fragmentIndex < 0) {
        return;
    }
    CreatureAction started;
    started.action = action;
    started.fragment = static_cast<std::uint32_t>(body.actions[action].fragmentIndex);
    // The count of the actions of the creature stays when an action ended: a new action must
    // not have the count of an action that a client got.
    started.generation = static_cast<std::uint16_t>(++entity.creature.actions);
    started.started = m_world.clock().now();
    if (length) {
        started.ends = started.started + *length;
        m_acting.insert(entity.id);
    } else {
        m_acting.erase(entity.id);
    }
    entity.creature.action = started;
    m_world.touch(entity.id, ComponentKey::movement);
}

void Creatures::end_actions() {
    const WorldClock::time_point now = m_world.clock().now();
    for (auto acting = m_acting.begin(); acting != m_acting.end();) {
        Entity* entity = find(*acting);
        if (entity != nullptr && entity->creature.action && entity->creature.action->ends
            && now < *entity->creature.action->ends) {
            ++acting;
            continue;
        }
        if (entity != nullptr && entity->creature.action && entity->creature.action->ends) {
            // No mark of the movement state: the client of each observer ended the action
            // itself and plays its idle action. A session that observes the creature from now
            // on gets a record with no action, or with the spawn action of a slice that needs
            // it: the client of that session shows the body only with that action. Its time is
            // the time from the creation of the creature.
            entity->creature.action.reset();
            const gamedata::CreatureSlice& body = *entity->creature.body;
            if (body.spawnAction && !entity->creature.dead) {
                CreatureAction standing;
                standing.action = *body.spawnAction;
                standing.fragment = static_cast<std::uint32_t>(body.actions[*body.spawnAction].fragmentIndex);
                standing.generation = static_cast<std::uint16_t>(++entity->creature.actions);
                standing.started = entity->creature.madeAt;
                entity->creature.action = standing;
            }
        }
        acting = m_acting.erase(acting);
    }
}

std::size_t Creatures::reset_spawn_points() {
    // Only the points that wait: the list of all spawn points of the world is long.
    const std::vector<std::uint32_t> waiting(m_waiting.begin(), m_waiting.end());
    const std::size_t count = waiting.size();
    for (const std::uint32_t index : waiting) {
        SpawnPointState& point = m_points[index];
        set_waiting(index, false);
        point.respawnAt.reset();
        // The body of the creature that died is in the way of the new creature.
        const Entity* body = point.entity != 0 ? find(point.entity) : nullptr;
        if (body != nullptr && body->creature.dead) {
            ++m_counts.removedByCommand;
            m_world.remove_creature(point.entity);
        }
    }
    // The next tick makes the creatures of the points near a player.
    m_lastScan.reset();
    return count;
}

bool Creatures::respawn_at(std::uint32_t spawnPoint, WorldClock::time_point time) {
    load_points();
    if (spawnPoint >= m_points.size()) {
        return false;
    }
    if (m_points[spawnPoint].waiting) {
        m_points[spawnPoint].respawnAt = time;
    }
    return true;
}

void Creatures::entity_removed(const Entity& entity) {
    if (entity.kind != EntityKind::creature) {
        return;
    }
    m_creatures.erase(entity.id);
    m_acting.erase(entity.id);
    if (entity.creature.spawnPoint && m_points[*entity.creature.spawnPoint].entity == entity.id) {
        m_points[*entity.creature.spawnPoint].entity = 0;
    }
}

void Creatures::tick() {
    if (!has_data()) {
        return;
    }
    end_actions();
    load_points();
    const WorldClock::time_point now = m_world.clock().now();
    if (m_lastScan && now - *m_lastScan < kScanInterval) {
        return;
    }
    m_lastScan = now;
    scan();
}

void Creatures::scan() {
    const LoadTimer timer(m_scanLoad);
    const WorldClock::time_point now = m_world.clock().now();
    const WorldOptions& options = m_world.options();
    // The cost of a scan depends on the players and on what is near them: the list of all
    // spawn points of the world is long, and the scan does not walk it.
    std::vector<state::WorldPosition> players;
    m_world.for_each_character([&players](const Entity& entity) { players.push_back(entity.position); });
    ++m_counts.scans;
    m_counts.lastScanPoints = 0;
    m_counts.lastScanCreatures = 0;
    // A creature of a spawn point leaves the world when no player is near it. First the
    // creatures that a player is near, from the position index of the world.
    m_near.clear();
    for (const state::WorldPosition& player : players) {
        m_world.for_each_near(player, options.creatureSpawnLeave, [this](Entity& entity) {
            ++m_counts.lastScanCreatures;
            if (entity.kind == EntityKind::creature) {
                m_near.insert(entity.id);
            }
        });
    }
    // The creatures of the world are the creatures near a player, and the creatures of commands.
    // The entity table changes in the loop: walk a copy of the ids.
    const std::vector<EntityId> creatures(m_creatures.begin(), m_creatures.end());
    for (const EntityId id : creatures) {
        const Entity* entity = find(id);
        if (entity == nullptr || !entity->creature.spawnPoint) {
            continue; // A creature of a command stays until a command removes it.
        }
        if (m_near.count(id) == 0) {
            ++m_counts.removedByDistance;
            m_world.remove_creature(id);
        }
    }
    m_near.clear();
    const std::vector<gamedata::SpawnPoint>& points = m_data->spawn_points();
    for (const state::WorldPosition& player : players) {
        m_grid.for_each_candidate(player, options.creatureSpawnEnter, [&](std::uint32_t index) {
            ++m_counts.lastScanPoints;
            const gamedata::SpawnPoint& point = points[index];
            SpawnPointState& state = m_points[index];
            if (horizontal_distance(player, point.position) > options.creatureSpawnEnter) {
                return;
            }
            if (state.waiting) {
                if (!state.respawnAt || now < *state.respawnAt) {
                    return;
                }
                // The respawn time came: the point is free again. The body of the creature that
                // died can be in the world still (a player stayed near it): it is in the way of
                // the new creature, and it leaves the world first.
                set_waiting(index, false);
                state.respawnAt.reset();
                const Entity* body = state.entity != 0 ? find(state.entity) : nullptr;
                if (body != nullptr && body->creature.dead) {
                    ++m_counts.removedByRespawn;
                    m_world.remove_creature(body->id);
                }
            }
            if (state.entity != 0) {
                return;
            }
            const Entity* entity = make(m_data->kinds()[point.kind], point.level, point.position, point.yaw, index,
                                        index + state.spawns);
            if (entity == nullptr) {
                // The level table has no row for the level of the point: no creature, and no new try.
                set_waiting(index, true);
                log::write("world: spawn point %s makes no creature: the level table has no level %u",
                           point.id.c_str(),
                           point.level);
                return;
            }
            state.entity = entity->id;
            ++state.spawns;
            ++m_counts.spawnedByPoint;
        });
    }
    m_counts.pointsLooked += m_counts.lastScanPoints;
    m_counts.creaturesLooked += m_counts.lastScanCreatures;
}

} // namespace soulwarden::backend::world

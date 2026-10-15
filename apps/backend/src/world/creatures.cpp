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

Creatures::Creatures(World& world, const gamedata::CreatureData* data)
    : m_world(world), m_data(data), m_behavior(world, *this) {
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
        // action. It is a value of the creature for its whole time in the world: the first
        // movement state of each session for the creature has it, with the time from its
        // start, and the client of that session then shows the creature at once. An attack, a
        // reaction, and a walk do not take it away.
        CreatureAction spawned;
        spawned.action = *body->spawnAction;
        spawned.fragment = static_cast<std::uint32_t>(body->actions[*body->spawnAction].fragmentIndex);
        spawned.generation = static_cast<std::uint16_t>(++entity.creature.actions);
        spawned.started = entity.creature.madeAt;
        spawned.tags = body->tags;
        entity.creature.spawnAction = spawned;
    }
    m_behavior.made(entity);
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
    if (entity->creature.immune() && health < entity->vitals.health) {
        // On its way back a creature takes no damage from any source: a hit, the health command,
        // and the kill command (code review of stage 3.4e, finding F7). A health that goes up
        // is not a damage.
        result.error = CreatureError::immune;
        return result;
    }
    entity->vitals.health = health;
    if (health == 0.0F) {
        entity->creature.dead = true;
        entity->creature.diedAt = m_world.clock().now();
        ++m_counts.deaths;
        if (entity->creature.spawnPoint) {
            // The respawn time of the spawner from the time of the death (world/rewards.h,
            // respawn_time): from then on the point makes its creature again when a player is near
            // it, and the body leaves the world first. The command `reset` makes it free at once.
            const std::uint32_t point = *entity->creature.spawnPoint;
            set_waiting(point, true);
            const std::chrono::milliseconds wait = respawn_time(m_data->spawn_points()[point], m_world.random_unit());
            m_points[point].respawnAt = entity->creature.diedAt + wait;
            log::write("world: spawn point %s makes its creature again in %.0f s, when a player is near it",
                       m_data->spawn_points()[point].id.c_str(),
                       std::chrono::duration<double>(wait).count());
        }
        log::write("world: creature %016llx (%s) is dead",
                   static_cast<unsigned long long>(entity->id),
                   entity->creature.kind->id.c_str());
        // A dead creature has no target, and the attack that it plays does no damage.
        m_behavior.died(*entity);
        // The death action of the slice: the body falls and stays on the ground. The client
        // starts this action only when the server starts it.
        const gamedata::CreatureSlice& body = *entity->creature.body;
        if (body.deathAction) {
            start_action(*entity, *body.deathAction);
        } else if (entity->creature.action || entity->creature.moving) {
            // A slice with no death action: the action that plays (a reaction, an attack, the
            // locomotion of a way) is not the death action of the body. A session that observes
            // the body from now on gets no such action: its first state has the spawn action of
            // the slice, when the slice has one. A session that has the action gets the scope
            // fields with no record: the client keeps the scope fields of a creature, and it would
            // keep the run of a dead creature (code review of the corrections after user test 1,
            // finding F2).
            entity->creature.action.reset();
            m_acting.erase(entity->id);
            entity->creature.moving.reset();
            m_world.touch(entity->id, ComponentKey::movement);
        }
        // A dead creature does not walk (start_action ended the locomotion for a death action).
        entity->creature.moving.reset();
    }
    m_world.touch(id, ComponentKey::vitals);
    result.entity = entity;
    return result;
}

CreatureResult Creatures::hurt(EntityId id, float damage, EntityId attacker) {
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
    if (entity->creature.immune()) {
        // On its way back a creature takes no damage, no reaction, and no target by a hit.
        result.error = CreatureError::immune;
        return result;
    }
    // One path with the health command and the kill command: the health 0 is the death.
    const float dealt = std::min(damage, entity->vitals.health);
    result = set_health(id, std::max(entity->vitals.health - damage, 0.0F));
    // The credit of the kill: the damage of each character (world/rewards.h). A death by a hit of
    // a character gives the rewards of the kill to the characters with the credit.
    Entity* character = attacker != 0 && result.error == CreatureError::none ? m_world.find(attacker) : nullptr;
    if (character != nullptr && character->kind == EntityKind::player) {
        m_world.rewards().hit(*entity, *character, dealt);
        if (entity->creature.dead) {
            m_world.rewards().killed(*entity);
        }
    }
    if (result.error == CreatureError::none && !entity->creature.dead) {
        const gamedata::CreatureSlice& body = *entity->creature.body;
        // A creature that plays an attack does not stop it for a hit: the reaction would take
        // the action of the attack away, and a player that hits fast would stop each attack.
        if (body.hitAction && !entity->creature.mind.attack) {
            start_action(*entity, *body.hitAction, kReactionTime);
        }
        if (attacker != 0) {
            m_behavior.hurt_by(*entity, attacker);
        }
    }
    return result;
}

CreatureResult Creatures::play(EntityId id, const std::string& action, std::optional<float> seconds,
                               std::optional<gamedata::FragmentTags> tags) {
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
    if (tags && entity->creature.action) {
        entity->creature.action->tags = *tags;
    }
    result.entity = entity;
    return result;
}

bool Creatures::start_action(Entity& entity, std::uint32_t action, std::optional<std::chrono::milliseconds> length) {
    const gamedata::CreatureSlice& body = *entity.creature.body;
    if (action >= body.actions.size() || body.actions[action].fragmentIndex < 0) {
        return false;
    }
    CreatureAction started;
    started.action = action;
    started.fragment = static_cast<std::uint32_t>(body.actions[action].fragmentIndex);
    // The count of the actions of the creature stays when an action ended: a new action must
    // not have the count of an action that a client got.
    started.generation = static_cast<std::uint16_t>(++entity.creature.actions);
    started.started = m_world.clock().now();
    started.tags = body.tags;
    if (length) {
        started.ends = started.started + *length;
        m_acting.insert(entity.id);
    } else {
        m_acting.erase(entity.id);
    }
    started.changed = m_world.touch(entity.id, ComponentKey::movement);
    entity.creature.action = started;
    // The action of the server replaces the locomotion: an attack and a reaction stop the walk.
    entity.creature.moving.reset();
    return true;
}

void Creatures::move(Entity& entity, const state::WorldPosition& position, Gait gait) {
    m_world.place_creature(entity, position);
    const Revision revision = m_world.touch(entity.id, ComponentKey::movement);
    if (gait == Gait::place) {
        return;
    }
    Creature& creature = entity.creature;
    // The step replaces an action of the server that plays (a reaction to a hit, the action of
    // a command): a creature that changes its place plays its locomotion, not a different
    // action (user test 1: a creature that moved in the pose of an attack). The behavior does
    // not move a creature while its attack plays. A slice with no locomotion action ends the
    // action too: its observers get the scope fields with no record (code review of the
    // corrections after user test 1, finding F7).
    if (creature.action) {
        creature.action.reset();
        m_acting.erase(entity.id);
    }
    const std::optional<std::uint32_t> locomotion =
        gait == Gait::walk && creature.body->walkAction ? creature.body->walkAction : creature.body->moveAction;
    if (!locomotion) {
        creature.moving.reset();
        return;
    }
    if (creature.moving && creature.moving->action == *locomotion) {
        return; // One record for the whole way: the client plays the locomotion from it.
    }
    const WorldClock::time_point now = m_world.clock().now();
    CreatureAction moving;
    moving.action = *locomotion;
    moving.fragment = static_cast<std::uint32_t>(creature.body->actions[moving.action].fragmentIndex);
    moving.generation = static_cast<std::uint16_t>(++creature.actions);
    moving.started = now;
    moving.tags = creature.body->tags;
    moving.changed = revision;
    creature.moving = moving;
}

void Creatures::immune_changed(Entity& entity) {
    entity.creature.immuneChanged = m_world.touch(entity.id, ComponentKey::vitals);
}

void Creatures::stop(Entity& entity) {
    if (!entity.creature.moving) {
        return;
    }
    entity.creature.moving.reset();
    m_world.touch(entity.id, ComponentKey::movement);
}

void Creatures::moved(Entity& entity) {
    m_world.touch(entity.id, ComponentKey::movement);
}

void Creatures::restore(Entity& entity) {
    // At its anchor with its full health: the damage of the fight gives no credit any more.
    m_world.rewards().restored(entity);
    if (entity.creature.dead || entity.vitals.health == entity.vitals.healthMax) {
        return;
    }
    entity.vitals.health = entity.vitals.healthMax;
    m_world.touch(entity.id, ComponentKey::vitals);
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
            // itself and plays its idle action. The next movement state of a session has the
            // scope fields with no record (the key of the scope of the session changed), so
            // that the client does not start the action again. A session that observes the
            // creature from now on gets a record with no action of this kind. The spawn action
            // of a slice that needs it is not here: it is a value of the creature
            // (Creature::spawnAction), and the end of an action does not change it, also for a
            // creature that died while its reaction played.
            entity->creature.action.reset();
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
    m_behavior.removed(entity);
    if (entity.creature.spawnPoint && m_points[*entity.creature.spawnPoint].entity == entity.id) {
        m_points[*entity.creature.spawnPoint].entity = 0;
    }
}

void Creatures::tick() {
    if (!has_data()) {
        return;
    }
    load_points();
    // The behavior first: a contact of an attack is due at the latest at the end of its action.
    m_behavior.tick();
    end_actions();
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
    // One walk of the spawn points near each player, with the larger of the two distances: a
    // point whose creature is in the world keeps it while a player is `creatureSpawnLeave` or
    // less from the point, and a point with no creature makes it when a player is
    // `creatureSpawnEnter` or less from it. The distance is the distance of the point, not of
    // the creature: a creature that goes to a player and away from its point stays in the world.
    m_near.clear();
    const float keep = std::max(options.creatureSpawnLeave, options.creatureSpawnEnter);
    const std::vector<gamedata::SpawnPoint>& points = m_data->spawn_points();
    std::vector<std::uint32_t> entering;
    for (const state::WorldPosition& player : players) {
        m_grid.for_each_candidate(player, keep, [&](std::uint32_t index) {
            ++m_counts.lastScanPoints;
            const float away = horizontal_distance(player, points[index].position);
            if (away > keep) {
                return;
            }
            if (m_points[index].entity != 0) {
                m_near.insert(m_points[index].entity);
            }
            if (away <= options.creatureSpawnEnter) {
                entering.push_back(index);
            }
        });
    }
    // The creatures of the world are the creatures of the points near a player, and the
    // creatures of commands. The entity table changes in the loop: walk a copy of the ids.
    const std::vector<EntityId> creatures(m_creatures.begin(), m_creatures.end());
    for (const EntityId id : creatures) {
        const Entity* entity = find(id);
        ++m_counts.lastScanCreatures;
        if (entity == nullptr || !entity->creature.spawnPoint) {
            continue; // A creature of a command stays until a command removes it.
        }
        if (m_near.count(id) == 0) {
            ++m_counts.removedByDistance;
            m_world.remove_creature(id);
        }
    }
    m_near.clear();
    for (const std::uint32_t index : entering) {
        const gamedata::SpawnPoint& point = points[index];
        SpawnPointState& state = m_points[index];
        if (state.waiting) {
            if (!state.respawnAt || now < *state.respawnAt) {
                continue;
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
            continue;
        }
        const Entity* entity = make(m_data->kinds()[point.kind], point.level, point.position, point.yaw, index,
                                    index + state.spawns);
        if (entity == nullptr) {
            // The level table has no row for the level of the point: no creature, and no new try.
            set_waiting(index, true);
            log::write("world: spawn point %s makes no creature: the level table has no level %u",
                       point.id.c_str(),
                       point.level);
            continue;
        }
        state.entity = entity->id;
        ++state.spawns;
        ++m_counts.spawnedByPoint;
    }
    m_counts.pointsLooked += m_counts.lastScanPoints;
    m_counts.creaturesLooked += m_counts.lastScanCreatures;
}

} // namespace soulwarden::backend::world

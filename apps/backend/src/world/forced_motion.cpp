#include "world/forced_motion.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "core/logging/log.h"
#include "world/creatures.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

unsigned long long id_number(EntityId id) {
    return static_cast<unsigned long long>(id);
}

float seconds_of(std::chrono::steady_clock::duration duration) {
    return std::chrono::duration<float>(duration).count();
}

} // namespace

ForcedMotion::ForcedMotion(World& world, Creatures& creatures) : m_world(world), m_creatures(creatures) {}

bool ForcedMotion::impact(Entity& entity, const gamedata::DamageImpact& impact, const state::WorldPosition& source,
                          const state::WorldPosition& attacker, std::string_view row, std::chrono::milliseconds length) {
    Creature& creature = entity.creature;
    if (entity.kind != EntityKind::creature || creature.dead || creature.immune()) {
        ++m_counts.refused;
        return false;
    }
    // The values for a target that is a creature: the "_AI" values of the row when it has them (client code at image
    // RVA 0x683D910 takes such a value when the row has one).
    const bool creatureValues = impact.creatureDistance[0] != 0.0F || impact.creatureDistance[1] != 0.0F
                                || impact.creatureDistance[2] != 0.0F;
    const std::array<float, 3>& distance = creatureValues ? impact.creatureDistance : impact.distance;
    const float decay = impact.creatureDecay > 0.0F ? impact.creatureDecay : impact.decay;
    // The direction: from the source of the hit (or the attacker) to the creature, in the plane.
    const state::WorldPosition& origin = impact.fromAttacker ? attacker : source;
    float dx = entity.position.x - origin.x;
    float dy = entity.position.y - origin.y;
    const float away = std::hypot(dx, dy);
    if (away >= forced_rules::kNoDirection) {
        dx /= away;
        dy /= away;
    } else {
        dx = 0.0F; // The default direction of the client code: north.
        dy = 1.0F;
    }
    ForcedMove move;
    move.row = std::string(row);
    const WorldClock::time_point now = m_world.clock().now();
    move.started = now;
    move.stepped = now;
    move.ends = now + std::max(length, forced_rules::kStep);
    move.start = entity.position;
    if (decay > 0.0F) {
        // direction * Y + side * X, side = direction x up = (dy, -dx). The vertical part (Z) is not a place of the
        // navigation: the creature stays on its floor.
        const float ex = dx * distance[1] + dy * distance[0];
        const float ey = dy * distance[1] - dx * distance[0];
        move.distance = std::hypot(ex, ey);
        if (move.distance < forced_rules::kShortest) {
            return false;
        }
        move.direction = {ex / move.distance, ey / move.distance};
        move.decay = std::min(decay, 1.0F);
    } else {
        move.constantSpeed = true;
        move.speed = std::hypot(distance[0], distance[1], distance[2]);
        move.point = origin;
        move.away = distance[1] > 0.0F;
        if (move.speed < forced_rules::kShortest || (!move.away && away <= forced_rules::kPointStop)) {
            return false;
        }
    }
    const bool replaced = creature.forced.has_value();
    creature.forced = move;
    m_moving.insert(entity.id);
    ++(replaced ? m_counts.replaced : m_counts.started);
    m_counts.largest = std::max(m_counts.largest, m_moving.size());
    // The path of the creature stays (user test 2, UT2-4): the navigation keeps it while the creature is near it.
    log::write("world: force: creature %016llx (%s) moves by the impact of %s: %s %.2f m%s (%s), for %.2f s at most",
               id_number(entity.id),
               creature.kind->id.c_str(),
               move.row.c_str(),
               move.constantSpeed ? (move.away ? "away from the source at" : "to the source at") : "a travel of",
               static_cast<double>(move.constantSpeed ? move.speed : move.distance),
               move.constantSpeed ? "/s" : "",
               move.constantSpeed ? "no decay" : (std::string("decay ") + std::to_string(move.decay)).c_str(),
               static_cast<double>(seconds_of(move.ends - now)));
    return true;
}

void ForcedMotion::tick() {
    if (m_moving.empty()) {
        return;
    }
    const WorldClock::time_point now = m_world.clock().now();
    // A step can end a movement: walk a copy of the ids.
    const std::vector<EntityId> moving(m_moving.begin(), m_moving.end());
    for (const EntityId id : moving) {
        Entity* entity = m_creatures.find(id);
        ++m_counts.looked;
        if (entity == nullptr || !entity->creature.forced) {
            m_moving.erase(id);
            continue;
        }
        if (entity->creature.dead || entity->creature.immune()) {
            end(*entity, entity->creature.dead ? "the creature died" : "the creature goes back");
            continue;
        }
        if (now - entity->creature.forced->stepped < forced_rules::kStep && now < entity->creature.forced->ends) {
            continue;
        }
        if (!step(*entity, now)) {
            continue;
        }
    }
}

bool ForcedMotion::step(Entity& entity, WorldClock::time_point now) {
    ForcedMove& move = *entity.creature.forced;
    const WorldClock::time_point at = std::min(now, move.ends);
    state::WorldPosition goal = entity.position;
    bool last = at >= move.ends;
    if (move.constantSpeed) {
        // A constant speed to the point, or away from it (client code at image RVA 0x488B8F0).
        const float dx = move.point.x - entity.position.x;
        const float dy = move.point.y - entity.position.y;
        const float left = std::hypot(dx, dy);
        if (!move.away && left <= forced_rules::kPointStop) {
            end(entity, "at the source point");
            return false;
        }
        float way = move.speed * seconds_of(at - move.stepped);
        float ux = left > 1.0e-4F ? dx / left : 0.0F;
        float uy = left > 1.0e-4F ? dy / left : 1.0F;
        if (move.away) {
            ux = -ux;
            uy = -uy;
        } else if (way >= left - forced_rules::kPointStop) {
            way = std::max(left - forced_rules::kPointStop, 0.0F);
            last = true;
        }
        goal = {entity.position.x + ux * way, entity.position.y + uy * way, entity.position.z};
    } else {
        // The travel of the time of the reaction (client code at image RVA 0x48A0D30).
        const float t = seconds_of(at - move.started);
        float travel = move.decay >= 1.0F ? move.distance : move.distance * (1.0F - std::pow(move.decay, forced_rules::kFrames * t));
        if (move.distance - travel < forced_rules::kRest) {
            travel = move.distance; // The last step goes the rest of the travel (user test 2: the end at kRest, not 1 mm).
            last = true;
        }
        const float more = travel - move.travelled;
        goal = {entity.position.x + move.direction[0] * more, entity.position.y + move.direction[1] * more, entity.position.z};
    }
    move.stepped = at;
    const Slide slide = m_world.navigation().slide(entity.position, goal);
    if (slide.waiting) {
        ++m_counts.waits;
        if (last) {
            end(entity, "the navigation data of its place is not there");
            return false;
        }
        return true;
    }
    const float moved = horizontal_distance(entity.position, slide.position);
    if (moved > 0.0005F || std::fabs(slide.position.z - entity.position.z) > 0.0005F) {
        move.travelled += moved;
        m_creatures.move(entity, slide.position, Gait::place);
        ++m_counts.steps;
    }
    if (slide.blocked) {
        ++m_counts.blocked;
        end(entity, "a wall or a drop stopped it");
        return false;
    }
    if (last) {
        end(entity, "the travel is complete");
        return false;
    }
    return true;
}

void ForcedMotion::stop(Entity& entity) {
    if (entity.creature.forced) {
        end(entity, entity.creature.dead ? "the creature died" : "the creature goes back");
    }
}

void ForcedMotion::end(Entity& entity, const char* cause) {
    m_moving.erase(entity.id);
    if (!entity.creature.forced) {
        return;
    }
    const ForcedMove move = *entity.creature.forced;
    entity.creature.forced.reset();
    ++m_counts.ended;
    m_creatures.behavior().moved_by_force(entity);
    log::write("world: force: creature %016llx (%s) stops after %.2f m in %.2f s (%s): %s, at %.2f %.2f %.2f",
               id_number(entity.id),
               entity.creature.kind->id.c_str(),
               static_cast<double>(horizontal_distance(move.start, entity.position)),
               static_cast<double>(seconds_of(m_world.clock().now() - move.started)),
               move.row.c_str(),
               cause,
               static_cast<double>(entity.position.x),
               static_cast<double>(entity.position.y),
               static_cast<double>(entity.position.z));
}

} // namespace soulwarden::backend::world

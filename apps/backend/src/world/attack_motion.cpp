#include "world/attack_motion.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/logging/log.h"
#include "world/creatures.h"
#include "world/forced_motion.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

/** A turn that sends a movement state (degrees): smaller turns go with the next state (a rule of this project). */
constexpr float kTurnSent = 2.0F;
/** The target angle is reached when less than this is left (degrees; a decision of an agent: the client compares rotations). */
constexpr float kTurnDone = 0.5F;
/** The place dest is reached when less than this is left (meters; the client compares each axis with a small epsilon). */
constexpr float kAtDest = 0.01F;

unsigned long long id_number(EntityId id) {
    return static_cast<unsigned long long>(id);
}

float seconds_of(std::chrono::steady_clock::duration duration) {
    return std::chrono::duration<float>(duration).count();
}

/** @return The signed turn from one yaw to a different one, from -180 to 180 degrees (counterclockwise positive, the yaw of a creature). */
float signed_turn(float from, float to) {
    return std::fmod(to - from + 540.0F, 360.0F) - 180.0F;
}

/** @return True when a target place passes the wedge of a clip from a place along a direction (client code 0x5C6A000). */
bool in_wedge(const gamedata::HomingClip& clip, const state::WorldPosition& from, float yaw, const state::WorldPosition& target) {
    if (clip.useRadius && horizontal_distance(from, target) >= clip.radius) {
        return false;
    }
    if (clip.height > 0.0F && std::fabs(target.z - from.z) >= clip.height) {
        return false;
    }
    if (horizontal_distance(from, target) < 0.01F) {
        return true;
    }
    return std::fabs(signed_turn(yaw, yaw_to(from, target))) <= clip.maxAngle;
}

} // namespace

AttackMotion::AttackMotion(World& world, Creatures& creatures) : m_world(world), m_creatures(creatures) {}

void AttackMotion::start(Entity& entity, EntityId target, std::uint32_t attackIndex) {
    Creature& creature = entity.creature;
    if (entity.kind != EntityKind::creature || attackIndex >= creature.body->attacks.size() || !creature.action) {
        return;
    }
    const gamedata::CreatureAttack& attack = creature.body->attacks[attackIndex];
    if (attack.homing.empty()) {
        creature.homing.reset();
        m_moving.erase(entity.id);
        return;
    }
    HomingMove homing;
    homing.attack = attackIndex;
    homing.generation = creature.action->generation;
    homing.target = target;
    homing.attackStarted = creature.action->started;
    homing.stepped = homing.attackStarted;
    creature.homing = homing;
    m_moving.insert(entity.id);
    ++m_counts.attacks;
    m_counts.largest = std::max(m_counts.largest, m_moving.size());
}

void AttackMotion::tick() {
    if (m_moving.empty()) {
        return;
    }
    const WorldClock::time_point now = m_world.clock().now();
    // A step can end a homing: walk a copy of the ids.
    const std::vector<EntityId> moving(m_moving.begin(), m_moving.end());
    for (const EntityId id : moving) {
        Entity* entity = m_creatures.find(id);
        ++m_counts.looked;
        if (entity == nullptr || !entity->creature.homing) {
            m_moving.erase(id);
            continue;
        }
        Creature& creature = entity->creature;
        const HomingMove& homing = *creature.homing;
        // The ends of a homing that are not the end of its clips: the attack does not play any more (a stun, a knockdown, a command, the
        // next action), the death, the way back, a push of a hit, a hold.
        const char* cause = creature.dead                                                                       ? "the creature died"
                            : creature.immune()                                                                 ? "the creature goes back"
                            : !creature.action || creature.action->generation != homing.generation           ? "the attack ended"
                            : creature.held_by_force()                                                          ? "a push of a hit"
                            : creature.held(now)                                                                ? "a stun or a knockdown"
                                                                                                                : nullptr;
        if (cause != nullptr) {
            end(*entity, cause, true);
            continue;
        }
        if (now - homing.stepped < forced_rules::kStep) {
            continue;
        }
        step(*entity, now);
    }
}

bool AttackMotion::step(Entity& entity, WorldClock::time_point now) {
    Creature& creature = entity.creature;
    HomingMove& homing = *creature.homing;
    const gamedata::CreatureAttack& attack = creature.body->attacks[homing.attack];
    const float sinceAttack = seconds_of(now - homing.attackStarted);
    const Entity* target = homing.target != 0 ? m_world.find(homing.target) : nullptr;
    // The start of the next clip at its time in the fragment.
    if (!homing.running) {
        if (homing.clip >= attack.homing.size()) {
            end(entity, "its clips ended", false);
            return false;
        }
        const gamedata::HomingClip& clip = attack.homing[homing.clip];
        if (sinceAttack < clip.start) {
            return true;
        }
        homing.running = true;
        homing.clipStarted = homing.attackStarted + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(clip.start));
        homing.stepped = homing.clipStarted;
        homing.origin = entity.position;
        homing.scanYaw = creature.yaw;
        homing.turnDone = false;
        homing.moveDone = !clip.moveToTarget;
        ++m_counts.clips;
        if (target == nullptr || !in_wedge(clip, entity.position, creature.yaw, target->position)) {
            // No target that passes the wedge: the clip does nothing (client code 0x255A140).
            homing.turnDone = true;
            homing.moveDone = true;
            ++m_counts.clipsOutOfWedge;
        } else {
            homing.aim = target->position;
        }
    }
    const gamedata::HomingClip& clip = attack.homing[homing.clip];
    const float clipEnd = clip.duration >= 0.0F ? clip.start + clip.duration : attack.length;
    const WorldClock::time_point at =
        std::min(now, homing.attackStarted + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(clipEnd)));
    const float dt = std::max(seconds_of(at - homing.stepped), 0.0F);
    const float t = std::max(seconds_of(at - homing.clipStarted), 0.0F);
    homing.stepped = at;
    // The target place: the target while it passes the wedge (from the origin along the direction of the start), else the last place;
    // the place of the start with LockInitialTargetPos.
    if (!clip.lockTarget && target != nullptr && !(homing.turnDone && homing.moveDone)
        && in_wedge(clip, homing.origin, homing.scanYaw, target->position)) {
        homing.aim = target->position;
    }
    // The turn (client code 0x25666A0).
    if (!homing.turnDone && dt > 0.0F && horizontal_distance(entity.position, homing.aim) > 0.01F) {
        const float left = signed_turn(creature.yaw, yaw_to(entity.position, homing.aim));
        // An exponential clip with a TurnRate of 1 or more (1080 and 3.0 in the data) turns the whole angle (a decision of an agent: the
        // formula of 0x25666A0 has no meaning for a part above 1; the client limits the step to the angle).
        float turn = clip.exponential ? (1.0F - std::pow(1.0F - std::clamp(clip.turnRate, 0.0F, 1.0F), 30.0F * dt)) * left
                                      : std::copysign(std::min(std::fabs(left), clip.turnRate * dt), left);
        if (std::fabs(turn) > std::fabs(left)) {
            turn = left;
        }
        creature.yaw = std::fmod(creature.yaw + turn + 360.0F, 360.0F);
        if (std::fabs(left - turn) < kTurnDone && !clip.turnAfterTarget) {
            homing.turnDone = true;
        }
    }
    // The move (client code 0x2564FC0, 0x2565210, 0x25653F0).
    bool moved = false;
    if (!homing.moveDone && dt > 0.0F) {
        const float away = horizontal_distance(entity.position, homing.aim);
        if (away <= clip.moveToDistance) {
            homing.moveDone = true; // The target is in MoveToDistance: no pull back (a decision of an agent, research Q1).
        } else {
            const float ux = (homing.aim.x - entity.position.x) / away;
            const float uy = (homing.aim.y - entity.position.y) / away;
            state::WorldPosition dest{homing.aim.x - ux * clip.moveToDistance, homing.aim.y - uy * clip.moveToDistance, entity.position.z};
            const float fromOrigin = horizontal_distance(homing.origin, dest);
            if (clip.maxMoveDistance > 0.0F && fromOrigin > clip.maxMoveDistance) {
                const float scale = clip.maxMoveDistance / fromOrigin;
                dest.x = homing.origin.x + (dest.x - homing.origin.x) * scale;
                dest.y = homing.origin.y + (dest.y - homing.origin.y) * scale;
            }
            float gx = entity.position.x;
            float gy = entity.position.y;
            if (clip.moveToVelocity > 0.0F) {
                // The velocity rule (0x25653F0): a step of MoveToVelocity x dt to dest.
                const float left = horizontal_distance(entity.position, dest);
                const float way = std::min(left, clip.moveToVelocity * dt);
                if (left > 1.0e-4F) {
                    gx += (dest.x - entity.position.x) / left * way;
                    gy += (dest.y - entity.position.y) / left * way;
                }
            } else {
                // The time rule (0x2565210): the smoothstep in MoveToDuration, at most MaxMoveDistance / MoveToDuration x dt in a step.
                const float u = clip.moveToDuration > 0.0F ? std::min(t / clip.moveToDuration, 1.0F) : 1.0F;
                const float w = u * u * (3.0F - 2.0F * u);
                float dx = homing.origin.x + w * (dest.x - homing.origin.x) - entity.position.x;
                float dy = homing.origin.y + w * (dest.y - homing.origin.y) - entity.position.y;
                const float length = std::hypot(dx, dy);
                if (clip.maxMoveDistance > 0.0F && clip.moveToDuration > 0.0F) {
                    const float limit = clip.maxMoveDistance / clip.moveToDuration * dt;
                    if (length > limit && length > 0.0F) {
                        dx *= limit / length;
                        dy *= limit / length;
                    }
                }
                gx += dx;
                gy += dy;
                if (t >= clip.moveToDuration) {
                    homing.moveDone = true;
                }
            }
            const state::WorldPosition goal{gx, gy, entity.position.z};
            if (horizontal_distance(goal, entity.position) > 0.0005F) {
                const Slide slide = m_world.navigation().slide(entity.position, goal);
                if (!slide.waiting) {
                    const float step = horizontal_distance(entity.position, slide.position);
                    if (step > 0.0005F || std::fabs(slide.position.z - entity.position.z) > 0.0005F) {
                        homing.travelled += step;
                        m_creatures.move(entity, slide.position, Gait::place);
                        moved = true;
                        ++m_counts.steps;
                    }
                    if (slide.blocked) {
                        homing.moveDone = true; // A wall or a drop ends the move of the clip.
                        ++m_counts.blocked;
                    }
                }
            }
            if (horizontal_distance(entity.position, dest) < kAtDest && !clip.moveAfterTarget) {
                homing.moveDone = true;
            }
        }
    }
    // The direction goes to the observers with a new place, or alone when it changed enough.
    if (!moved && std::fabs(signed_turn(creature.mind.sentYaw, creature.yaw)) >= kTurnSent) {
        m_creatures.moved(entity);
        ++m_counts.turns;
    }
    if (moved || std::fabs(signed_turn(creature.mind.sentYaw, creature.yaw)) >= kTurnSent) {
        creature.mind.sentYaw = creature.yaw;
    }
    // The end of the clip.
    if (at >= homing.attackStarted + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(clipEnd))
        || (homing.turnDone && homing.moveDone && clip.duration < 0.0F)) {
        homing.running = false;
        ++homing.clip;
        if (homing.clip >= attack.homing.size()) {
            end(entity, "its clips ended", false);
            return false;
        }
    }
    return true;
}

void AttackMotion::stop(Entity& entity, const char* cause) {
    if (entity.creature.homing) {
        end(entity, cause, true);
    }
}

void AttackMotion::end(Entity& entity, const char* cause, bool interrupted) {
    m_moving.erase(entity.id);
    if (!entity.creature.homing) {
        return;
    }
    const HomingMove homing = *entity.creature.homing;
    entity.creature.homing.reset();
    ++(interrupted ? m_counts.interrupted : m_counts.ended);
    if (homing.travelled > 0.0F) {
        // The trail of a fight keeps the new place (the way back), and the path stays while the creature is near it (Behavior).
        m_creatures.behavior().moved_by_force(entity);
    }
    const gamedata::CreatureSlice& body = *entity.creature.body;
    log::write("world: homing: creature %016llx (%s) moved %.2f m in the attack %s (%zu clip(s)): %s",
               id_number(entity.id),
               entity.creature.kind->id.c_str(),
               static_cast<double>(homing.travelled),
               homing.attack < body.attacks.size() ? body.actions[body.attacks[homing.attack].action].name.c_str() : "?",
               homing.attack < body.attacks.size() ? body.attacks[homing.attack].homing.size() : std::size_t{0},
               cause);
}

} // namespace soulwarden::backend::world

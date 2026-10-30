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
    const auto at_time = [&](float seconds) {
        return homing.attackStarted + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(seconds));
    };
    // The start of each clip at its time in the fragment, also while an earlier clip runs (code review of stage U3, finding U3-R6: the
    // exporter collects the clips of each procedural layer, and 32 attacks of the data have clips that overlap).
    while (homing.clip < attack.homing.size() && attack.homing[homing.clip].start <= sinceAttack) {
        const gamedata::HomingClip& clip = attack.homing[homing.clip];
        HomingClipRun run;
        run.clip = homing.clip;
        run.clipStarted = at_time(clip.start);
        run.stepped = run.clipStarted;
        run.origin = entity.position;
        run.curve = entity.position;
        run.scanYaw = creature.yaw;
        run.moveDone = !clip.moveToTarget;
        ++m_counts.clips;
        if (target == nullptr || !in_wedge(clip, entity.position, creature.yaw, target->position)) {
            // No target that passes the wedge: the clip does nothing (client code 0x255A140).
            run.turnDone = true;
            run.moveDone = true;
            ++m_counts.clipsOutOfWedge;
        } else {
            run.aim = target->position;
        }
        homing.runs.push_back(run);
        ++homing.clip;
    }
    // No clip runs: the next one waits for its time (each tick looks; the steps of 50 ms count from the start of a clip).
    if (homing.runs.empty()) {
        if (homing.clip >= attack.homing.size()) {
            end(entity, "its clips ended", false);
            return false;
        }
        return true;
    }
    homing.stepped = now;
    // A root holds the place of the creature (code review of stage U3, finding U3-R1; the rule of the behavior): the turn and the attack go
    // on, the curve of the time rule goes on with no travel (the blocked travel does not come back at the end of the root).
    const bool rooted = creature.rooted(now);
    bool moved = false;
    for (HomingClipRun& run : homing.runs) {
        const gamedata::HomingClip& clip = attack.homing[run.clip];
        const float clipEnd = clip.duration >= 0.0F ? clip.start + clip.duration : attack.length;
        const WorldClock::time_point at = std::min(now, at_time(clipEnd));
        const float dt = std::max(seconds_of(at - run.stepped), 0.0F);
        const float t = std::max(seconds_of(at - run.clipStarted), 0.0F);
        run.stepped = at;
        // The target place: the target while it passes the wedge (from the origin along the direction of the start), else the last place;
        // the place of the start with LockInitialTargetPos.
        if (!clip.lockTarget && target != nullptr && !(run.turnDone && run.moveDone) && in_wedge(clip, run.origin, run.scanYaw, target->position)) {
            run.aim = target->position;
        }
        // The turn (client code 0x25666A0).
        if (!run.turnDone && dt > 0.0F && horizontal_distance(entity.position, run.aim) > 0.01F) {
            const float left = signed_turn(creature.yaw, yaw_to(entity.position, run.aim));
            // An exponential clip with a TurnRate of 1 or more (1080 and 3.0 in the data) turns the whole angle (a decision of an agent: the
            // formula of 0x25666A0 has no meaning for a part above 1; the client limits the step to the angle).
            float turn = clip.exponential ? (1.0F - std::pow(1.0F - std::clamp(clip.turnRate, 0.0F, 1.0F), 30.0F * dt)) * left
                                          : std::copysign(std::min(std::fabs(left), clip.turnRate * dt), left);
            if (std::fabs(turn) > std::fabs(left)) {
                turn = left;
            }
            creature.yaw = std::fmod(creature.yaw + turn + 360.0F, 360.0F);
            if (std::fabs(left - turn) < kTurnDone && !clip.turnAfterTarget) {
                run.turnDone = true;
            }
        }
        // The move (client code 0x2564FC0, 0x2565210, 0x25653F0).
        if (run.moveDone || !(dt > 0.0F)) {
            continue;
        }
        const float away = horizontal_distance(entity.position, run.aim);
        if (away <= clip.moveToDistance) {
            run.moveDone = true; // The target is in MoveToDistance: no pull back (a decision of an agent, research Q1).
            continue;
        }
        // The line to the target: from the place of the creature (the velocity rule), or from the place of the curve (the time rule; finding
        // U3-R2: a push between two steps does not move the end of the curve).
        const state::WorldPosition& from = clip.moveToVelocity > 0.0F ? entity.position : run.curve;
        const float line = std::max(horizontal_distance(from, run.aim), 1.0e-4F);
        const float ux = (run.aim.x - from.x) / line;
        const float uy = (run.aim.y - from.y) / line;
        state::WorldPosition dest{run.aim.x - ux * clip.moveToDistance, run.aim.y - uy * clip.moveToDistance, entity.position.z};
        const float fromOrigin = horizontal_distance(run.origin, dest);
        if (clip.maxMoveDistance > 0.0F && fromOrigin > clip.maxMoveDistance) {
            const float scale = clip.maxMoveDistance / fromOrigin;
            dest.x = run.origin.x + (dest.x - run.origin.x) * scale;
            dest.y = run.origin.y + (dest.y - run.origin.y) * scale;
        }
        float dx = 0.0F;
        float dy = 0.0F;
        if (clip.moveToVelocity > 0.0F) {
            // The velocity rule (0x25653F0): a step of MoveToVelocity x dt to dest, from the place of the creature.
            const float left = horizontal_distance(entity.position, dest);
            const float way = std::min(left, clip.moveToVelocity * dt);
            if (left > 1.0e-4F) {
                dx = (dest.x - entity.position.x) / left * way;
                dy = (dest.y - entity.position.y) / left * way;
            }
        } else {
            // The time rule (0x2565210): the smoothstep in MoveToDuration, at most MaxMoveDistance / MoveToDuration x dt in a step. The step is
            // the change of the place of the curve (finding U3-R2: a push of an additive reaction between two steps stays).
            const float u = clip.moveToDuration > 0.0F ? std::min(t / clip.moveToDuration, 1.0F) : 1.0F;
            const float w = u * u * (3.0F - 2.0F * u);
            const state::WorldPosition curve{run.origin.x + w * (dest.x - run.origin.x), run.origin.y + w * (dest.y - run.origin.y), run.origin.z};
            dx = curve.x - run.curve.x;
            dy = curve.y - run.curve.y;
            const float length = std::hypot(dx, dy);
            if (clip.maxMoveDistance > 0.0F && clip.moveToDuration > 0.0F) {
                const float limit = clip.maxMoveDistance / clip.moveToDuration * dt;
                if (length > limit && length > 0.0F) {
                    dx *= limit / length;
                    dy *= limit / length;
                }
            }
            run.curve.x += dx;
            run.curve.y += dy;
            if (t >= clip.moveToDuration) {
                run.moveDone = true;
            }
        }
        const state::WorldPosition goal{entity.position.x + dx, entity.position.y + dy, entity.position.z};
        if (!rooted && horizontal_distance(goal, entity.position) > 0.0005F) {
            const Slide slide = m_world.navigation(entity.space).slide(entity.position, goal);
            if (!slide.waiting) {
                const float travel = horizontal_distance(entity.position, slide.position);
                if (travel > 0.0005F || std::fabs(slide.position.z - entity.position.z) > 0.0005F) {
                    homing.travelled += travel;
                    m_creatures.move(entity, slide.position, Gait::place);
                    moved = true;
                    ++m_counts.steps;
                }
                if (slide.blocked) {
                    run.moveDone = true; // A wall or a drop ends the move of the clip.
                    ++m_counts.blocked;
                }
            }
        }
        if (horizontal_distance(entity.position, dest) < kAtDest && !clip.moveAfterTarget) {
            run.moveDone = true;
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
    // The end of each clip: its time, or its turn and its move done for a clip with no duration.
    std::erase_if(homing.runs, [&](const HomingClipRun& run) {
        const gamedata::HomingClip& clip = attack.homing[run.clip];
        const float clipEnd = clip.duration >= 0.0F ? clip.start + clip.duration : attack.length;
        return now >= at_time(clipEnd) || (run.turnDone && run.moveDone && clip.duration < 0.0F);
    });
    if (homing.clip >= attack.homing.size() && homing.runs.empty()) {
        end(entity, "its clips ended", false);
        return false;
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

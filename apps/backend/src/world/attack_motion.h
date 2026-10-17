#pragma once

#include <chrono>
#include <cstdint>
#include <set>

#include "world/entity.h"
#include "world/ids.h"
#include "world/world_clock.h"

// The homing of the attack of a creature (stage U3, research creature-combat-feel.md Q1; protocol notes 4.14). It is a part of the
// creature system (world/creatures.h): only that system moves a creature.
//
// The game data has the Homing clips of each attack fragment (gamedata::HomingClip: the clips of the variant of the attack; 2,055 clips
// that move in the attacks of the supported build). The client applies a clip only to its own character: a creature has no target in the
// client (the AI target bus has its handler only in the server facet) and its movement step runs only for the local player (client code
// 0x255A140, 0x2564160, 0x255DA20, 0x2A28FD0; live check LC-Q1-2: in a lunge and a bite of a wolf the client entered the clips and asked
// the AI target, and it did not call the move 0x2565210 or the accumulator 0x2A21FB0). The client puts a creature at the place of the
// server. Thus the server moves and turns the creature with the rule of the client code, and each observer gets its movement state:
//
//   Start.     Each clip starts at its time, also while an earlier clip runs (code review of stage U3, finding U3-R6: the clips of an
//              attack can overlap; each one has its own run, HomingClipRun). At the start of a clip the target must pass the wedge of the clip from the place of the
//              creature along its direction: the horizontal distance below Radius (with UseRadiusforAITarget), the height below Height,
//              the angle not above MaxAngle (0x5C6A000). Else the clip does nothing (0x255A140: no target sets "turn done" and "move
//              done"). Later the target place follows the target while it passes the wedge, else it keeps the last place that passed;
//              with LockInitialTargetPos it keeps the place of the start.
//   Turn.      TurnRate degrees each second, or with UseExponential (1 - (1 - TurnRate)^(30 dt)) of the angle that is left (0x25666A0); at
//              most the angle. It ends at the target angle, unless AllowRotationAfterReachingTargetAngle.
//   Move.      With MoveToTarget: the place `dest` is MoveToDistance from the target on the line to it, and not more than MaxMoveDistance
//              from the origin of the clip (0: no limit). With MoveToVelocity 0 (0x2565210): u = min(t / MoveToDuration, 1), w = u^2 (3 - 2u),
//              the curve place origin + w (dest - origin) with dest on the line from the curve place; the step is the change of the curve
//              place (finding U3-R2: a push of an additive reaction stays), at most MaxMoveDistance / MoveToDuration x dt. With MoveToVelocity above 0
//              (0x25653F0, a literal reading of the code): a step of MoveToVelocity x dt to dest. The move ends at MoveToDuration, at dest
//              (unless AllowMovementAfterReachingTargetPos), and when the target is MoveToDistance or nearer (a decision of an agent: the
//              client pulls the entity back to its curve in that frame; research Q1 phase A).
//   Steps.     A new place at most each forced_rules::kStep (50 ms) of the world clock, on the line of the navigation (Navigation::slide):
//              a wall or a drop ends the move of the clip. The height is the height of the floor (no move up: the creature stays on its
//              floor). The attack record stays (Gait::place). A root holds the place (finding U3-R1): the turn goes on, the curve goes on
//              with no travel.
//   Ends.      The end of the clip (its own duration, else the end of the attack), the end or the replacement of the attack (a stun, a
//              knockdown, a command, the next action), the death, the way back, and the push of a full reaction (it replaces the
//              homing; the push of an additive reaction adds to it: ForcedMove::additive).
//
// The root motion of the animations (phase B of the research) is not done: live check LC-Q1-1 showed no travel of the body of a remote
// creature in a lunge (the client plays the animation in place). The cost: a step looks only at the creatures with a homing that runs
// (`m_moving`), each one at most each 50 ms. World thread only.

namespace soulwarden::backend::world {

class World;
class Creatures;

/** Counts of the homing of the attacks, for the log and for the tests. */
struct HomingCounts {
    /** Attacks with Homing clips that started, clips that started, clips that did nothing (the target out of the wedge). */
    std::uint64_t attacks{0};
    std::uint64_t clips{0};
    std::uint64_t clipsOutOfWedge{0};
    /** New places, turns that sent a state, moves that a wall or a drop stopped, and homings that a different cause ended. */
    std::uint64_t steps{0};
    std::uint64_t turns{0};
    std::uint64_t blocked{0};
    std::uint64_t ended{0};
    std::uint64_t interrupted{0};
    /** The creatures that the steps looked at, and the largest number at one time. */
    std::uint64_t looked{0};
    std::size_t largest{0};
};

class AttackMotion {
public:
    AttackMotion(World& world, Creatures& creatures);

    /**
     * A creature started an attack (Behavior::start_attack, after Creatures::start_action): the Homing clips of the attack run on the world
     * clock. No clip: nothing.
     */
    void start(Entity& creature, EntityId target, std::uint32_t attackIndex);
    /** The creature system calls it in each tick: one step of each creature with a homing that runs. */
    void tick();
    /** The homing of a creature ends now (a push, the death, the way back). */
    void stop(Entity& creature, const char* cause);
    /** The world removed an entity. */
    void removed(EntityId id) {
        m_moving.erase(id);
    }
    [[nodiscard]] bool moving(EntityId id) const {
        return m_moving.count(id) != 0;
    }
    [[nodiscard]] std::size_t moving_count() const {
        return m_moving.size();
    }
    [[nodiscard]] const HomingCounts& counts() const {
        return m_counts;
    }

private:
    /** One step of a creature. @return False when its homing ended. */
    bool step(Entity& creature, WorldClock::time_point now);
    void end(Entity& creature, const char* cause, bool interrupted);

    World& m_world;
    Creatures& m_creatures;
    std::set<EntityId> m_moving;
    HomingCounts m_counts;
};

} // namespace soulwarden::backend::world

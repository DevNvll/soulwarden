#pragma once

#include <chrono>
#include <cstdint>
#include <set>
#include <string_view>

#include "gamedata/combat_tables.h"
#include "world/entity.h"
#include "world/ids.h"
#include "world/world_clock.h"

// The forced movement of a creature (stage 3.3b-S4, protocol notes 4.26): the pull and the push of a hit. It is a part
// of the creature system (world/creatures.h): only that system moves a creature.
//
// The game data has the impact of each damage row (gamedata::DamageImpact: the columns ImpactDistanceX, Y, Z and
// ImpactDecayRate, and the "_AI" columns for a target that is a creature). The client moves a body by them in the
// activity ReactionMotion of its reaction action (the action lists of the creatures: `Reaction` of baseai and of the
// wolf, Scale 1); live check L7 showed that the client does not move a creature of the server by itself. Thus the world
// moves the creature with the rule of the client code, and each observer gets its movement state:
//
//   Direction.  From the source of the hit (the attacker for a melee hit, the center of an area or of a spell; the
//               attacker for a row with UseAttackerPosForReaction) to the creature, in the plane. The displacement is
//               direction * Y + side * X (side = direction x up), client code at image RVA 0x48A0690. A creature at
//               the source has the direction north (the default direction of that code).
//   Decay > 0.  The travel at the time t of the reaction is |displacement| * (1 - decay^(30 t)) (client code at image
//               RVA 0x48A0D30): Maelstrom (Y -1.5, decay 0.8) pulls 1.5 m, 74 % of it in 0.2 s.
//   Decay <= 0. A constant speed of |displacement| meters in each second to the source point for a negative Y (a pull),
//               away from it for a positive Y; a pull ends 0.1 m from the point (client code at image RVA 0x488B8F0):
//               the vortex of Gravity Well (Y -7) pulls at 7 m/s to its center.
//   Length.     The movement runs for the length of the reaction of the hit at most (user test 2: the length of the reaction
//               fragment of the power of the damage row, Creatures::reaction_length; protocol notes 4.26: the client pushes a
//               creature to the end of the reaction fragment). A decay travel ends when less than forced_rules::kRest is left.
//   Navigation. Each step goes on the straight line of the navigation data (Navigation::slide): a wall or a drop
//               stops the creature, and the movement ends there. The height is the height of the floor.
//   Rules.      A dead creature and a creature on its way back (immune) do not move. A new impact replaces the movement
//               that runs (the client starts its reaction motion again). While it moves, the behavior does not move the
//               creature and starts no attack (but for the push of an additive reaction, stage U3: the behavior and the homing
//               go on and the push adds to them; it does not replace the push of a full reaction that runs); at its end the
//               creature goes on with its path when it is near it (user test
//               2, UT2-4: the navigation keeps a path that is navigation_rules::kOffPath or less away, and takes the free
//               straight line at once for one that is farther), and its trail gets the new place (Behavior::moved_by_force).
//
// The cost: a step looks only at the creatures that a force moves now (`m_moving`), each one at most each `kStep` of
// the world clock, and at the cells of its short line. World thread only.

namespace soulwarden::backend::world {

class World;
class Creatures;

/** The constants of the forced movement that the game data does not give (rules of this project). */
namespace forced_rules {
/** A creature that a force moves gets a new place at most each this time of the world clock (20 movement states in a second). */
inline constexpr std::chrono::milliseconds kStep{50};
/** Frames of the client in one second: the decay rate is a rate for each frame (client code at image RVA 0x48A0D30). */
inline constexpr float kFrames = 30.0F;
/** A pull to a point ends this near to the point (meters; client code at image RVA 0x488B8F0). */
inline constexpr float kPointStop = 0.1F;
/** A displacement shorter than this (meters) moves nothing. */
inline constexpr float kShortest = 0.01F;
/**
 * A decay travel ends when less than this (meters) of it is left (user test 2, UT2-4; a decision of an agent): the travel of
 * GreatAxe_PrimaryAttack (1.0 m, decay 0.85) is 97 percent complete after 0.33 s, and the end at 1 mm held the creature for 1.47 s.
 */
inline constexpr float kRest = 0.02F;
/** A creature nearer than this to the source of the hit (meters) has the default direction (north). */
inline constexpr float kNoDirection = 0.05F;
} // namespace forced_rules

/** Counts of the forced movement, for the log and for the tests. */
struct ForcedCounts {
    /** Movements that started, that a new impact replaced, and impacts that a creature did not take (dead, immune). */
    std::uint64_t started{0};
    std::uint64_t replaced{0};
    std::uint64_t refused{0};
    /** Pushes of an additive reaction that started (stage U3), and that did not start because the push of a full reaction runs. */
    std::uint64_t additive{0};
    std::uint64_t keptFull{0};
    /** Steps (new places), steps that waited for the navigation data, movements that a wall or a drop stopped, and that ended. */
    std::uint64_t steps{0};
    std::uint64_t waits{0};
    std::uint64_t blocked{0};
    std::uint64_t ended{0};
    /** The creatures that the steps looked at, and the largest number of moving creatures at one time. */
    std::uint64_t looked{0};
    std::size_t largest{0};
};

class ForcedMotion {
public:
    ForcedMotion(World& world, Creatures& creatures);

    /**
     * A hit with an impact (gamedata::DamageImpact) hurt a creature: the creature moves by it.
     * @param source The source of the hit: the attacker of a melee hit, the center of an area or of a spell.
     * @param attacker The place of the attacker (a row with UseAttackerPosForReaction takes the direction from it).
     * @param row The id of the damage row (for the log).
     * @param length The longest time of the movement: the length of the reaction of the hit (Creatures::reaction_length).
     * @param scale The Scale of the ReactionMotion of the action that the hit started (stage U3, research Q5): it scales the displacement.
     * @param pushBack Its PushBackScale: it scales only a displacement away from the source (a push, not a pull; client code 0x48A0690).
     * @param additive True for the push of an additive reaction (ForcedMove::additive): the behavior goes on, and the push adds to its
     *                 movement. It does not replace the push of a full reaction that runs.
     * @return True when the creature moves.
     */
    bool impact(Entity& creature, const gamedata::DamageImpact& impact, const state::WorldPosition& source,
                const state::WorldPosition& attacker, std::string_view row, std::chrono::milliseconds length, float scale = 1.0F,
                float pushBack = 1.0F, bool additive = false);

    /** The creature system calls it in each tick: one step of each creature that a force moves. */
    void tick();

    /** The creature died, or it starts its way back (it is immune): its movement ends. */
    void stop(Entity& creature);
    /** The world removed an entity. */
    void removed(EntityId id) {
        m_moving.erase(id);
    }

    /** @return True while a force moves the creature. */
    [[nodiscard]] bool moving(EntityId id) const {
        return m_moving.count(id) != 0;
    }
    [[nodiscard]] std::size_t moving_count() const {
        return m_moving.size();
    }
    [[nodiscard]] const ForcedCounts& counts() const {
        return m_counts;
    }

private:
    /** One step of a creature. @return False when its movement ended. */
    bool step(Entity& creature, WorldClock::time_point now);
    void end(Entity& creature, const char* cause);

    World& m_world;
    Creatures& m_creatures;
    /** The creatures that a force moves now: the step looks only at these. */
    std::set<EntityId> m_moving;
    ForcedCounts m_counts;
};

} // namespace soulwarden::backend::world

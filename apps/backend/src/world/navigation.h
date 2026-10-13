#pragma once

#include <cmath>
#include <optional>

#include "gamedata/height_maps.h"
#include "state/store.h"

// The navigation of a creature: where the ground is, and where the next step to a goal is.
//
// These two functions are the place of the path search. In stage 3.4b they are simple: the
// ground is the terrain (the height maps of the game), and the way to a goal is the straight
// line. Thus a creature goes through a wall, and a creature on a floor of a building has no
// ground here. Stage 3.4d replaces the two bodies: the navigation data that a tool makes from
// the collision shapes of the game has each floor, and the path search goes around a wall.
// The callers do not change then: the behavior of a creature (world/behavior.h) asks only
// these two functions for a place.
//
// World thread only: the terrain source reads its files when a place is asked.

namespace soulwarden::backend::world {

class Navigation {
public:
    /** @param terrain The terrain heights (game data). Null: no place has a ground, and no creature walks. */
    explicit Navigation(gamedata::Terrain* terrain) : m_terrain(terrain) {}

    /** @return True when the navigation has a source for the ground. */
    [[nodiscard]] bool available() const {
        return m_terrain != nullptr;
    }

    /**
     * @return The height of the ground at a position: the surface on which a creature at this
     *         position stands. The position has a height, because one place can have more than
     *         one floor: the result is the floor that is nearest to that height. In this stage
     *         the terrain is the only floor, and the height of the position selects nothing.
     *         No value: the navigation has no ground at the place.
     */
    [[nodiscard]] std::optional<float> ground_height(const state::WorldPosition& position) const {
        if (m_terrain == nullptr || !std::isfinite(position.x) || !std::isfinite(position.y)) {
            return std::nullopt;
        }
        return m_terrain->height(position.x, position.y);
    }

    /**
     * @return The place of a creature after one step from `from` to `goal`: a place on the
     *         ground that is `distance` meters nearer to the goal on its way, or the goal with
     *         the height of the ground when the goal is not farther than that. In this stage
     *         the way is the straight line. No value: the creature cannot make the step (no
     *         ground at the new place).
     * @param distance The length of the step, in meters (the speed times the time).
     */
    [[nodiscard]] std::optional<state::WorldPosition> next_step(const state::WorldPosition& from,
                                                                const state::WorldPosition& goal,
                                                                float distance) const {
        const float dx = goal.x - from.x;
        const float dy = goal.y - from.y;
        const float way = std::hypot(dx, dy);
        if (!std::isfinite(way) || !std::isfinite(distance) || distance <= 0.0F) {
            return std::nullopt;
        }
        state::WorldPosition next = goal;
        if (way > distance) {
            next.x = from.x + dx / way * distance;
            next.y = from.y + dy / way * distance;
        }
        const std::optional<float> ground = ground_height(state::WorldPosition{next.x, next.y, from.z});
        if (!ground) {
            return std::nullopt;
        }
        next.z = *ground;
        return next;
    }

private:
    gamedata::Terrain* m_terrain;
};

} // namespace soulwarden::backend::world

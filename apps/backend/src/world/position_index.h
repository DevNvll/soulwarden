#pragma once

#include <climits>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <utility>

#include "state/store.h"
#include "world/ids.h"

// Position index of the entity table: a grid of square cells in the horizontal plane, with
// the entity ids of each cell. The interest table of a session asks it for the entities near
// its character (roadmap, rule MP-8). The height of a position is not a part of the index.

namespace soulwarden::backend::world {

/** @return The horizontal distance between two positions, in meters. */
[[nodiscard]] inline float horizontal_distance(const state::WorldPosition& a, const state::WorldPosition& b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

/**
 * Limits of a position of the world, in meters. The level of the client is some kilometers
 * wide: a number outside these limits is not a position, and the grid of the index cannot
 * hold the cell of a very large number.
 */
inline constexpr float kMaxHorizontalPosition = 1000000.0F;
inline constexpr float kMaxVerticalPosition = 100000.0F;

/** @return True when each number of the position is finite and in the limits of the world. */
[[nodiscard]] inline bool valid_position(const state::WorldPosition& position) {
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z)
           && std::abs(position.x) <= kMaxHorizontalPosition && std::abs(position.y) <= kMaxHorizontalPosition
           && std::abs(position.z) <= kMaxVerticalPosition;
}

/**
 * Limits of the height of a place for a character, in meters: the range of the height of a
 * world position on the wire (-100 to 1000, protocol notes 4.6), with a margin for the spawn
 * lift. The check of a position report refuses the two limit values of that range.
 */
inline constexpr float kLowestTravelHeight = -99.0F;
inline constexpr float kHighestTravelHeight = 999.0F;

/** @return True when a position is a place to which the world can move a character (World::travel). */
[[nodiscard]] inline bool travel_position(const state::WorldPosition& position) {
    return valid_position(position) && position.z >= kLowestTravelHeight && position.z <= kHighestTravelHeight;
}

/**
 * The key of a cell is the space and the cell of the position (stage DG1, docs/systems/instances.md): an entity of one space is never a
 * candidate of a search in a different space, also at the same coordinates (an instance level has its own coordinates from 0).
 */
class PositionIndex {
public:
    /** @param cellSize Edge of one cell, in meters. */
    explicit PositionIndex(float cellSize = 64.0F) : m_cellSize(cellSize) {}

    void insert(EntityId id, SpaceId space, const state::WorldPosition& position) {
        m_cells[cell_of(space, position)].insert(id);
    }

    void erase(EntityId id, SpaceId space, const state::WorldPosition& position) {
        const auto cell = m_cells.find(cell_of(space, position));
        if (cell == m_cells.end()) {
            return;
        }
        cell->second.erase(id);
        if (cell->second.empty()) {
            m_cells.erase(cell);
        }
    }

    void move(EntityId id, SpaceId space, const state::WorldPosition& from, const state::WorldPosition& to) {
        if (cell_of(space, from) != cell_of(space, to)) {
            erase(id, space, from);
            insert(id, space, to);
        }
    }

    /**
     * Calls `visit(EntityId)` for each entity of the space in a cell that the circle touches. An entity of the result can be farther than
     * `range`: the caller compares the exact distance.
     */
    template <class Visit>
    void for_each_candidate(SpaceId space, const state::WorldPosition& center, float range, Visit&& visit) const {
        const Cell low = cell_of(space, state::WorldPosition{center.x - range, center.y - range, 0.0F});
        const Cell high = cell_of(space, state::WorldPosition{center.x + range, center.y + range, 0.0F});
        // One search for each column of cells that the circle touches: the cost is the cells of
        // the circle, not the cells of the world. A circle with more columns than the index has
        // cells (a very large range) walks the cells of the space one time. The sequence of
        // the visits is the same for the two ways: the columns from west to east, the cells of
        // a column from south to north, and the ids of a cell from the lowest one.
        const std::int64_t columns = static_cast<std::int64_t>(high.column) - low.column + 1;
        if (columns > static_cast<std::int64_t>(m_cells.size())) {
            for (auto cell = m_cells.lower_bound(low); cell != m_cells.end(); ++cell) {
                if (cell->first.space != space || cell->first.column > high.column) {
                    break;
                }
                if (cell->first.row < low.row || cell->first.row > high.row) {
                    continue;
                }
                for (const EntityId id : cell->second) {
                    visit(id);
                }
            }
            return;
        }
        for (std::int32_t column = low.column;; ++column) {
            for (auto cell = m_cells.lower_bound(Cell{space, column, low.row});
                 cell != m_cells.end() && cell->first.space == space && cell->first.column == column && cell->first.row <= high.row; ++cell) {
                for (const EntityId id : cell->second) {
                    visit(id);
                }
            }
            if (column == high.column) {
                break;
            }
        }
    }

    /** Calls `visit(EntityId)` for each entity of a space (stage DG1: the end of an instance). The cost is the cells of that space. */
    template <class Visit>
    void for_each_in_space(SpaceId space, Visit&& visit) const {
        for (auto cell = m_cells.lower_bound(Cell{space, INT32_MIN, INT32_MIN}); cell != m_cells.end() && cell->first.space == space; ++cell) {
            for (const EntityId id : cell->second) {
                visit(id);
            }
        }
    }

    /** @return The number of cells that have an entity, in all spaces. */
    [[nodiscard]] std::size_t cell_count() const {
        return m_cells.size();
    }

private:
    struct Cell {
        SpaceId space;
        std::int32_t column;
        std::int32_t row;

        auto operator<=>(const Cell&) const = default;
    };

    [[nodiscard]] Cell cell_of(SpaceId space, const state::WorldPosition& position) const {
        return Cell{space, static_cast<std::int32_t>(std::floor(position.x / m_cellSize)),
                    static_cast<std::int32_t>(std::floor(position.y / m_cellSize))};
    }

    float m_cellSize;
    std::map<Cell, std::set<EntityId>> m_cells;
};

} // namespace soulwarden::backend::world

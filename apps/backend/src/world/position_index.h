#pragma once

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

class PositionIndex {
public:
    /** @param cellSize Edge of one cell, in meters. */
    explicit PositionIndex(float cellSize = 64.0F) : m_cellSize(cellSize) {}

    void insert(EntityId id, const state::WorldPosition& position) {
        m_cells[cell_of(position)].insert(id);
    }

    void erase(EntityId id, const state::WorldPosition& position) {
        const auto cell = m_cells.find(cell_of(position));
        if (cell == m_cells.end()) {
            return;
        }
        cell->second.erase(id);
        if (cell->second.empty()) {
            m_cells.erase(cell);
        }
    }

    void move(EntityId id, const state::WorldPosition& from, const state::WorldPosition& to) {
        if (cell_of(from) != cell_of(to)) {
            erase(id, from);
            insert(id, to);
        }
    }

    /**
     * Calls `visit(EntityId)` for each entity in a cell that the circle touches. An entity of
     * the result can be farther than `range`: the caller compares the exact distance.
     */
    template <class Visit>
    void for_each_candidate(const state::WorldPosition& center, float range, Visit&& visit) const {
        const Cell low = cell_of(state::WorldPosition{center.x - range, center.y - range, 0.0F});
        const Cell high = cell_of(state::WorldPosition{center.x + range, center.y + range, 0.0F});
        for (auto cell = m_cells.lower_bound(Cell{low.first, low.second}); cell != m_cells.end(); ++cell) {
            if (cell->first.first > high.first) {
                break;
            }
            if (cell->first.second < low.second || cell->first.second > high.second) {
                continue;
            }
            for (const EntityId id : cell->second) {
                visit(id);
            }
        }
    }

    /** @return The number of cells that have an entity. */
    [[nodiscard]] std::size_t cell_count() const {
        return m_cells.size();
    }

private:
    using Cell = std::pair<std::int32_t, std::int32_t>;

    [[nodiscard]] Cell cell_of(const state::WorldPosition& position) const {
        return Cell{static_cast<std::int32_t>(std::floor(position.x / m_cellSize)),
                    static_cast<std::int32_t>(std::floor(position.y / m_cellSize))};
    }

    float m_cellSize;
    std::map<Cell, std::set<EntityId>> m_cells;
};

} // namespace soulwarden::backend::world

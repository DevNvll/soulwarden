#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "state/store.h"

// The chests of the world (stage 3.5b.1, docs/systems/chests.md): each chest placement of the main world with what an open needs: the
// gatherable row of its variation (the open time, the reset values), its loot root, the loot tags and the content level of its
// territories, and its state for this backend (ordinary, or unavailable with a reason).
//
// The data is game data: the tool tools/gamedata/chests.sh makes the file from the game copy of the user, and the file stays outside
// the repository. Without the file no chest opens (a request is refused: the handler has no chest), and the rest of the backend works.
//
// The cost does not depend on the size of the world (roadmap rule MP-8): a request finds its chest by the placement key (a hash), and a
// lookup near a character looks at the cells of 64 m near it.

namespace soulwarden::backend::gamedata {

/** File name of the chests in the game data directory of the backend. */
inline constexpr const char* kChestDataFile = "chest-data.json";

/** The kind of a chest (a table of the tool from the loot roots of the data). */
enum class ChestKind : std::uint8_t {
    supply,
    provision,
    alchemy,
    elite,
    other,
};

[[nodiscard]] const char* chest_kind_text(ChestKind kind);

/** The values of a gatherable row of a chest (GatherableData of the game). */
struct ChestGatherable {
    std::string id;
    /** BaseGatherTime: the time of an open (seconds). */
    float openSeconds{0.0F};
    /** MinRespawnRate and MaxRespawnRate (seconds; 0 and 0: the row has no reset of its own). */
    float resetMin{0.0F};
    float resetMax{0.0F};
    bool elite{false};
    /** GameEventID: the game event of an open (its rewards are not given in stage 3.5b.1). */
    std::string event;
};

/** One chest. */
struct ChestDefinition {
    /** The placement UUID: the durable key of the chest (the key of its object in the interaction data). */
    std::string key;
    /** The managed actor id of the placement (the UUID rule): the key of its LootTracker entry. */
    std::array<std::uint8_t, 16> actor{};
    state::WorldPosition position;
    std::string variant;
    /** The name of the prompt (a localization key of the variation row). */
    std::string name;
    /** An index into ChestData::gatherables. -1: no gatherable row (an unavailable chest). */
    std::int32_t gatherable{-1};
    /** The loot root (a loot table id). */
    std::string root;
    ChestKind kind{ChestKind::other};
    /** The loot tags of its territories (LootTags of the territory tables: "Everfall"). */
    std::vector<std::string> tags;
    /** The largest creature level of its territories (AIVariantLevelOverride). No value: none. */
    std::optional<std::uint32_t> contentLevel;
    /** False: unavailable (a gate that the backend has no predicate for), with its reason. */
    bool available{false};
    std::string reason;
    std::string region;
};

/** The values of the base attributes of a player for the instanced chests. */
struct ChestRules {
    /** Instanced Loot Chest Reset Time Mins: the reset of a chest whose gatherable row has no reset of its own. */
    std::uint32_t resetMinutes{0};
    /** Max Instanced Loot Chest Count (kept, not a limit: docs/systems/chests.md). */
    std::uint32_t countLimit{0};
};

/** The work of one lookup near a place (case LC8). */
struct ChestLookupWork {
    std::uint64_t cells{0};
    std::uint64_t chests{0};
};

class ChestData {
public:
    /** Edge of a cell of the grid (meters). */
    static constexpr float kCellSize = 64.0F;

    /**
     * Reads the file of the tool. A file that is not complete is not used, as a whole: a member with a wrong form, a number that is not
     * finite or not in its range, a chest key two times, a chest whose gatherable row the file does not have, an ordinary chest with no
     * gatherable row or no root, and an unavailable chest with no reason.
     * @return False when the file is not there or is not complete. `error()` then has the cause.
     */
    [[nodiscard]] bool load(const std::string& path);
    /** Sets the data with no file (tests), with the same checks. @return False when a part is not correct: no chest then. */
    [[nodiscard]] bool set(ChestRules rules, std::vector<ChestGatherable> gatherables, std::vector<ChestDefinition> chests);

    [[nodiscard]] bool loaded() const {
        return m_rules.resetMinutes != 0;
    }
    [[nodiscard]] const std::string& error() const {
        return m_error;
    }
    [[nodiscard]] const ChestRules& rules() const {
        return m_rules;
    }
    [[nodiscard]] const std::vector<ChestDefinition>& chests() const {
        return m_chests;
    }
    [[nodiscard]] const std::vector<ChestGatherable>& gatherables() const {
        return m_gatherables;
    }
    /** @return The gatherable row of a chest, or null. */
    [[nodiscard]] const ChestGatherable* gatherable_of(const ChestDefinition& chest) const {
        return chest.gatherable >= 0 ? &m_gatherables[static_cast<std::size_t>(chest.gatherable)] : nullptr;
    }
    /** @return The index of the chest of a placement key (any case), or no value. */
    [[nodiscard]] std::optional<std::uint32_t> find(std::string_view key) const;
    /** @return The number of ordinary chests. */
    [[nodiscard]] std::size_t ordinary() const {
        return m_ordinary;
    }

    /**
     * Calls `visit(std::uint32_t index)` for each chest whose place is `range` or less from `center` (horizontal). It looks only at
     * the cells near the center. @param work When not null: gets the work of the lookup.
     */
    template <class Visit>
    void for_each_near(const state::WorldPosition& center, float range, Visit&& visit, ChestLookupWork* work = nullptr) const;

    /** @return The cell key of a place. */
    [[nodiscard]] static std::int64_t cell_of(float x, float y) {
        const auto cell = [](float value) { return static_cast<std::int32_t>(std::floor(value / kCellSize)); };
        return cell_key(cell(x), cell(y));
    }
    [[nodiscard]] static std::int64_t cell_key(std::int32_t x, std::int32_t y) {
        return (static_cast<std::int64_t>(x) << 32) ^ static_cast<std::uint32_t>(y);
    }

private:
    ChestRules m_rules;
    std::vector<ChestGatherable> m_gatherables;
    std::vector<ChestDefinition> m_chests;
    std::unordered_map<std::string, std::uint32_t> m_byKey;
    std::unordered_map<std::int64_t, std::vector<std::uint32_t>> m_cells;
    std::size_t m_ordinary{0};
    std::string m_error;
};

template <class Visit>
void ChestData::for_each_near(const state::WorldPosition& center, float range, Visit&& visit, ChestLookupWork* work) const {
    if (m_cells.empty() || !(range >= 0.0F)) {
        return;
    }
    const auto low = [](float value) { return static_cast<std::int32_t>(std::floor(value / kCellSize)); };
    const float rangeSq = range * range;
    for (std::int32_t x = low(center.x - range); x <= low(center.x + range); ++x) {
        for (std::int32_t y = low(center.y - range); y <= low(center.y + range); ++y) {
            if (work != nullptr) {
                ++work->cells;
            }
            const auto found = m_cells.find(cell_key(x, y));
            if (found == m_cells.end()) {
                continue;
            }
            for (const std::uint32_t index : found->second) {
                if (work != nullptr) {
                    ++work->chests;
                }
                const state::WorldPosition& place = m_chests[index].position;
                const float dx = place.x - center.x;
                const float dy = place.y - center.y;
                if (dx * dx + dy * dy <= rangeSq) {
                    visit(index);
                }
            }
        }
    }
}

} // namespace soulwarden::backend::gamedata

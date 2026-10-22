#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "state/store.h"

// The destructible quest objects of the world (stage 1.8e, docs/systems/quests.md): the placements whose slice has the variation table
// Gatherable_Quest_Damageable (the corruption nodules of 06_survivors, the monolith of 06_demonstorm2), and their gatherable rows.
//
// The data is game data: tools/gamedata/quest-objects.sh makes the file from the game copy of the user, and the file stays outside the
// repository. Without the file no quest object can be destroyed, and the rest of the backend works.

namespace soulwarden::backend::gamedata {

/** File name of the destructible quest objects in the game data directory of the backend. */
inline constexpr const char* kQuestObjectDataFile = "quest-objects.json";

/** The gatherable row of a destructible quest object (GatherableData; the rules of gathering.py). */
struct QuestObjectRow {
    std::string id;
    std::string table;
    /** BaseGatherTime: the work of the destruction (the strikes that it needs, rounded up: a rule of this project). */
    float time{1.0F};
    /** MinRespawnRate, MaxRespawnRate (seconds): the object comes back for its character after a uniform draw of these bounds. */
    float respawnMin{0.0F};
    float respawnMax{0.0F};
    /** FinalLootTable (QuestInteractable, Empty: no roll) and GameEventID (the game event of the quests: MSQ_CorruptedNodule_1_interact). */
    std::string loot;
    std::string event;
};

/** One destructible quest object: a placement of a layer. The client makes it from its level data (a managed actor). */
struct QuestObjectPlacement {
    /** The placement UUID (lower case): the durable key. */
    std::string key;
    /** The managed actor id of the placement (the UUID rule): the key of its LootTracker entry. */
    std::array<std::uint8_t, 16> actor{};
    std::array<std::uint8_t, 16> asset{};
    std::uint32_t sub{2};
    /** The slot of the GatherableController (type 12; an instanced object takes no state there: protocol notes 4.41). */
    std::uint32_t slot{0};
    std::string variant;
    /** Index into QuestObjectData::rows. */
    std::uint32_t row{0};
    /** InteractTag of the variation row (lower case) and InteractOnlyOnQuest: the object is there only during a task with the tag. */
    std::string tag;
    bool onlyOnQuest{false};
    state::WorldPosition position;
};

class QuestObjectData {
public:
    /**
     * Reads the file of the tool (format 1). A file that is not complete is not used, as a whole: a member with a wrong form, a row index with
     * no row, an actor or an asset that is not 32 hexadecimal digits, a number that is not finite, a work that is not above 0, two objects
     * with one placement key or with one actor (stage 1.8f).
     * @return False when the file is not there or is not complete. `error()` then has the cause.
     */
    [[nodiscard]] bool load(const std::string& path);
    /** The same for a document in memory (tests). */
    [[nodiscard]] bool load_text(const std::string& text);

    [[nodiscard]] bool loaded() const {
        return m_loaded;
    }
    [[nodiscard]] const std::string& error() const {
        return m_error;
    }
    [[nodiscard]] const std::vector<QuestObjectRow>& rows() const {
        return m_rows;
    }
    [[nodiscard]] const std::vector<QuestObjectPlacement>& objects() const {
        return m_objects;
    }
    /**
     * @return The objects whose position is `radius` meters or less (in the plane) from a place: the cells of a grid of kCell meters near
     *         it (the cost depends on the objects near the place, not on the size of the world: rule MP-8). `visits` counts the objects
     *         that the lookup looked at.
     */
    [[nodiscard]] std::vector<std::uint32_t> nearby(const state::WorldPosition& place, float radius, std::uint64_t* visits = nullptr) const;

    /** The side of a cell of the grid (meters). */
    static constexpr float kCell = 64.0F;

private:
    bool fail(std::string cause);
    [[nodiscard]] static std::int64_t cell_of(float x, float y);

    bool m_loaded{false};
    std::string m_error;
    std::vector<QuestObjectRow> m_rows;
    std::vector<QuestObjectPlacement> m_objects;
    std::unordered_map<std::int64_t, std::vector<std::uint32_t>> m_cells;
};

} // namespace soulwarden::backend::gamedata

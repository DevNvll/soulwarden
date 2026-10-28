#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// The trigger volumes of the quests (stage 1.8b, docs/systems/quests.md): the volume of each tag that a TaskTriggerArea names (column
// TP_TargetTag). The tool tools/gamedata/triggers.py makes trigger-data.json from the chunk slices of the game copy: each entity with an
// ObjectiveDetectionVolumeComponent, its tag (m_targetTag of the server facet), and the shape of its detection volume in world coordinates.
//
// The client sends no message for these volumes (live check T1 of stage 1.8b: no message 560 at the entry of Aelstrom_IGC_Trigger): the
// server tests the accepted position of a character against the volumes of the tag of an active task. A lookup goes by the tag (a hash):
// no scan of all volumes (rule MP-8). Without the file the backend starts, and a task of a trigger area waits.
//
// Stage QG3: the tool also walks the capitals of the open world and the slices that they and the chunks spawn at their start; each volume
// has its world. A volume of another world (an instance: a dungeon, the introduction worlds) is in the member `elsewhere` of the file and
// is never tested in the open world: its tag and its worlds are kept only for the cause of a task that waits (the manifest).

namespace soulwarden::backend::gamedata {

/** File name of the trigger data in the game data directory of the backend. */
inline constexpr const char* kTriggerDataFile = "trigger-data.json";

enum class TriggerShape : std::uint8_t { aabb, box, sphere, cylinder, capsule };

struct TriggerVolume {
    std::string tag;
    TriggerShape shape{TriggerShape::aabb};
    std::array<float, 3> center{};
    /** The unit axes X, Y, Z of the volume in world coordinates (an aabb: the world axes). */
    std::array<float, 9> axes{1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F};
    /** The half size of a box on its axes. */
    std::array<float, 3> half{};
    /** The radius of a sphere, a cylinder, or a capsule, and the full height of a cylinder or a capsule (along the Z axis of the volume). */
    float radius{0.0F};
    float height{0.0F};
    /** m_ignoreHeight: the volume has no limit in Z. */
    bool ignoreHeight{false};
    /** Stage QG3: the slice of the entity and the placement that put it in the world (a chunk, or a capitals file and its row). */
    std::string source;
    std::string via;

    /** @return True when the point is in the volume (the border counts as inside). */
    [[nodiscard]] bool contains(float x, float y, float z) const;
};

class TriggerData {
public:
    /** Reads the file of the tool (format 1). A file that is not complete is not used, as a whole. @return False: `error()` has the cause. */
    [[nodiscard]] bool load(const std::string& path);
    /** The same for a document in memory (tests). */
    [[nodiscard]] bool load_text(const std::string& text);
    /** Sets the volumes with no file (tests). */
    void set(std::vector<TriggerVolume> volumes);

    [[nodiscard]] bool loaded() const {
        return !m_volumes.empty();
    }
    [[nodiscard]] const std::string& error() const {
        return m_error;
    }
    [[nodiscard]] std::size_t volume_count() const {
        return m_volumes.size();
    }
    [[nodiscard]] std::size_t tag_count() const {
        return m_byTag.size();
    }
    /** @return True when the data has a volume of the tag (lower case). */
    [[nodiscard]] bool knows(std::string_view tag) const;
    /**
     * @return True when the point is in a volume of the tag (lower case). Only the volumes of the tag are visited; `visits` (when not
     *         null) counts them.
     */
    [[nodiscard]] bool inside(std::string_view tag, float x, float y, float z, std::uint64_t* visits = nullptr) const;
    /**
     * Stage QG3: @return The worlds of the volumes of a tag (lower case) that are not in the open world (an instance), or null. Such a
     *         volume is never tested: only the cause of a task of the tag uses it.
     */
    [[nodiscard]] const std::vector<std::string>* elsewhere(std::string_view tag) const;
    /** Stage QG3: @return The volumes that hold the point (a grid of 256 m: only the volumes near the cell of the point; the place view). */
    [[nodiscard]] std::vector<const TriggerVolume*> at(float x, float y, float z) const;
    /** Stage QG3: the volumes of the member `volumes` of another world (not used), and the tags of the other worlds. */
    [[nodiscard]] std::size_t other_world() const {
        return m_otherWorld;
    }
    [[nodiscard]] std::size_t elsewhere_tags() const {
        return m_elsewhere.size();
    }

private:
    bool fail(std::string cause);
    void index();

    std::vector<TriggerVolume> m_volumes;
    std::unordered_map<std::string, std::vector<std::uint32_t>> m_byTag;
    std::string m_error;
    /** Stage QG3: the worlds of each tag of the other worlds, the cells of the grid, and the count of the volumes of another world. */
    std::unordered_map<std::string, std::vector<std::string>> m_elsewhere;
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> m_cells;
    std::size_t m_otherWorld{0};
    /** Code review of stage QG3, findings QG3R-R2 and QG3R-R3: the volumes that the grid does not hold; each lookup tests them. */
    std::vector<std::uint32_t> m_unbounded;
};

} // namespace soulwarden::backend::gamedata

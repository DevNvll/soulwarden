#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "gamedata/creature_data.h"
#include "gamedata/height_maps.h"
#include "gamedata/interaction_data.h"
#include "gamedata/navigation_data.h"

// The level data of an own level (stage DG2, docs/systems/instances.md, "The level data"): the data set of one level that is not the level
// of the main world (Amrine, nw_dungeon_windsward_00; a solo story level). The tool tools/gamedata/level.sh makes the set from the game copy
// of the user into the folder levels/LEVEL of the game data folder: the manifest level.json, the creature data of its terrain project
// (creatures.json, the format of the main world file with the spawn points of the level), the terrain heights (heights/), the navigation data
// (navigation/), and the interaction objects (interaction-data.json). The files stay outside the repository.
//
// The backend reads a set when the first space of its level starts (World, world/levels.h), not at its start. A level with no set, or a set
// that the reader refuses, is a level with no content (the instance of stage DG1). A part of a set that is not there, or that its reader
// refuses, is a part with no data; the other parts stay.

namespace soulwarden::backend::gamedata {

/** The folder of the level data sets in the game data directory, and the manifest of one set. */
inline constexpr const char* kLevelsFolder = "levels";
inline constexpr const char* kLevelManifestFile = "level.json";
/** The form of the manifest that this code reads (tools/gamedata/level_data.py). */
inline constexpr std::uint64_t kLevelFormat = 1;

/** @return True for a level text that can name a folder: 1 to 64 letters a to z, digits, and "_" (the levels of the map table). */
[[nodiscard]] bool level_name_valid(std::string_view level);

/** What the reader did with one part of a set. */
struct LevelPart {
    /** True: the part has data. */
    bool loaded{false};
    /** The bytes of its files. */
    std::uint64_t bytes{0};
    /** Why the part has no data: no file, a file of a different form (an old file), a file that is not correct. Empty with data. */
    std::string problem;
};

/** The data set of one own level. The world thread owns it after its load, and it stays until the stop of the backend. */
class LevelData {
public:
    explicit LevelData(std::string level) : m_level(std::move(level)) {}
    LevelData(const LevelData&) = delete;
    LevelData& operator=(const LevelData&) = delete;

    /**
     * Reads the set of the folder. The manifest must be there, of the form kLevelFormat, and it must name this level: else the whole set
     * is refused (`problem()`), and the level has no content. Then each part: a part that its reader refuses has no data.
     */
    void load(const std::string& directory);

    [[nodiscard]] const std::string& level() const {
        return m_level;
    }
    /** @return True when the manifest was accepted. */
    [[nodiscard]] bool accepted() const {
        return m_accepted;
    }
    /** @return Why the whole set was refused; empty for an accepted set. */
    [[nodiscard]] const std::string& problem() const {
        return m_problem;
    }
    /** @return The terrain folder of the manifest ("coatlicue/nw_dungeon_windsward_00"). */
    [[nodiscard]] const std::string& terrain() const {
        return m_terrain;
    }

    /** The creature data of the level: its spawn points with their kinds and slices (not loaded: no creature of a spawn point). */
    [[nodiscard]] const CreatureData& creatures() const {
        return m_creatures;
    }
    [[nodiscard]] CreatureData& creatures() {
        return m_creatures;
    }
    /** The navigation data and the terrain heights of the level (the per-level navigation of the world, world/navigation.h). */
    [[nodiscard]] const NavigationData& navigation() const {
        return m_navigation;
    }
    [[nodiscard]] NavigationData& navigation() {
        return m_navigation;
    }
    [[nodiscard]] HeightMaps& heights() {
        return m_heights;
    }
    [[nodiscard]] const HeightMaps& heights() const {
        return m_heights;
    }
    /** The interaction objects of the capitals of the level (the lore pages, the exit portal of the level). */
    [[nodiscard]] const InteractionData& interactions() const {
        return m_interactions;
    }
    [[nodiscard]] InteractionData& interactions() {
        return m_interactions;
    }

    [[nodiscard]] const LevelPart& creature_part() const {
        return m_creaturePart;
    }
    [[nodiscard]] const LevelPart& navigation_part() const {
        return m_navigationPart;
    }
    [[nodiscard]] const LevelPart& height_part() const {
        return m_heightPart;
    }
    [[nodiscard]] const LevelPart& interaction_part() const {
        return m_interactionPart;
    }
    /** @return True when one part has data. */
    [[nodiscard]] bool has_content() const {
        return m_creaturePart.loaded || m_navigationPart.loaded || m_heightPart.loaded || m_interactionPart.loaded;
    }

    /** Tests: the set with no file (the manifest is accepted, the parts come from the test). */
    void accept_for_test(std::string terrain) {
        m_accepted = true;
        m_terrain = std::move(terrain);
    }
    /** Tests: marks the parts that a test set has. */
    void mark_parts_for_test() {
        m_creaturePart.loaded = m_creatures.loaded();
        m_navigationPart.loaded = m_navigation.available();
        m_heightPart.loaded = m_heights.available();
        m_interactionPart.loaded = m_interactions.loaded();
    }

private:
    std::string m_level;
    std::string m_terrain;
    bool m_accepted{false};
    std::string m_problem;
    CreatureData m_creatures;
    NavigationData m_navigation;
    HeightMaps m_heights;
    InteractionData m_interactions;
    LevelPart m_creaturePart;
    LevelPart m_navigationPart;
    LevelPart m_heightPart;
    LevelPart m_interactionPart;
};

/** The source of the level data sets (the backend: a folder; a test: sets in the memory). */
class LevelSource {
public:
    virtual ~LevelSource() = default;
    /**
     * Reads the set of a level. Each call reads the files again: the world calls it one time for each level.
     * @return The set; null when the source has no set for the level (no folder).
     */
    [[nodiscard]] virtual std::unique_ptr<LevelData> open(const std::string& level) const = 0;
};

/** The folder levels/ of the game data folder: one folder for each level. */
class LevelFolder final : public LevelSource {
public:
    explicit LevelFolder(std::string directory) : m_directory(std::move(directory)) {}

    [[nodiscard]] std::unique_ptr<LevelData> open(const std::string& level) const override;
    [[nodiscard]] const std::string& directory() const {
        return m_directory;
    }
    /** @return The levels with a folder (the names that level_name_valid accepts), in the order of their names. For the log only. */
    [[nodiscard]] std::vector<std::string> levels() const;

private:
    std::string m_directory;
};

} // namespace soulwarden::backend::gamedata

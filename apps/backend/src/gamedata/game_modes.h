#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "state/store.h"

// The game modes (stage DG1, docs/systems/instances.md): the maps of the game modes of the client tables (javelindata_gamemodes and
// javelindata_gamemodemaps) that have a level and a start place. Each map has the level text and the terrain text of the level
// information (message 1635), the start place of each team, and the cells of its mode. The development command "instance enter" takes
// the start of a map.
//
// The data is game data: the tool tools/gamedata/game-modes.sh makes the file from the game copy of the user, and the file stays outside
// the repository. Without the file the backend starts and works: "instance enter" is then refused.

namespace soulwarden::backend::gamedata {

/** File name of the game modes in the game data directory of the backend. */
inline constexpr const char* kGameModesFile = "game-modes.json";

/** One start place of a team: a position on the ground (the client lets the character fall from the spawn lift) and a heading. */
struct ModeStart {
    state::WorldPosition position;
    /** Degrees, as the data has it (TeamTeleportData). */
    float heading{0.0F};
};

struct GameMap {
    /** GameModeMapId: the text of a command ("DungeonAmrine"). */
    std::string id;
    /** GameModeId of the mode of the map. */
    std::string mode;
    /** The row of the map table and of the mode table (0 is the first row). */
    std::uint32_t row{0};
    std::uint32_t modeRow{0};
    /** The level text and the terrain text of the level information ("nw_dungeon_windsward_00", "coatlicue/nw_dungeon_windsward_00"). */
    std::string level;
    std::string terrain;
    /** The start place of each team, at least one. */
    std::vector<ModeStart> starts;
    /** The display name key of the mode and its English text (empty when the game has none). */
    std::string nameKey;
    std::string name;
    std::uint32_t recommendedLevel{0};
    std::uint32_t requiredLevel{0};
    std::uint32_t teamCapacity{0};
    std::uint32_t minGroupSize{0};
    bool dungeon{false};
    bool soloTrial{false};
    bool raidTrial{false};
    /** TimerThresholdDefault of the mode (seconds). */
    std::uint32_t timeoutSeconds{0};
};

class GameModes {
public:
    /**
     * Reads the file of the tool. A file that is not complete is not used, as a whole: a map with no id, no level, no terrain, or no
     * start; a start that is not a place for a character; two maps with one id (case folded); a member "counts" whose number of maps is
     * not the size of the list. @return False when the file is not there or is not complete. `error()` then has the cause.
     */
    [[nodiscard]] bool load(const std::string& path);

    /** Sets the maps with no file (tests), with the same check. @return False when a map is not correct: the object then has no map. */
    [[nodiscard]] bool set(std::vector<GameMap> maps);

    [[nodiscard]] bool loaded() const {
        return !m_maps.empty();
    }
    [[nodiscard]] const std::string& error() const {
        return m_error;
    }
    [[nodiscard]] const std::vector<GameMap>& maps() const {
        return m_maps;
    }

    /**
     * @return The map with this map id, or the one map of the mode with this mode id (the compare ignores the case of the letters A to Z),
     *         or null: no such map, or a mode with more than one map (a command then names the map).
     */
    [[nodiscard]] const GameMap* find(std::string_view id) const;

private:
    std::vector<GameMap> m_maps;
    /** Lower-case map id to the position in the list. */
    std::unordered_map<std::string, std::size_t> m_byId;
    /** Lower-case mode id to the position of its one map (a mode with more than one map is not here). */
    std::unordered_map<std::string, std::size_t> m_byMode;
    std::string m_error;
};

} // namespace soulwarden::backend::gamedata

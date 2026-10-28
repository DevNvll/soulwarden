#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// The points of interest of the world (stage 1.8c, docs/systems/quests.md): the shape of each point of interest of the territory tables,
// for the quest tasks of a place (TaskGoToPOITag, TaskGoToPOITagOnTerritory, TaskExitPOITag) and the location rule of a task
// (EnforceLocationRequirement).
//
// Stage QG1 (format 2): the file also has the bounds of each territory, for the territory of a character (World::territory_of).
//
// The data is game data: the tool tools/gamedata/pois.sh makes poi-data.json from the game copy (the rows of TerritoryDefinition, the
// shape files objects/worldprisms/pois/REGION/poi_ID_*.vshapec, the POI placements of the layer "pois", and the entries of the region
// metadata as the check of each origin). Without the file the backend starts, and no task of a place progresses.
//
// The rule of a shape (live check POI1 of stage 1.8c, research map.md 2.9): the points of the file are relative to the POI placement and
// are not turned with it; the client sends 2297 with the "charted" achievement when its player goes into the shape, and with the
// "discovered" achievement when it goes into the sphere of DiscoveryRadius around the same placement. The membership of a quest task is
// the shape (the point of interest), not the discovery sphere.

namespace soulwarden::backend::gamedata {

/** File name of the point of interest data in the game data directory of the backend. */
inline constexpr const char* kPoiDataFile = "poi-data.json";

/** One point of interest with a shape. */
struct Poi {
    /** TerritoryID of the row (40142). */
    std::uint32_t id{0};
    /** POITag of the row, each tag in lower case. */
    std::vector<std::string> tags;
    /** The territory that holds the point of interest (the first id of the TerritoryIds of the region metadata: 6 for Windsward). */
    std::uint16_t territory{0};
    /** The origin of the shape (the POI placement) and DiscoveryRadius. */
    std::array<float, 3> center{};
    float radius{0.0F};
    /** The shape in world meters (x, y), not turned; and its bounds. */
    std::vector<std::array<float, 2>> shape;
    float minX{0.0F};
    float minY{0.0F};
    float maxX{0.0F};
    float maxY{0.0F};

    /** @return True when the point (x, y) is in the shape (the bounds first, then the even-odd rule). */
    [[nodiscard]] bool contains(float x, float y) const;
};

class PoiData {
public:
    /**
     * Reads the file of the tool (format 1). A file that is not complete is not used, as a whole: a member with a wrong form, a shape with
     * fewer than 3 points, a number that is not finite.
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
    [[nodiscard]] const std::vector<Poi>& pois() const {
        return m_pois;
    }
    /** @return The points of interest with a tag (lower case). */
    [[nodiscard]] const std::vector<std::uint32_t>* with_tag(std::string_view tag) const;
    /** @return The point of interest of a TerritoryID, or null. */
    [[nodiscard]] const Poi* poi(std::uint32_t id) const;
    /**
     * @return True when the place (x, y) is in a point of interest with one of the tags (lower case). `territory` other than 0: only the
     *         points of interest of that territory (TerritoryID of the task, 0x8262460). `visited` counts the shapes that the call tested.
     */
    [[nodiscard]] bool inside(const std::vector<std::string>& tags, std::uint16_t territory, float x, float y, std::uint64_t* visited = nullptr) const;
    /**
     * @return True when each tag has a shape of the data (code review of stage 1.8c, finding QR8C-R7: the membership of a tag with no shape is
     *         not known, and an exit task waits). No tag: false.
     */
    [[nodiscard]] bool knows(const std::vector<std::string>& tags) const;
    /**
     * Stage QG1: @return The territory at the place (x, y): the first bounds of a territory (objects/worldprisms/bounds/territory of the game data,
     *         the property TerritoryId) that hold it; 0 outside each bounds or with a file of the format 1. The client takes the claim
     *         territory of its player from the territory at its position (research map.md 2.6, ClaimKey 0x2A09BA0); the tool checks the
     *         bounds against the entries of the region metadata (266,696 entries, 1 outside the bounds of its territory).
     */
    [[nodiscard]] std::uint16_t territory_at(float x, float y) const;
    [[nodiscard]] const std::vector<Poi>& territories() const {
        return m_territories;
    }

private:
    bool fail(std::string cause);

    bool m_loaded{false};
    std::string m_error;
    std::vector<Poi> m_pois;
    /** Stage QG1: the bounds of the territories (Poi::territory is the territory id). */
    std::vector<Poi> m_territories;
    std::unordered_map<std::string, std::vector<std::uint32_t>> m_byTag;
    std::unordered_map<std::uint32_t, std::uint32_t> m_byId;
};

} // namespace soulwarden::backend::gamedata

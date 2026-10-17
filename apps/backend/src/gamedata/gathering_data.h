#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "state/store.h"

// The gathering data of the world (stage 3.6a, docs/systems/gathering.md): the gatherable rows, the gathering slices (their Interact bound
// and the slot of their node state), the natural nodes of each distribution file of the world, the capital nodes, the skinning of the
// creature slices, the tools, the trade skill ranks, and the events.
//
// The data is game data: the tool tools/gamedata/gathering.sh makes the files from the game copy of the user, and the files stay outside
// the repository. Without the files no node can be gathered (each request is refused), and the rest of the backend works.
//
// The natural nodes (2.45 million in the supported build) are in one file for each region (gathering/REGION.swgn). The backend reads a
// region file only when a request or a stored node names a node of that region, and keeps it. A request finds its node from its G in
// O(1): the client gives each record of a distribution file the offline id G = ((counter & 0x7FFFF) << 13) | ((regionY & 0x1F) << 8) |
// ((regionX & 0x1F) << 3) | 1, with the counter = the record number + 1 (client 0x69D5090; live check L2 of stage 3.6a). The cost does
// not depend on the size of the world (roadmap rule MP-8).
//
// World thread only (the region cache changes at a lookup).

namespace soulwarden::backend::gamedata {

/** File name of the gathering data in the game data directory of the backend. */
inline constexpr const char* kGatheringDataFile = "gathering-data.json";

/** The gathering type of a row (GatheringType of the data) and of a tool (GatheringTypes of its weapon row). */
enum class GatheringType : std::uint8_t {
    none,
    chopping,
    cutting,
    dressing,
    mining,
    /** A type that this stage does not have (AzothStaff, fishing): the row is refused. */
    other,
};

[[nodiscard]] const char* gathering_type_text(GatheringType type);
[[nodiscard]] GatheringType gathering_type_of(std::string_view text);
/** @return The paperdoll slot of the tool of a type (protocol notes 4.11: 33 logging axe, 34 sickle, 35 skinning knife, 36 pickaxe), or 0. */
[[nodiscard]] std::uint32_t tool_slot(GatheringType type);

/** A gatherable row (GatherableData of the game). */
struct GatherableRow {
    std::string id;
    std::string table;
    GatheringType type{GatheringType::none};
    /** Restriction: NoToolNecessary, ToolRequired, or ItemRequired with no item (an other value is in `unsupported`). */
    std::string restriction;
    /** The trade skill of the rank gate and of the speed and the luck (empty: none) and the rank that the node needs. */
    std::string tradeskill;
    std::uint32_t level{0};
    /** BaseGatherTime: the work amount (seconds at the speed 1). */
    float time{0.0F};
    /** MinRespawnRate, MaxRespawnRate (seconds). */
    float respawnMin{0.0F};
    float respawnMax{0.0F};
    /** FinalLootTable (the loot root), LootTableDefaultMaterial, GameEventID (the trade skill experience). */
    std::string loot;
    std::string material;
    std::string event;
    /** ResetGatherTimeOnEndInteraction: an end of the use resets the work. */
    bool resetOnEnd{false};
    bool destroy{false};
    std::string depleted;
    std::uint32_t required{1};
    bool waitForImpact{false};
    /** The rules of the data that the backend has no predicate for: a use is refused with the first one. Empty: supported. */
    std::vector<std::string> unsupported;
};

/** A game event of a gatherable row: the trade skill (CategoricalProgressionId) and its experience (CategoricalProgressionReward). */
struct GatheringEvent {
    std::string id;
    std::string progression;
    std::uint32_t amount{0};
};

/** One rank of a trade skill (TradeskillRankData): RollBonus, GatheringEfficiency, GatheringMultiplier. */
struct GatheringRank {
    float rollBonus{0.0F};
    float efficiency{0.0F};
    float multiplier{0.0F};
};

/** A tool (the weapon row of an item): its gathering types and its speed range over the gear score range of the item. */
struct GatheringTool {
    std::vector<GatheringType> types;
    float minEfficiency{0.0F};
    float maxEfficiency{0.0F};
    /** GatheringEfficiency and GatheringMultiplier of the weapon row (kept; the speed uses the range: live check L7). */
    float efficiency{0.0F};
    float multiplier{0.0F};
};

/** The Interact bound of a gathering slice (in the frame of the slice: the ids of the slice). */
struct GatheringBound {
    std::uint64_t entity{0};
    std::uint64_t component{0};
    std::uint64_t option{0};
    std::uint32_t slot{0};
    float rangeSq{0.0F};
    std::int32_t capacity{-1};
    std::array<float, 3> translation{};
    bool lineOfSight{false};
    bool arcRequired{false};
    /** The bound has a requirement of the data (an achievement, a progression, a quest, a condition): it is refused. */
    bool requirement{false};
};

/** A gathering slice. */
struct GatheringTemplate {
    std::string slice;
    std::array<std::uint8_t, 16> asset{};
    std::uint32_t sub{2};
    std::uint64_t root{0};
    /** The slot of the GatherableController component (the node state, type 12). */
    std::uint32_t controllerSlot{0};
    std::string table;
    GatheringBound bound;
};

/** One record of a region file. */
struct NaturalRecord {
    std::uint16_t type{0};
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
};

/** A region: its distribution file and the gathering type of each slice index of it. */
struct GatheringRegion {
    std::string name;
    std::int32_t x{0};
    std::int32_t y{0};
    std::string file;
    std::uint32_t records{0};
    std::uint32_t tails{0};
    /** The version of the distribution file (a stored node of a different version is not the same node). */
    std::string version;
    /** For each slice index of the file: the template and the row (-1: not a gathering type), and the variant name. */
    std::vector<std::int32_t> templates;
    std::vector<std::int32_t> rows;
    std::vector<std::string> variants;
};

/** A natural node (a record of a distribution file) that a lookup found. */
struct NaturalNode {
    std::uint32_t region{0};
    std::uint32_t record{0};
    std::uint32_t templateIndex{0};
    std::uint32_t row{0};
    state::WorldPosition position;
    std::uint64_t group{0};
};

/** A capital node or the skinning of a creature slice: the template and the row. */
struct GatheringLink {
    std::uint32_t templateIndex{0};
    std::uint32_t row{0};
};

/** The work of the lookups (the load case). */
struct GatheringDataWork {
    std::uint64_t lookups{0};
    std::uint64_t regionLoads{0};
    std::uint64_t recordsRead{0};
};

class GatheringData {
public:
    /** The offline id of a record of a distribution file (client 0x69D5090). */
    [[nodiscard]] static std::uint64_t natural_group(std::int32_t regionX, std::int32_t regionY, std::uint32_t counter);

    /**
     * Reads gathering-data.json of a game data directory (the region files later, at their first use). A file that is not complete is
     * not used, as a whole. @return False when the file is not there or is not complete; `error()` then has the cause.
     */
    [[nodiscard]] bool load(const std::string& directory);
    /**
     * Sets the data from a document (the form of the file) and the records of some regions (tests: no region file; a region with no
     * records here reads its file from `directory`). @return False when a part is not correct.
     */
    [[nodiscard]] bool set(const std::string& document, std::unordered_map<std::string, std::vector<NaturalRecord>> records = {},
                           const std::string& directory = "");

    [[nodiscard]] bool loaded() const {
        return m_loaded;
    }
    [[nodiscard]] const std::string& error() const {
        return m_error;
    }

    [[nodiscard]] const std::vector<GatherableRow>& rows() const {
        return m_rows;
    }
    [[nodiscard]] std::optional<std::uint32_t> find_row(std::string_view id) const;
    [[nodiscard]] const std::vector<GatheringTemplate>& templates() const {
        return m_templates;
    }
    [[nodiscard]] const std::vector<GatheringRegion>& regions() const {
        return m_regions;
    }
    [[nodiscard]] const GatheringEvent* event(std::string_view id) const;
    /** @return The values of a rank of a trade skill. No table or a rank above it: the last rank; no table: zeros. */
    [[nodiscard]] GatheringRank rank(std::string_view tradeskill, std::uint32_t rank) const;
    [[nodiscard]] const GatheringTool* tool(std::string_view itemId) const;
    /** The option of a gather (GatherOption of the user interface database: its id and its action, 14). */
    [[nodiscard]] std::uint32_t option_id() const {
        return m_optionId;
    }

    /** @return The node of a placement UUID (any case), or no value. */
    [[nodiscard]] std::optional<GatheringLink> capital(std::string_view key) const;
    /** @return The skinning of a creature slice (its asset id and sub id), or no value. */
    [[nodiscard]] std::optional<GatheringLink> creature(const std::array<std::uint8_t, 16>& asset, std::uint32_t sub) const;
    /** @return The natural node of a G (reads the file of its region at its first use), or no value (not a record of a gathering type). */
    [[nodiscard]] std::optional<NaturalNode> natural(std::uint64_t group) const;
    /** @return The natural node of a region and a record number (a stored node). */
    [[nodiscard]] std::optional<NaturalNode> natural_at(std::uint32_t region, std::uint32_t record) const;
    /** @return The index of a region by its name, or no value. */
    [[nodiscard]] std::optional<std::uint32_t> region_index(std::string_view name) const;

    [[nodiscard]] const GatheringDataWork& work() const {
        return m_work;
    }
    /** @return The number of region files in the memory. */
    [[nodiscard]] std::size_t regions_loaded() const {
        return m_loadedRecords.size();
    }
    /** @return The number of gathering records of the regions (the census of the file), the capital nodes, the creature slices. */
    [[nodiscard]] std::uint64_t natural_count() const {
        return m_naturalCount;
    }
    [[nodiscard]] std::size_t capital_count() const {
        return m_capitals.size();
    }
    [[nodiscard]] std::size_t creature_count() const {
        return m_creatures.size();
    }

private:
    bool parse(const std::string& text);
    [[nodiscard]] const std::vector<NaturalRecord>* records_of(std::uint32_t region) const;

    bool m_loaded{false};
    std::string m_error;
    std::string m_directory;
    std::uint32_t m_optionId{0};
    std::vector<GatherableRow> m_rows;
    std::unordered_map<std::string, std::uint32_t> m_rowIndex;
    std::unordered_map<std::string, GatheringEvent> m_events;
    std::unordered_map<std::string, std::vector<GatheringRank>> m_ranks;
    std::unordered_map<std::string, GatheringTool> m_tools;
    std::vector<GatheringTemplate> m_templates;
    std::unordered_map<std::string, GatheringLink> m_capitals;
    std::map<std::pair<std::array<std::uint8_t, 16>, std::uint32_t>, GatheringLink> m_creatures;
    std::vector<GatheringRegion> m_regions;
    std::map<std::pair<std::int32_t, std::int32_t>, std::uint32_t> m_regionByPlace;
    std::uint64_t m_naturalCount{0};
    /** The records of the regions that a lookup needed (or that `set` gave). */
    mutable std::unordered_map<std::uint32_t, std::vector<NaturalRecord>> m_loadedRecords;
    mutable GatheringDataWork m_work;
};

} // namespace soulwarden::backend::gamedata

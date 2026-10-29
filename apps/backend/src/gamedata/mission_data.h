#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "gamedata/game_event.h"

// The mission data of the boards (stage QG6a, docs/systems/mission-boards.md; tools/gamedata/missions.py, mission-data.json): the mission
// rows of the town project boards and of the open world boards (MissionData), the projects of the towns (TerritoryProgressionData), and the
// game events of the missions. The client joins a mission by its missionId (the CRC-32 of the lower-case MissionID; loader 0x39C4870, lookup
// 0x3A342E0), takes ObjectiveID of the row, and builds the task tree of that template with the values of the row (factory 0x4461150). The
// server keeps the same join: an accepted mission is an instance of its template with the effective values of its row
// (QuestData::bind_missions).
//
// With no file the backend starts with no mission: each board offers nothing, and each accept is refused (rule A5a).

namespace soulwarden::backend::gamedata {

/** File name of the mission data in the game data directory of the backend. */
inline constexpr const char* kMissionDataFile = "mission-data.json";

/** The table of a mission row (the source of the tool). */
enum class MissionTable : std::uint8_t {
    /** javelindata_territoryprogression_missions: the town project missions of the town boards. */
    town,
    /** javelindata_owg_mission: the open world missions of the faction boards (stage QG6b). */
    world,
    /** javelindata_affinitymissions (no row in the supported build). */
    affinity,
    other,
};

[[nodiscard]] const char* mission_table_text(MissionTable table);

/** One mission row. The cells keep the rule of the source: a blank cell has no value, an explicit 0 is 0. */
struct MissionRow {
    std::string id;
    /** The missionId of the wire: the CRC-32 of the lower-case MissionID. */
    std::uint32_t crc{0};
    MissionTable table{MissionTable::other};
    /** The row number in its table (the order of the candidates of the client). */
    std::uint32_t row{0};
    /** ObjectiveID (the template) and its CRC-32 (the objectiveId of an active element). */
    std::string objective;
    std::uint32_t objectiveCrc{0};
    /** AvailableTerritoryID: the territories (client test 0x39D3EC0: an empty list passes; a list needs the territory; with `exclude` the
     *  territory must not be in it). */
    std::vector<std::uint16_t> territories;
    bool excludeTerritories{false};
    /** TerritoryProjectId (a town row) and its CRC-32. Empty: none. */
    std::string project;
    std::uint32_t projectCrc{0};
    std::uint32_t tier{0};
    /** MissionGoalType (Kill, Gather, Explore; Raid, Loot, Hunt, ... of the world table). */
    std::string goalType;
    /** Enabled (a blank cell: true), IsCustomObjective (row +0x144: the template keeps its own tasks), IsPvpMission. */
    bool enabled{true};
    bool custom{false};
    bool pvp{false};
    /** MinLevel and MaxLevel (0: no bound; client filter 0x3A09900). */
    std::uint32_t minLevel{0};
    std::uint32_t maxLevel{0};
    /** RequiredTradeskill and TradeskillLevel (an empty skill: none). */
    std::string tradeskill;
    std::uint32_t tradeskillLevel{0};
    /** RequiredFaction (the text of the cell; empty: none) and RequiredAchievementConditional. */
    std::string requiredFaction;
    std::string achievementConditional;
    /** ReturnToGiverOverride (row +0x14B): the return task of the template goes to the provider. */
    bool returnToGiver{false};
    /** TaskKillContributionOverride (row +0x2C) and TaskKillContributionQtyOverride (row +0x30): case 13 of the factory 0x4461150. */
    std::string killCategory;
    std::optional<std::uint32_t> killQuantity;
    /** SuccessGameEventIdOverride (row +0x50) and FailureGameEventIdOverride (row +0x54). Empty: the event of the template. */
    std::string successEvent;
    std::string failureEvent;
    /** CommunityGoalAmount: the project contribution of a town mission (not a reward of the character). */
    std::uint32_t communityGoalAmount{0};
    /** GrantRewardsToOriginTerritory: the standing goes to the origin territory of the instance. */
    bool rewardsToOrigin{false};
    /** ProviderDistanceLimit and TurninDistance (meters; 0: a blank cell). */
    float providerDistance{0.0F};
    float turnInDistance{0.0F};
    /** IsSettlementValidDestination and IsOutpostValidDestination. */
    bool settlementDestination{false};
    bool outpostDestination{false};
    /** TaskTimerOverride (seconds) and UseGlobalStorageOverride: they change only a TaskTimer leaf and an item task. No value: blank. */
    std::optional<double> timerSeconds;
    std::optional<bool> useGlobalStorage;
    // Stage QG6b (docs/systems/mission-boards.md, the overrides): the cells that the task factory 0x4461150 gives the tasks of the template
    // (the loader 0x39C4870; the static read evidence/quests/qg6b/implement/static/factory/FACTORY.md). A blank cell has no value.
    /** POITagOverride (row +0x98, a list of tags in lower case) and TerritoryIDOverride (row +0x28). */
    std::vector<std::string> poiTags;
    std::uint16_t territoryOverride{0};
    /** OverrideLocation (three numbers: the place of the pin). */
    std::string location;
    /** LootTagOverride (no row of the supported build has it). */
    std::string lootTag;
    /** TaskHaveAndReturnItemsOverride (row +0x38) and its quantity (row +0x3C). */
    std::string item;
    std::optional<std::uint32_t> itemQuantity;
    /** The drop chances of the item (row +0x40: a creature or a gatherable; row +0x44: a chest), percent, and the drop sources (row +0x118). */
    std::optional<double> itemDropChance;
    std::optional<double> chestDropChance;
    std::vector<std::string> dropSources;
    /** TaskGiveAndTakeItemOverride (row +0x34): the item of a give, a give-and-take, and a remove. */
    std::string giveItem;
    /**
     * PauseAndClearLootOnTaskCompleteOverride (row +0x141, +0x142; the loader 0x39C5933 reads only this spelling). The town table has the
     * column PauseAndClearLootTagOnTaskCompleteOverride: the client loader does not read it (`pauseLootAlias`: the cell is there, no effect).
     */
    std::optional<bool> pauseLoot;
    bool pauseLootAlias{false};
    /** PatrolDuration (row +0x80, seconds) and PatrolPoi1 to PatrolPoi4 (row +0xF8, the tags in lower case, the blank cells skipped). */
    std::optional<double> patrolSeconds;
    std::vector<std::string> patrolPois;
    /** RequiredFortControlStatus (blank in each row of the supported build). */
    std::string fortStatus;
    /**
     * The other override cells (a column that the backend does not know): "COLUMN VALUE". QuestData::bind_missions refuses a row with one
     * (a cell with no rule).
     */
    std::vector<std::pair<std::string, std::string>> overrides;
};

/** One project of a town (TerritoryProgressionData). */
struct MissionProject {
    std::string id;
    std::uint32_t crc{0};
    /** ProjectType (Settlement, Fortress, Lifestyle, AlwaysAvailable) and ProgressionLevel, CurrentTier. */
    std::string type;
    std::uint32_t level{0};
    std::uint32_t tier{0};
};

/**
 * One goal bucket of the town missions in the order of the client (the provider map at component +0x110 that 0x3A5DB80 copies from the
 * global map of 0x4468AA0): the key (the MissionGoalType value: Gather 1, Kill 4, Explore 5) and its missionIds in the order of the
 * candidates (tools/gamedata/missions.py, member townBuckets). The selection of 0x3A362A0 takes one candidate of each bucket that its filter
 * keeps.
 */
struct TownBucket {
    std::uint8_t key{0};
    std::vector<std::uint32_t> missions;
};

/** Stage QG6b: one row of MissionWeightsData (the member weights): the bucket (FactionPvE, FactionPvP, Affinity), the slot, the goal, the weight. */
struct MissionWeight {
    std::string bucket;
    std::uint32_t slot{0};
    std::string goal;
    std::uint32_t weight{0};
};

/**
 * Stage QG6b: one row of RewardModifierData of the missions (the member rewardModifiers): the additions of a bonus to the multipliers of the
 * reward preview (0x31F4630 with the helper 0x5BF6120: the row adds its values; static read FACTION.md section 5). A blank cell is 0.
 */
struct MissionModifier {
    std::string id;
    double experience{0.0};
    double currency{0.0};
    double standing{0.0};
    double reputation{0.0};
    double tokens{0.0};
};

class MissionData {
public:
    /**
     * Reads the file of the tool (format 1). A file that is not complete is not used, as a whole: a member with a wrong form, two missions
     * or two projects with one CRC-32, a territory id outside 1 to 65535.
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
    [[nodiscard]] const std::vector<MissionRow>& missions() const {
        return m_missions;
    }
    [[nodiscard]] const std::vector<MissionProject>& projects() const {
        return m_projects;
    }
    /** @return The mission of a missionId, or null. */
    [[nodiscard]] const MissionRow* mission(std::uint32_t crc) const;
    /** @return The mission of an id (any case), or null. */
    [[nodiscard]] const MissionRow* mission(std::string_view id) const;
    /** @return The project of a CRC-32 of its lower-case id, or null. */
    [[nodiscard]] const MissionProject* project(std::uint32_t crc) const;
    /** @return The game event of an id (any case) of the file, or null. */
    [[nodiscard]] const QuestGameEvent* game_event(std::string_view id) const;
    [[nodiscard]] const std::unordered_map<std::string, QuestGameEvent>& events() const {
        return m_events;
    }
    /** @return The goal buckets of the town missions in the order of the client (empty: a file with no townBuckets: no town offer). */
    [[nodiscard]] const std::vector<TownBucket>& town_buckets() const {
        return m_townBuckets;
    }
    /** Stage QG6b: @return The weight rows (empty: a file with no member weights: a faction board has no offer). */
    [[nodiscard]] const std::vector<MissionWeight>& weights() const {
        return m_weights;
    }
    /** Stage QG6b: @return The reward modifier of an id (any case), or null (a file with no member rewardModifiers: no daily bonus). */
    [[nodiscard]] const MissionModifier* modifier(std::string_view id) const;

private:
    bool fail(std::string cause);

    bool m_loaded{false};
    std::string m_error;
    std::vector<MissionRow> m_missions;
    std::vector<MissionProject> m_projects;
    std::unordered_map<std::uint32_t, std::size_t> m_missionByCrc;
    std::unordered_map<std::uint32_t, std::size_t> m_projectByCrc;
    std::unordered_map<std::string, QuestGameEvent> m_events;
    std::vector<TownBucket> m_townBuckets;
    std::vector<MissionWeight> m_weights;
    std::vector<MissionModifier> m_modifiers;
};

} // namespace soulwarden::backend::gamedata

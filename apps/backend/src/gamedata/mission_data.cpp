#include "gamedata/mission_data.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "gamedata/loot_tables.h"
#include "gamedata/quest_data.h"

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

constexpr std::uint64_t kFormat = 1;

/**
 * The columns of a mission row that the backend reads or that change nothing on the server (the texts of the screen, the estimates). Each
 * other column with a text is an override of `MissionRow::overrides`.
 */
const std::set<std::string>& known_columns() {
    static const std::set<std::string> kColumns = {
        "MissionTier", "ObjectiveID", "TerritoryProjectId", "ReturnToGiverOverride", "IsSettlementValidDestination", "IsOutpostValidDestination",
        "TaskKillContributionOverride", "TaskKillContributionQtyOverride", "CommunityGoalAmount", "UseGlobalStorageOverride", "TaskTimerOverride",
        "SuccessGameEventIdOverride", "FailureGameEventIdOverride", "TitleOverride", "DescriptionOverride", "ImagePath", "DetailImagePath",
        "MissionGoalType", "EstimatedCompletionTimeMinutes", "RecommendedGroupSize", "DifficultyHintString", "AvailableTerritoryID",
        "AvailableTerritoryId", "RequiredAchievementConditional", "ProviderDistanceLimit", "MinLevel", "MaxLevel", "RequiredTradeskill",
        "TradeskillLevel", "Enabled", "IsCustomObjective", "IsPvpMission", "RequiredFaction", "GrantRewardsToOriginTerritory", "TurninDistance",
        "DevName", "VCLevel",
        // Stage QG6b: the override cells with a rule (QuestData::bind_missions).
        "POITagOverride", "TerritoryIDOverride", "OverrideLocation", "LootTagOverride", "TaskHaveAndReturnItemsOverride",
        "TaskHaveAndReturnItemsQtyOverride", "TaskHaveAndReturnItemsDropProbabilityOverride", "TaskHaveAndReturnChestDropProbabilityOverride",
        "TaskHaveAndReturnItemsItemsDropVCOverride", "TaskGiveAndTakeItemOverride", "PauseAndClearLootOnTaskCompleteOverride",
        "PauseAndClearLootTagOnTaskCompleteOverride", "PatrolDuration", "PatrolPoi1", "PatrolPoi2", "PatrolPoi3", "PatrolPoi4",
        "RequiredFortControlStatus"};
    return kColumns;
}

/** @return The parts of a list cell ("a,b"): trimmed, in lower case, the empty parts skipped. */
std::vector<std::string> list_of(const std::string& text) {
    std::vector<std::string> parts;
    std::string part;
    const auto flush = [&]() {
        const std::size_t first = part.find_first_not_of(" \t");
        const std::size_t last = part.find_last_not_of(" \t");
        if (first != std::string::npos) {
            parts.push_back(lower_text(part.substr(first, last - first + 1)));
        }
        part.clear();
    };
    for (const char letter : text) {
        if (letter == ',') {
            flush();
        } else {
            part += letter;
        }
    }
    flush();
    return parts;
}

/** @return The number of a cell (no value: a blank cell or a text that is not a finite number of 0 or more). */
std::optional<double> real(const std::string& text) {
    if (text.empty()) {
        return std::nullopt;
    }
    try {
        std::size_t used = 0;
        const double value = std::stod(text, &used);
        return used == text.size() && std::isfinite(value) && value >= 0.0 ? std::optional<double>(value) : std::nullopt;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string cell(const json& cells, const char* column) {
    const auto found = cells.find(column);
    return found != cells.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

/** @return The whole number of a cell (a blank cell: `fallback`), or no value for a text that is not a number of 0 to 2^32 - 1. */
std::optional<std::uint32_t> whole(const std::string& text, std::uint32_t fallback = 0) {
    if (text.empty()) {
        return fallback;
    }
    try {
        std::size_t used = 0;
        const double value = std::stod(text, &used);
        if (used != text.size() || !std::isfinite(value) || value < 0.0 || value > 4294967295.0) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(value);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool truth(const std::string& text) {
    return text == "1" || text == "true" || text == "TRUE" || text == "True";
}

} // namespace

const char* mission_table_text(MissionTable table) {
    switch (table) {
    case MissionTable::town:
        return "town";
    case MissionTable::world:
        return "world";
    case MissionTable::affinity:
        return "affinity";
    case MissionTable::other:
        break;
    }
    return "other";
}

bool MissionData::fail(std::string cause) {
    *this = MissionData{};
    m_error = std::move(cause);
    return false;
}

bool MissionData::load(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        *this = MissionData{};
        m_error = "no file";
        return false;
    }
    std::stringstream text;
    text << input.rdbuf();
    return load_text(text.str());
}

bool MissionData::load_text(const std::string& text) {
    *this = MissionData{};
    json document;
    try {
        document = json::parse(text);
    } catch (const json::exception& error) {
        return fail(std::string("the file is not JSON: ") + error.what());
    }
    if (!document.is_object() || !document.contains("format") || !document["format"].is_number_unsigned()
        || document["format"].get<std::uint64_t>() != kFormat) {
        return fail("the file has no format 1");
    }
    if (!document.contains("missions") || !document["missions"].is_array() || !document.contains("projects") || !document["projects"].is_array()) {
        return fail("the file has no list missions or projects");
    }
    for (const json& entry : document["missions"]) {
        if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_string() || !entry.contains("crc") || !entry["crc"].is_number_unsigned()
            || !entry.contains("table") || !entry["table"].is_string() || !entry.contains("row") || !entry["row"].is_number_unsigned()
            || !entry.contains("cells") || !entry["cells"].is_object() || !entry.contains("territories") || !entry["territories"].is_object()) {
            return fail("a mission is not {id, crc, table, row, territories, cells}");
        }
        MissionRow row;
        row.id = entry["id"].get<std::string>();
        row.crc = static_cast<std::uint32_t>(entry["crc"].get<std::uint64_t>());
        if (row.id.empty() || row.crc != quest_crc(row.id)) {
            return fail("the mission " + row.id + " has a CRC-32 that is not the CRC-32 of its lower-case id");
        }
        const std::string table = entry["table"].get<std::string>();
        row.table = table == "town" ? MissionTable::town : table == "world" ? MissionTable::world : table == "affinity" ? MissionTable::affinity
                                                                                                                         : MissionTable::other;
        row.row = static_cast<std::uint32_t>(std::min<std::uint64_t>(entry["row"].get<std::uint64_t>(), 0xFFFFFFFFULL));
        const json& cells = entry["cells"];
        row.objective = cell(cells, "ObjectiveID");
        row.objectiveCrc = row.objective.empty() ? 0 : quest_crc(row.objective);
        const json& rule = entry["territories"];
        if (!rule.contains("ids") || !rule["ids"].is_array() || !rule.contains("exclude") || !rule["exclude"].is_boolean()) {
            return fail("the territory rule of the mission " + row.id + " is not {ids, exclude}");
        }
        for (const json& id : rule["ids"]) {
            if (!id.is_number_unsigned() || id.get<std::uint64_t>() == 0 || id.get<std::uint64_t>() > 0xFFFF) {
                return fail("the mission " + row.id + " has a territory id outside 1 to 65535");
            }
            row.territories.push_back(static_cast<std::uint16_t>(id.get<std::uint64_t>()));
        }
        row.excludeTerritories = rule["exclude"].get<bool>();
        row.project = cell(cells, "TerritoryProjectId");
        row.projectCrc = row.project.empty() ? 0 : quest_crc(row.project);
        const std::optional<std::uint32_t> tier = whole(cell(cells, "MissionTier"));
        const std::optional<std::uint32_t> minLevel = whole(cell(cells, "MinLevel"));
        const std::optional<std::uint32_t> maxLevel = whole(cell(cells, "MaxLevel"));
        const std::optional<std::uint32_t> skillLevel = whole(cell(cells, "TradeskillLevel"));
        const std::optional<std::uint32_t> amount = whole(cell(cells, "CommunityGoalAmount"));
        if (!tier || !minLevel || !maxLevel || !skillLevel || !amount) {
            return fail("the mission " + row.id + " has a tier, a level, a trade skill level, or a goal amount that is not a whole number");
        }
        row.tier = *tier;
        row.minLevel = *minLevel;
        row.maxLevel = *maxLevel;
        row.tradeskillLevel = *skillLevel;
        row.communityGoalAmount = *amount;
        row.goalType = cell(cells, "MissionGoalType");
        row.enabled = !cells.contains("Enabled") || truth(cell(cells, "Enabled"));
        row.custom = truth(cell(cells, "IsCustomObjective"));
        row.pvp = truth(cell(cells, "IsPvpMission"));
        row.tradeskill = cell(cells, "RequiredTradeskill");
        row.requiredFaction = cell(cells, "RequiredFaction");
        row.achievementConditional = cell(cells, "RequiredAchievementConditional");
        row.returnToGiver = truth(cell(cells, "ReturnToGiverOverride"));
        row.killCategory = cell(cells, "TaskKillContributionOverride");
        if (cells.contains("TaskKillContributionQtyOverride")) {
            const std::optional<std::uint32_t> quantity = whole(cell(cells, "TaskKillContributionQtyOverride"));
            if (!quantity) {
                return fail("the mission " + row.id + " has a kill quantity that is not a whole number");
            }
            row.killQuantity = *quantity;
        }
        row.successEvent = cell(cells, "SuccessGameEventIdOverride");
        row.failureEvent = cell(cells, "FailureGameEventIdOverride");
        row.rewardsToOrigin = truth(cell(cells, "GrantRewardsToOriginTerritory"));
        row.providerDistance = static_cast<float>(whole(cell(cells, "ProviderDistanceLimit")).value_or(0));
        row.turnInDistance = static_cast<float>(whole(cell(cells, "TurninDistance")).value_or(0));
        row.settlementDestination = truth(cell(cells, "IsSettlementValidDestination"));
        row.outpostDestination = truth(cell(cells, "IsOutpostValidDestination"));
        if (cells.contains("TaskTimerOverride")) {
            row.timerSeconds = static_cast<double>(whole(cell(cells, "TaskTimerOverride")).value_or(0));
        }
        if (cells.contains("UseGlobalStorageOverride")) {
            row.useGlobalStorage = truth(cell(cells, "UseGlobalStorageOverride"));
        }
        // Stage QG6b: the override cells (docs/systems/mission-boards.md, the overrides).
        row.poiTags = list_of(cell(cells, "POITagOverride"));
        if (cells.contains("TerritoryIDOverride")) {
            const std::optional<std::uint32_t> territory = whole(cell(cells, "TerritoryIDOverride"));
            if (!territory || *territory > 0xFFFF) {
                return fail("the mission " + row.id + " has a TerritoryIDOverride that is not a territory id");
            }
            row.territoryOverride = static_cast<std::uint16_t>(*territory);
        }
        row.location = cell(cells, "OverrideLocation");
        row.lootTag = cell(cells, "LootTagOverride");
        row.item = cell(cells, "TaskHaveAndReturnItemsOverride");
        if (cells.contains("TaskHaveAndReturnItemsQtyOverride")) {
            const std::optional<std::uint32_t> quantity = whole(cell(cells, "TaskHaveAndReturnItemsQtyOverride"));
            if (!quantity) {
                return fail("the mission " + row.id + " has an item quantity that is not a whole number");
            }
            row.itemQuantity = *quantity;
        }
        for (const auto& [column, target] : {std::pair<const char*, std::optional<double>*>{"TaskHaveAndReturnItemsDropProbabilityOverride", &row.itemDropChance},
                                             std::pair<const char*, std::optional<double>*>{"TaskHaveAndReturnChestDropProbabilityOverride", &row.chestDropChance},
                                             std::pair<const char*, std::optional<double>*>{"PatrolDuration", &row.patrolSeconds}}) {
            if (cells.contains(column)) {
                *target = real(cell(cells, column));
                if (!*target) {
                    return fail("the mission " + row.id + " has a " + column + " that is not a number");
                }
            }
        }
        row.dropSources = list_of(cell(cells, "TaskHaveAndReturnItemsItemsDropVCOverride"));
        row.giveItem = cell(cells, "TaskGiveAndTakeItemOverride");
        if (cells.contains("PauseAndClearLootOnTaskCompleteOverride")) {
            row.pauseLoot = truth(cell(cells, "PauseAndClearLootOnTaskCompleteOverride"));
        }
        row.pauseLootAlias = cells.contains("PauseAndClearLootTagOnTaskCompleteOverride");
        for (const char* column : {"PatrolPoi1", "PatrolPoi2", "PatrolPoi3", "PatrolPoi4"}) {
            for (std::string& tag : list_of(cell(cells, column))) {
                row.patrolPois.push_back(std::move(tag));
            }
        }
        row.fortStatus = cell(cells, "RequiredFortControlStatus");
        for (const auto& [column, value] : cells.items()) {
            if (value.is_string() && !value.get<std::string>().empty() && known_columns().count(column) == 0) {
                row.overrides.emplace_back(column, value.get<std::string>());
            }
        }
        if (!m_missionByCrc.emplace(row.crc, m_missions.size()).second) {
            return fail("two missions have the CRC-32 of " + row.id);
        }
        m_missions.push_back(std::move(row));
    }
    for (const json& entry : document["projects"]) {
        if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_string() || !entry.contains("crc") || !entry["crc"].is_number_unsigned()
            || !entry.contains("cells") || !entry["cells"].is_object()) {
            return fail("a project is not {id, crc, cells}");
        }
        MissionProject project;
        project.id = entry["id"].get<std::string>();
        project.crc = static_cast<std::uint32_t>(entry["crc"].get<std::uint64_t>());
        if (project.id.empty() || project.crc != quest_crc(project.id)) {
            return fail("the project " + project.id + " has a CRC-32 that is not the CRC-32 of its lower-case id");
        }
        project.type = cell(entry["cells"], "ProjectType");
        project.level = whole(cell(entry["cells"], "ProgressionLevel")).value_or(0);
        project.tier = whole(cell(entry["cells"], "CurrentTier")).value_or(0);
        if (!m_projectByCrc.emplace(project.crc, m_projects.size()).second) {
            return fail("two projects have the CRC-32 of " + project.id);
        }
        m_projects.push_back(std::move(project));
    }
    if (document.contains("events") && document["events"].is_object()) {
        for (const auto& [key, cells] : document["events"].items()) {
            if (!cells.is_object()) {
                return fail("the game event " + key + " is not an object");
            }
            EventCells values;
            for (const auto& [column, text] : cells.items()) {
                if (text.is_string()) {
                    values.emplace(column, text.get<std::string>());
                }
            }
            QuestGameEvent event = read_game_event(key, values);
            m_events.emplace(lower_text(event.id), std::move(event));
        }
    }
    // Code review of stage QG6a, finding QG6AR-R4: a file with town rows needs its town buckets, and each member of a bucket is a town row of
    // the goal of the key (MissionGoalType: Gather 1, Kill 4, Explore 5, the keys of the tool), in one bucket only. A file that does not
    // agree is not used as a whole (the backend would compute other offers than the client).
    const bool townRows = std::any_of(m_missions.begin(), m_missions.end(), [](const MissionRow& row) { return row.table == MissionTable::town; });
    if (townRows && !document.contains("townBuckets")) {
        return fail("the file has town missions and no member townBuckets");
    }
    if (document.contains("townBuckets")) {
        if (!document["townBuckets"].is_array()) {
            return fail("the member townBuckets is not a list");
        }
        std::set<std::uint8_t> keys;
        std::set<std::uint32_t> members;
        for (const json& entry : document["townBuckets"]) {
            if (!entry.is_object() || !entry.contains("key") || !entry["key"].is_number_unsigned() || entry["key"].get<std::uint64_t>() > 255
                || !entry.contains("missions") || !entry["missions"].is_array()) {
                return fail("a town bucket is not {key, missions}");
            }
            TownBucket bucket;
            bucket.key = static_cast<std::uint8_t>(entry["key"].get<std::uint64_t>());
            if (!keys.insert(bucket.key).second) {
                return fail("two town buckets have the key " + std::to_string(bucket.key));
            }
            for (const json& mission : entry["missions"]) {
                if (!mission.is_number_unsigned() || mission.get<std::uint64_t>() > 0xFFFFFFFFULL
                    || m_missionByCrc.count(static_cast<std::uint32_t>(mission.get<std::uint64_t>())) == 0) {
                    return fail("a town bucket names a mission that the file does not have");
                }
                const auto id = static_cast<std::uint32_t>(mission.get<std::uint64_t>());
                const MissionRow& row = m_missions[m_missionByCrc.at(id)];
                const char* goal = bucket.key == 1 ? "Gather" : bucket.key == 4 ? "Kill" : bucket.key == 5 ? "Explore" : "";
                if (row.table != MissionTable::town || row.goalType != goal) {
                    return fail("the town bucket " + std::to_string(bucket.key) + " has the mission " + row.id + " of the table "
                                + mission_table_text(row.table) + " and the goal " + row.goalType);
                }
                if (!members.insert(id).second) {
                    return fail("the mission " + row.id + " is in two town buckets or two times in one");
                }
                bucket.missions.push_back(id);
            }
            m_townBuckets.push_back(std::move(bucket));
        }
    }
    // Stage QG6b: the weights of the faction boards and the reward modifiers of the missions (the source text of each cell).
    if (document.contains("weights")) {
        if (!document["weights"].is_array()) {
            return fail("the member weights is not a list");
        }
        for (const json& entry : document["weights"]) {
            if (!entry.is_object() || !entry.contains("cells") || !entry["cells"].is_object()) {
                return fail("a weight row is not {table, cells}");
            }
            const json& cells = entry["cells"];
            const std::optional<std::uint32_t> slot = whole(cell(cells, "SlotNumber"));
            const std::optional<std::uint32_t> weight = whole(cell(cells, "Weight"));
            if (!slot || !weight) {
                return fail("the weight row " + cell(cells, "MissionWeightId") + " has a slot or a weight that is not a whole number");
            }
            m_weights.push_back(MissionWeight{cell(cells, "Bucket"), *slot, cell(cells, "MissionGoalType"), *weight});
        }
    }
    if (document.contains("rewardModifiers")) {
        if (!document["rewardModifiers"].is_array()) {
            return fail("the member rewardModifiers is not a list");
        }
        for (const json& entry : document["rewardModifiers"]) {
            if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_string() || !entry.contains("cells") || !entry["cells"].is_object()) {
                return fail("a reward modifier is not {id, cells}");
            }
            MissionModifier modifier;
            modifier.id = entry["id"].get<std::string>();
            const json& cells = entry["cells"];
            for (const auto& [column, target] : {std::pair<const char*, double*>{"Experience", &modifier.experience},
                                                 std::pair<const char*, double*>{"Currency", &modifier.currency},
                                                 std::pair<const char*, double*>{"TerritoryStanding", &modifier.standing},
                                                 std::pair<const char*, double*>{"FactionReputationModifier", &modifier.reputation},
                                                 std::pair<const char*, double*>{"FactionTokenModifier", &modifier.tokens}}) {
                const std::string text = cell(cells, column);
                const std::optional<double> value = real(text);
                if (!text.empty() && !value) {
                    return fail("the reward modifier " + modifier.id + " has a " + column + " that is not a number");
                }
                *target = value.value_or(0.0);
            }
            m_modifiers.push_back(std::move(modifier));
        }
    }
    m_loaded = true;
    return true;
}

const MissionModifier* MissionData::modifier(std::string_view id) const {
    const std::string key = lower_text(id);
    for (const MissionModifier& modifier : m_modifiers) {
        if (lower_text(modifier.id) == key) {
            return &modifier;
        }
    }
    return nullptr;
}

const MissionRow* MissionData::mission(std::uint32_t crc) const {
    const auto found = m_missionByCrc.find(crc);
    return found != m_missionByCrc.end() ? &m_missions[found->second] : nullptr;
}

const MissionRow* MissionData::mission(std::string_view id) const {
    return mission(quest_crc(id));
}

const MissionProject* MissionData::project(std::uint32_t crc) const {
    const auto found = m_projectByCrc.find(crc);
    return found != m_projectByCrc.end() ? &m_projects[found->second] : nullptr;
}

const QuestGameEvent* MissionData::game_event(std::string_view id) const {
    const auto found = m_events.find(lower_text(id));
    return found != m_events.end() ? &found->second : nullptr;
}

} // namespace soulwarden::backend::gamedata

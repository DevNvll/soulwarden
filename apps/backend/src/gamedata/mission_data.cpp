#include "gamedata/mission_data.h"

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
        "DevName", "VCLevel"};
    return kColumns;
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
    if (document.contains("townBuckets")) {
        if (!document["townBuckets"].is_array()) {
            return fail("the member townBuckets is not a list");
        }
        std::set<std::uint8_t> keys;
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
                bucket.missions.push_back(static_cast<std::uint32_t>(mission.get<std::uint64_t>()));
            }
            m_townBuckets.push_back(std::move(bucket));
        }
    }
    m_loaded = true;
    return true;
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

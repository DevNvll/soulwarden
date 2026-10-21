#include "gamedata/quest_data.h"

#include <cmath>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "server/hub/wire.h"

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

constexpr std::uint64_t kFormat = 1;
/** The task index of the wire is a u8 (task element: instance:u64 index:u8 ...; reader 0x3ADDB50). */
constexpr std::size_t kIndexLimit = 255;
/** The client loader reads the bit of an achievement as 16 bits (0x43B24A0). */
constexpr std::uint64_t kAchievementIndexLimit = 0xFFFF;

/**
 * The objective types of the client (EObjectiveType, table 0x3AA4A00; research quests.md 2.5): the index of the auto-pin flag of an
 * objective in message 2709. The other type texts have no known value.
 */
std::optional<std::uint8_t> objective_type_index(const std::string& type) {
    static const std::pair<const char*, std::uint8_t> kTypes[] = {{"objective", 0},   {"quest", 1},          {"crafting", 3},
                                                                 {"journey", 4},     {"mission", 5},        {"communitygoal", 6},
                                                                 {"mainstoryquest", 13}, {"dynamicpoi", 14}, {"event", 15}};
    const std::string key = lower_text(type);
    for (const auto& [name, value] : kTypes) {
        if (key == name) {
            return value;
        }
    }
    return std::nullopt;
}

/** @return The text of a member of a cell object, or an empty text. */
std::string cell(const json& cells, const char* name) {
    if (!cells.is_object()) {
        return {};
    }
    const auto found = cells.find(name);
    return found != cells.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

/** @return True for the texts of a flag that is on ("1", "true"; a number cell of 1 has the text "1" or "1.0"). */
bool flag(const std::string& text) {
    if (text.empty()) {
        return false;
    }
    const std::string key = lower_text(text);
    if (key == "true") {
        return true;
    }
    char* end = nullptr;
    const double value = std::strtod(key.c_str(), &end);
    return end != key.c_str() && value != 0.0;
}

/** @return A number of a cell text, or no value for a blank or not finite text. */
std::optional<double> number(const std::string& text) {
    if (text.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

std::vector<std::string> text_list(const json& value) {
    std::vector<std::string> list;
    if (value.is_array()) {
        for (const json& entry : value) {
            if (entry.is_string()) {
                list.push_back(entry.get<std::string>());
            }
        }
    }
    return list;
}

} // namespace

std::uint32_t quest_crc(std::string_view text) {
    const std::string lower = lower_text(text);
    return server::hub::crc32(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(lower.data()), lower.size()));
}

bool QuestData::fail(std::string cause) {
    *this = QuestData{};
    m_error = std::move(cause);
    return false;
}

bool QuestData::load(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return fail("the file is not there");
    }
    std::ostringstream text;
    text << input.rdbuf();
    return load_text(text.str());
}

bool QuestData::load_text(const std::string& text) {
    *this = QuestData{};
    const json document = json::parse(text, nullptr, false);
    if (!document.is_object()) {
        return fail("the file is not a JSON object");
    }
    if (!document.contains("format") || !document["format"].is_number_unsigned() || document["format"].get<std::uint64_t>() != kFormat) {
        return fail("the file does not have the format 1");
    }
    for (const char* member : {"objectives", "achievements", "territories", "backstories"}) {
        if (!document.contains(member) || !document[member].is_array()) {
            return fail(std::string("the file has no list ") + member);
        }
    }
    if (!document.contains("tasks") || !document["tasks"].is_object()) {
        return fail("the file has no task object");
    }
    // The tasks of the trees, by their lower-case id.
    std::unordered_map<std::string, std::uint32_t> taskByKey;
    std::unordered_map<std::uint32_t, std::string> taskCrcs;
    for (const auto& [key, entry] : document["tasks"].items()) {
        if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_string() || !entry.contains("code")
            || !entry["code"].is_number_unsigned() || entry["code"].get<std::uint64_t>() > 255) {
            return fail("the task " + key + " does not have the members id and code");
        }
        QuestTask task;
        task.id = entry["id"].get<std::string>();
        if (lower_text(task.id) != key) {
            return fail("the task key " + key + " is not the lower-case id " + task.id);
        }
        task.crc = quest_crc(task.id);
        const auto other = taskCrcs.find(task.crc);
        if (other != taskCrcs.end()) {
            return fail("the tasks " + other->second + " and " + task.id + " have one CRC-32");
        }
        taskCrcs.emplace(task.crc, task.id);
        task.type = static_cast<std::uint8_t>(entry["code"].get<std::uint64_t>());
        const json& cells = entry.contains("cells") ? entry["cells"] : json::object();
        task.itemName = cell(cells, "ItemName");
        if (const std::optional<double> target = number(cell(cells, "TargetQty"))) {
            if (*target != std::floor(*target) || *target < 0.0 || *target > 2147483647.0) {
                return fail("the task " + task.id + " has a TargetQty that is not a whole number of 0 or more");
            }
            task.targetQty = static_cast<std::int64_t>(*target);
        }
        task.stayActive = flag(cell(cells, "StayActive"));
        task.failOnCondition = flag(cell(cells, "FailOnCondition"));
        task.hidden = flag(cell(cells, "IsHidden"));
        task.duration = number(cell(cells, "Duration"));
        if (task.duration && *task.duration < 0.0) {
            return fail("the task " + task.id + " has a Duration below 0");
        }
        task.description = cell(cells, "TP_DescriptionTag");
        taskByKey.emplace(key, static_cast<std::uint32_t>(m_tasks.size()));
        m_tasks.push_back(std::move(task));
    }
    // The objectives and their trees.
    for (const json& entry : document["objectives"]) {
        if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_string() || !entry.contains("tree") || !entry["tree"].is_array()
            || entry["tree"].empty()) {
            return fail("an objective does not have the members id and tree");
        }
        QuestObjective objective;
        objective.id = entry["id"].get<std::string>();
        objective.crc = quest_crc(objective.id);
        const std::string key = lower_text(objective.id);
        if (m_objectiveById.count(key) != 0) {
            return fail("two objectives have the id " + objective.id);
        }
        const auto sameCrc = m_objectiveByCrc.find(objective.crc);
        if (sameCrc != m_objectiveByCrc.end()) {
            return fail("the objectives " + m_objectives[sameCrc->second].id + " and " + objective.id + " have one CRC-32");
        }
        const json& cells = entry.contains("cells") ? entry["cells"] : json::object();
        objective.type = cell(cells, "Type");
        objective.typeIndex = objective_type_index(objective.type);
        objective.destination = cell(cells, "NpcDestinationId");
        objective.achievement = cell(cells, "AchievementId");
        objective.required = cell(cells, "RequiredAchievementId");
        objective.successEvent = cell(cells, "SuccessGameEventId");
        if (const std::optional<double> territory = number(cell(cells, "ExclusiveTerritory"))) {
            if (*territory < 0.0 || *territory > 65535.0 || *territory != std::floor(*territory)) {
                return fail("the objective " + objective.id + " has an ExclusiveTerritory that is not a territory id");
            }
            objective.exclusiveTerritory = static_cast<std::uint16_t>(*territory);
        }
        const std::string abandon = cell(cells, "CanBeAbandoned");
        objective.canAbandon = abandon.empty() || flag(abandon);
        objective.title = cell(cells, "Title");
        std::size_t lastIndex = 0;
        bool first = true;
        for (const json& node : entry["tree"]) {
            if (!node.is_array() || node.size() != 3 || !node[0].is_number_unsigned() || !node[1].is_string() || !node[2].is_array()) {
                return fail("the objective " + objective.id + " has a tree node with a wrong form");
            }
            const std::uint64_t index = node[0].get<std::uint64_t>();
            if (index > kIndexLimit) {
                return fail("the objective " + objective.id + " has a task index above 255");
            }
            if (!first && index <= lastIndex) {
                return fail("the tree of the objective " + objective.id + " is not in the order of its indexes");
            }
            first = false;
            lastIndex = index;
            const auto task = taskByKey.find(node[1].get<std::string>());
            if (task == taskByKey.end()) {
                return fail("the objective " + objective.id + " names the task " + node[1].get<std::string>() + ", which the file does not have");
            }
            if (objective.nodes.size() <= index) {
                objective.nodes.resize(index + 1, QuestNode{kNoTask, {}, std::nullopt});
            }
            QuestNode& target = objective.nodes[index];
            target.task = task->second;
            for (const json& child : node[2]) {
                if (!child.is_number_unsigned() || child.get<std::uint64_t>() >= index) {
                    return fail("the objective " + objective.id + " has a child index that is not below its container");
                }
                target.children.push_back(static_cast<std::uint8_t>(child.get<std::uint64_t>()));
            }
            if (!target.children.empty() && !is_container(m_tasks[target.task].type)) {
                return fail("the objective " + objective.id + " has children under a task that is not a container");
            }
        }
        // Each child must be a node of the tree, with one container.
        for (std::size_t index = 0; index < objective.nodes.size(); ++index) {
            for (const std::uint8_t child : objective.nodes[index].children) {
                QuestNode& node = objective.nodes[child];
                if (node.task == kNoTask) {
                    return fail("the objective " + objective.id + " has a child index with no node");
                }
                if (node.parent) {
                    return fail("the objective " + objective.id + " has a node with two containers");
                }
                node.parent = static_cast<std::uint8_t>(index);
            }
        }
        // The effective StayActive and the target of each node, children first (the index order).
        for (QuestNode& node : objective.nodes) {
            if (node.task == kNoTask) {
                continue;
            }
            const QuestTask& task = m_tasks[node.task];
            node.stayActive = task.stayActive || (task.type == task_type::kTimer && task.failOnCondition);
            for (const std::uint8_t child : node.children) {
                node.stayActive = node.stayActive || objective.nodes[child].stayActive;
            }
            if (task.type == task_type::kPartialContainer) {
                node.target = 1;
            } else if (is_container(task.type)) {
                node.target = static_cast<std::uint32_t>(node.children.size());
            } else {
                node.target = task.targetQty ? static_cast<std::uint32_t>(*task.targetQty) : 1U;
            }
        }
        objective.root = static_cast<std::uint8_t>(objective.nodes.size() - 1);
        if (objective.nodes[objective.root].parent) {
            return fail("the root of the objective " + objective.id + " has a container");
        }
        m_objectiveById.emplace(key, m_objectives.size());
        m_objectiveByCrc.emplace(objective.crc, m_objectives.size());
        m_objectives.push_back(std::move(objective));
    }
    // The achievement table.
    for (const json& row : document["achievements"]) {
        if (!row.is_array() || row.size() < 2 || !row[0].is_string() || !row[1].is_number_unsigned()
            || row[1].get<std::uint64_t>() > kAchievementIndexLimit) {
            return fail("an achievement row does not have an id and a bit of 0 to 65535");
        }
        const std::string id = row[0].get<std::string>();
        const auto index = static_cast<std::uint32_t>(row[1].get<std::uint64_t>());
        if (!m_achievementByName.emplace(lower_text(id), index).second) {
            return fail("two achievement rows have the id " + id);
        }
        if (!m_achievementIds.emplace(index, id).second) {
            return fail("two achievement rows have the bit " + std::to_string(index));
        }
    }
    if (document.contains("npa") && document["npa"].is_array()) {
        for (const json& row : document["npa"]) {
            if (row.is_array() && row.size() >= 2 && row[0].is_string() && row[1].is_string()) {
                QuestNpa npa;
                npa.conditional = row[1].get<std::string>();
                if (row.size() >= 4 && row[3].is_string()) {
                    npa.status = row[3].get<std::string>();
                }
                m_npa.emplace(lower_text(row[0].get<std::string>()), std::move(npa));
            }
        }
    }
    for (const json& row : document["territories"]) {
        if (!row.is_object() || !row.contains("territory") || !row["territory"].is_number_unsigned() || !row.contains("objective")
            || !row["objective"].is_string() || row["territory"].get<std::uint64_t>() > 65535) {
            return fail("a territory row does not have a territory and an objective");
        }
        const auto found = m_objectiveById.find(lower_text(row["objective"].get<std::string>()));
        if (found != m_objectiveById.end()) {
            m_territoryObjective.emplace(static_cast<std::uint16_t>(row["territory"].get<std::uint64_t>()), found->second);
        }
    }
    for (const json& row : document["backstories"]) {
        if (!row.is_object() || !row.contains("id") || !row["id"].is_string()) {
            return fail("a backstory row has no id");
        }
        QuestBackstory backstory;
        backstory.id = row["id"].get<std::string>();
        const auto text = [&](const char* name) {
            return row.contains(name) && row[name].is_string() ? row[name].get<std::string>() : std::string{};
        };
        backstory.name = text("name");
        backstory.purpose = text("purpose");
        backstory.playtest = text("playtest");
        backstory.level = text("level");
        backstory.faction = text("faction");
        backstory.objectiveText = text("objectiveText");
        backstory.objectives = text_list(row.contains("objectives") ? row["objectives"] : json());
        backstory.achievements = text_list(row.contains("achievements") ? row["achievements"] : json());
        backstory.archetype = row.contains("archetype") && row["archetype"].is_boolean() && row["archetype"].get<bool>();
        if (!m_backstoryById.emplace(lower_text(backstory.id), m_backstories.size()).second) {
            return fail("two backstory rows have the id " + backstory.id);
        }
        m_backstories.push_back(std::move(backstory));
    }
    m_loaded = true;
    return true;
}

const QuestObjective* QuestData::objective(std::string_view id) const {
    const auto found = m_objectiveById.find(lower_text(id));
    return found != m_objectiveById.end() ? &m_objectives[found->second] : nullptr;
}

const QuestObjective* QuestData::objective_by_crc(std::uint32_t crc) const {
    const auto found = m_objectiveByCrc.find(crc);
    return found != m_objectiveByCrc.end() ? &m_objectives[found->second] : nullptr;
}

std::optional<std::uint32_t> QuestData::achievement_index(std::string_view id) const {
    const auto found = m_achievementByName.find(lower_text(id));
    return found != m_achievementByName.end() ? std::optional<std::uint32_t>(found->second) : std::nullopt;
}

const std::string& QuestData::achievement_id(std::uint32_t index) const {
    static const std::string kNone;
    const auto found = m_achievementIds.find(index);
    return found != m_achievementIds.end() ? found->second : kNone;
}

const QuestNpa* QuestData::npa(std::string_view id) const {
    const auto found = m_npa.find(lower_text(id));
    return found != m_npa.end() ? &found->second : nullptr;
}

const QuestObjective* QuestData::territory_objective(std::uint16_t territory) const {
    const auto found = m_territoryObjective.find(territory);
    return found != m_territoryObjective.end() ? &m_objectives[found->second] : nullptr;
}

const QuestBackstory* QuestData::backstory(std::string_view id) const {
    const auto found = m_backstoryById.find(lower_text(id));
    return found != m_backstoryById.end() ? &m_backstories[found->second] : nullptr;
}

} // namespace soulwarden::backend::gamedata

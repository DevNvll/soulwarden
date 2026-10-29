#include "gamedata/quest_data.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "gamedata/mission_data.h"
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

std::string trimmed(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}

/** @return The parts of a list cell ("a, b,c"): each part trimmed and in lower case; an empty part is left out. */
std::vector<std::string> lower_list(const std::string& text) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = std::min(text.find(',', start), text.size());
        std::string part = lower_text(trimmed(text.substr(start, end - start)));
        if (!part.empty()) {
            parts.push_back(std::move(part));
        }
        start = end + 1;
    }
    return parts;
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

std::vector<QuestFlowClause> parse_objective_flow(const std::string& text) {
    // The client parser 0x43BD930 (static read A1 of stage QG5b): the split at "," (0x43BDBBF), the prefix before the first ":" (0x43BDF9F;
    // only "u" and "l", 0x833DEB4 and 0x833DEB8; another prefix leaves the clause out, 0x43BE66E), then OBJECTIVE, "/TASK", ":MODIFIER".
    // The objective modifiers are a, c, r (0x43BD9DD); the task modifiers a and c (0x43BDB6C); no modifier and an unknown modifier keep the
    // flag of a (0x43BE551).
    std::vector<QuestFlowClause> clauses;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = std::min(text.find(',', start), text.size());
        const std::string part = trimmed(text.substr(start, end - start));
        start = end + 1;
        const std::size_t colon = part.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const std::string prefix = lower_text(trimmed(part.substr(0, colon)));
        if (prefix != "u" && prefix != "l") {
            continue;
        }
        QuestFlowClause clause;
        clause.unlock = prefix == "u";
        std::string target = part.substr(colon + 1);
        std::string modifier;
        if (const std::size_t second = target.find(':'); second != std::string::npos) {
            modifier = lower_text(trimmed(target.substr(second + 1)));
            target = target.substr(0, second);
        }
        if (const std::size_t slash = target.find('/'); slash != std::string::npos) {
            clause.objective = trimmed(target.substr(0, slash));
            clause.task = trimmed(target.substr(slash + 1));
        } else {
            clause.objective = trimmed(target);
        }
        if (modifier == "c" || (modifier == "r" && clause.task.empty())) {
            clause.modifier = modifier[0];
        }
        clauses.push_back(std::move(clause));
    }
    return clauses;
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
        if (entry.contains("type") && entry["type"].is_string()) {
            task.typeName = entry["type"].get<std::string>();
        }
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
        // Stage 1.8c: the cells of the event tasks.
        task.targets = lower_list(cell(cells, "KillEnemyType"));
        task.weapon = lower_text(trimmed(cell(cells, "KillEnemyWeaponID")));
        task.poiTags = lower_list(cell(cells, "POITag"));
        if (const std::optional<double> territory = number(cell(cells, "TerritoryID"));
            territory && *territory > 0.0 && *territory <= 65535.0 && *territory == std::floor(*territory)) {
            task.territory = static_cast<std::uint16_t>(*territory);
        }
        task.enforceLocation = flag(cell(cells, "EnforceLocationRequirement"));
        task.pin = !trimmed(cell(cells, "OverrideLocation")).empty();
        task.dropSources = lower_list(cell(cells, "ItemDropVC"));
        task.dropChance = std::clamp(number(cell(cells, "ItemDropProbability")).value_or(0.0), 0.0, 100.0);
        task.chestChance = std::clamp(number(cell(cells, "ChestDropProbability")).value_or(0.0), 0.0, 100.0); // Stage QG5a.
        task.guaranteedAttempts = static_cast<std::uint32_t>(std::clamp(number(cell(cells, "GuranteedDropAttempts")).value_or(0.0), 0.0, 1e6));
        task.awardOnce = flag(cell(cells, "AwardOnlyOnce"));
        task.pauseLoot = flag(cell(cells, "PauseAndClearLootOnTaskComplete"));
        task.useGlobalStorage = flag(cell(cells, "UseGlobalStorage"));
        task.minGearScore = static_cast<std::uint32_t>(std::clamp(number(cell(cells, "MinGearScore")).value_or(0.0), 0.0, 1e6));
        task.resetOnCondition = flag(cell(cells, "ResetOnCondition"));
        task.recipe = lower_text(trimmed(cell(cells, "CraftingRecipeId")));
        task.anyRecipe = flag(cell(cells, "CraftAnyRecipe"));
        task.interactTag = lower_text(trimmed(cell(cells, "InteractTag")));
        task.onInteractStart = flag(cell(cells, "OnInteractStart"));
        task.screen = trimmed(cell(cells, "UIScreenTag"));
        task.screenCrc = task.screen.empty() ? 0U : quest_crc(task.screen);
        // Stage 1.8b.
        task.targetTag = lower_text(trimmed(cell(cells, "TP_TargetTag")));
        const std::string rule = lower_text(trimmed(cell(cells, "TriggerAreaType")));
        task.areaRule = rule == "leave" ? AreaRule::leave : rule == "stay inside" ? AreaRule::stayInside : AreaRule::enter;
        task.nameCrc = trimmed(task.itemName).empty() ? 0U : quest_crc(trimmed(task.itemName));
        task.checkPoints = flag(cell(cells, "ShouldCheckPoints")); // Stage QG2a: TaskHaveLevel.
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
        if (entry.contains("table") && entry["table"].is_string()) {
            objective.table = entry["table"].get<std::string>();
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
        // Stage 1.8b: the rewards of the row.
        objective.itemReward = trimmed(cell(cells, "ItemRewardName"));
        if (const std::optional<double> quantity = number(cell(cells, "ItemRewardQty"))) {
            if (*quantity != std::floor(*quantity) || std::fabs(*quantity) > 1e9) {
                return fail("the objective " + objective.id + " has an ItemRewardQty that is not a whole number");
            }
            objective.itemRewardQty = static_cast<std::int64_t>(*quantity);
        }
        if (const std::optional<double> experience = number(cell(cells, "ExperienceReward")); experience && *experience > 0.0) {
            objective.experienceReward = static_cast<std::uint64_t>(std::min(*experience, 1e12));
        }
        objective.progressionReward = trimmed(cell(cells, "ProgressionReward"));
        objective.resetOnFactionChange = flag(cell(cells, "ShouldResetOnFactionChange"));
        objective.objectiveReward = trimmed(cell(cells, "ObjectiveReward"));
        // Stage 1.8d: the availability and the repeat (loader 0x445CA30; research quests-18d rules).
        if (const std::optional<double> level = number(cell(cells, "RequiredLevel")); level && *level > 0.0) {
            objective.requiredLevel = static_cast<std::uint32_t>(std::min(*level, 1e6));
        }
        objective.factionMask = faction_mask(trimmed(cell(cells, "RequiredFaction")));
        objective.questArc = trimmed(cell(cells, "QuestArcTag"));
        objective.worldTags = trimmed(cell(cells, "RequiredWorldTags"));
        objective.gameMode = trimmed(cell(cells, "RequiredGameModeId"));
        objective.requiredProgression = trimmed(cell(cells, "RequiredProgression"));
        objective.skipInvalidRequirement = flag(cell(cells, "SkipReqAchievmentWhenInvalid"));
        objective.pushOnRequirement = flag(cell(cells, "PushObjectiveOnReqMet"));
        objective.pushRetrigger = flag(cell(cells, "PushObjectiveOnReqRetrigger"));
        objective.repeatable = flag(cell(cells, "Repeatable"));
        objective.schedule = trimmed(cell(cells, "ScheduleId"));
        objective.resetOnScheduleEnd = flag(cell(cells, "ShouldResetInProgressQuestOnScheduleEnd"));
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
            // Stage 1.8c: TaskDie is a condition that stays active (its death fails or resets the objective: client 0x825E5F0, 0x825E628;
            // a decision of an agent: a satisfied TaskDie with no StayActive would stop watching the death).
            // Stage QG6b: the dwell task of a PatrolTask is StayActive (the constructor of TaskGoToDuration sets +0x2FD: 0x3B0101C).
            node.stayActive = task.stayActive || (task.type == task_type::kTimer && task.failOnCondition) || task.type == task_type::kDie
                              || task.type == task_type::kPatrol;
            for (const std::uint8_t child : node.children) {
                node.stayActive = node.stayActive || objective.nodes[child].stayActive;
            }
            // Stage QG2a: TaskHaveLevel is a condition with the base target 1 (constructor 0x3B01C80 passes 1 to the base task 0x3ACD210 and
            // keeps TargetQty at +0x550 for its own compare: the callback 0x3B073A0 sets the progress 1); TargetQty is its level.
            if (task.type == task_type::kPartialContainer || task.type == task_type::kHaveLevel) {
                node.target = 1;
            } else if (is_container(task.type)) {
                node.target = static_cast<std::uint32_t>(node.children.size());
            } else if (task.type == task_type::kPatrol) {
                // Stage QG6b: the target of the dwell task is the whole seconds of its Duration (0x3B00DB1; 0 seconds: 1).
                node.target = patrol_target(task);
            } else {
                node.target = task.targetQty ? static_cast<std::uint32_t>(*task.targetQty) : 1U;
            }
        }
        objective.root = static_cast<std::uint8_t>(objective.nodes.size() - 1);
        if (objective.nodes[objective.root].parent) {
            return fail("the root of the objective " + objective.id + " has a container");
        }
        // One root (code review of stage 1.8a, finding QR8A-R6): each other node of a task has a container. A task that no container
        // reaches never runs (an achievement action of its start would be lost): the file is not complete. A hole stays a hole.
        for (std::size_t index = 0; index + 1 < objective.nodes.size(); ++index) {
            if (objective.nodes[index].task != kNoTask && !objective.nodes[index].parent) {
                return fail("the objective " + objective.id + " has the task index " + std::to_string(index) + ", which no container reaches");
            }
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
                if (row.size() >= 3 && row[2].is_string()) {
                    npa.flow = trimmed(row[2].get<std::string>()); // Finding QG4R-R2.
                    npa.flowClauses = parse_objective_flow(npa.flow);
                }
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
    // Stage 1.8b: the game events (rewards, whispers) and the whispers. A file of before stage 1.8b has the events with fewer columns: an
    // event with no whisper column plays the whisper whose GameEvent names it.
    if (document.contains("events") && document["events"].is_object()) {
        for (const auto& [key, cells] : document["events"].items()) {
            if (!cells.is_object()) {
                return fail("the game event " + key + " is not an object");
            }
            // Stage QG2a: one reader of the reward cells (gamedata/game_event.h): the salvage events of the loot data use it too.
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
    if (document.contains("whispers") && document["whispers"].is_array()) {
        for (const json& row : document["whispers"]) {
            if (!row.is_object() || !row.contains("id") || !row["id"].is_string()) {
                return fail("a whisper row has no id");
            }
            QuestWhisper whisper;
            whisper.id = row["id"].get<std::string>();
            whisper.crc = quest_crc(whisper.id);
            const json& cells = row.contains("cells") ? row["cells"] : json::object();
            whisper.gameEvent = trimmed(cell(cells, "GameEvent"));
            whisper.achievement = trimmed(cell(cells, "AchievementId"));
            if (!m_whisperById.emplace(lower_text(whisper.id), m_whispers.size()).second) {
                return fail("two whisper rows have the id " + whisper.id);
            }
            if (!whisper.gameEvent.empty()) {
                m_whisperByEvent.emplace(lower_text(whisper.gameEvent), m_whispers.size());
            }
            m_whispers.push_back(std::move(whisper));
        }
    }
    // Stage 1.8d: the schedule rows (ScheduleData; research quest-engine.md Q5): the kinds of a repeat. A row of dates (a seasonal window) is
    // `none`: decision D4 of the user is open, and the backend has no event calendar.
    if (document.contains("schedules") && document["schedules"].is_array()) {
        for (const json& row : document["schedules"]) {
            if (!row.is_object() || !row.contains("id") || !row["id"].is_string()) {
                return fail("a schedule row has no id");
            }
            const json& cells = row.contains("cells") ? row["cells"] : json::object();
            const std::string start = trimmed(cell(cells, "StartDate"));
            const std::string weekday = trimmed(cell(cells, "StartWeekday"));
            ScheduleKind kind = ScheduleKind::none;
            if (start == "*-*-*T*:00:00" && trimmed(cell(cells, "EndDate")).empty()) {
                kind = ScheduleKind::hourly;
            } else if (start == "*-*-*TDailyCooldown") {
                kind = ScheduleKind::daily;
            } else if (weekday == "WeeklyCooldown" && start.empty()) {
                kind = ScheduleKind::weekly;
            }
            m_schedules[lower_text(row["id"].get<std::string>())] = kind;
        }
    }
    m_loaded = true;
    return true;
}

ScheduleKind QuestData::schedule_kind(std::string_view id) const {
    const auto found = m_schedules.find(lower_text(id));
    return found != m_schedules.end() ? found->second : ScheduleKind::none;
}

std::uint8_t faction_mask(std::string_view text) {
    // Client parser 0x445CA30 (instructions 0x445D18F to 0x445D363): one leading "!", then Syndicate 1, Marauder 2, Covenant 3, Any 4, each
    // other text 0. A positive name gives the bit of its faction (bit N + 1); Any gives 0xE; 0 gives 0xF. A negation gives the complement in
    // the four bits (!Any: 0x1, !None: 0xE). Bit 0 bypasses the test of the eligibility (0x3A4C166).
    bool negated = false;
    std::string_view name = text;
    while (!name.empty() && name.front() == ' ') {
        name.remove_prefix(1);
    }
    if (!name.empty() && name.front() == '!') {
        negated = true;
        name.remove_prefix(1);
    }
    const std::string key = lower_text(name);
    std::uint8_t mask = 0;
    if (key == "syndicate") {
        mask = 0x2;
    } else if (key == "marauder") {
        mask = 0x4;
    } else if (key == "covenant") {
        mask = 0x8;
    } else if (key == "any") {
        mask = 0xE;
    }
    if (mask == 0) {
        return negated ? 0xE : kAnyFactionMask; // No faction: each faction; "!None" (or "!"): a faction 1 to 3.
    }
    return negated ? static_cast<std::uint8_t>(~mask & 0x0F) : mask;
}

const QuestGameEvent* QuestData::game_event(std::string_view id) const {
    const auto found = m_events.find(lower_text(id));
    return found != m_events.end() ? &found->second : nullptr;
}

const QuestWhisper* QuestData::whisper(std::string_view id) const {
    const auto found = m_whisperById.find(lower_text(id));
    return found != m_whisperById.end() ? &m_whispers[found->second] : nullptr;
}

const QuestWhisper* QuestData::whisper_of_event(std::string_view eventId) const {
    if (const QuestGameEvent* event = game_event(eventId); event != nullptr && !event->whisper.empty()) {
        return whisper(event->whisper);
    }
    const auto found = m_whisperByEvent.find(lower_text(eventId));
    return found != m_whisperByEvent.end() ? &m_whispers[found->second] : nullptr;
}

const QuestObjective* QuestData::objective(std::string_view id) const {
    const auto found = m_objectiveById.find(lower_text(id));
    return found != m_objectiveById.end() ? &m_objectives[found->second] : nullptr;
}

const QuestObjective* QuestData::objective_by_crc(std::uint32_t crc) const {
    const auto found = m_objectiveByCrc.find(crc);
    return found != m_objectiveByCrc.end() ? &m_objectives[found->second] : nullptr;
}

const QuestObjective* QuestData::objective_by_crc(std::uint32_t crc, std::uint32_t mission) const {
    if (mission != 0) {
        const auto found = m_missions.find(mission);
        if (found != m_missions.end() && found->second.objective != nullptr && found->second.objective->crc == crc) {
            return found->second.objective;
        }
    }
    return objective_by_crc(crc);
}

const MissionBinding* QuestData::mission(std::uint32_t missionCrc) const {
    const auto found = m_missions.find(missionCrc);
    return found != m_missions.end() ? &found->second : nullptr;
}

void QuestData::bind_missions(const MissionData& missions) {
    // A second call drops the variants and the events of the first one.
    if (!m_missions.empty() || !m_variants.empty()) {
        m_tasks.resize(m_fileTasks);
        for (const std::string& event : m_missionEvents) {
            m_events.erase(event);
        }
    }
    m_fileTasks = m_tasks.size();
    m_missions.clear();
    m_variants.clear();
    m_missionEvents.clear();
    if (!m_loaded || !missions.loaded()) {
        return;
    }
    // The events of the missions (SuccessGameEventIdOverride, FailureGameEventIdOverride) that the quest data does not have.
    for (const auto& [key, event] : missions.events()) {
        if (m_events.emplace(key, event).second) {
            m_missionEvents.push_back(key);
        }
    }
    for (const MissionRow& row : missions.missions()) {
        MissionBinding binding;
        binding.row = &row;
        binding.templateObjective = row.objective.empty() ? nullptr : objective(row.objective);
        if (binding.templateObjective == nullptr) {
            binding.refusals.push_back("the template " + (row.objective.empty() ? std::string("(blank)") : row.objective) +
                                       " has no row of the quest data");
            m_missions.emplace(row.crc, std::move(binding));
            continue;
        }
        const QuestObjective& base = *binding.templateObjective;
        if (!row.enabled) {
            binding.refusals.push_back("Enabled is 0");
        }
        if (row.pvp) {
            binding.refusals.push_back("a PvP mission (IsPvpMission 1): the PvP missions (3818) are a later stage");
        }
        if (row.outpostDestination) {
            binding.refusals.push_back("IsOutpostValidDestination 1: the destination of an outpost has no rule (a later stage)");
        }
        const MissionOverrides result = apply_overrides(row, base);
        for (const std::string& refusal : result.refusals) {
            binding.refusals.push_back(refusal);
        }
        if (result.variant) {
            m_variants.push_back(std::move(*result.variant));
            binding.objective = &m_variants.back();
        } else {
            binding.objective = &base;
        }
        m_missions.emplace(row.crc, std::move(binding));
    }
}

MissionOverrides QuestData::apply_overrides(const MissionRow& row, const QuestObjective& base) {
    // Stage QG6b (docs/systems/mission-boards.md, the overrides; static read evidence/quests/qg6b/implement/static/factory/FACTORY.md): the
    // task factory 0x4461150 builds each leaf of the template with the cells of the row, only for a valid row with IsCustomObjective false
    // (0x44611A5, 0x44611AE). A custom row keeps the tasks of its template. The authored containers keep their values (the tree builder
    // 0x4466350 builds them apart).
    MissionOverrides result;
    if (row.custom) {
        return result;
    }
    for (const auto& [column, value] : row.overrides) {
        result.refusals.push_back(column + " " + value + ": the backend has no rule of this column");
    }
    if (!row.fortStatus.empty()) {
        result.refusals.push_back("RequiredFortControlStatus " + row.fortStatus + ": no rule (no row of the supported build has it)");
    }
    QuestObjective variant = base;
    bool changed = false;
    for (QuestNode& node : variant.nodes) {
        if (node.task == kNoTask || is_container(m_tasks[node.task].type)) {
            continue;
        }
        const QuestTask& original = m_tasks[node.task];
        QuestTask task = original;
        std::uint32_t target = node.target;
        // The common parameters of each leaf (0x44611D5 to 0x4461219): a TerritoryIDOverride that is not 0 replaces TerritoryID; a POI tag
        // list that is not empty replaces POITag (no append: 0x2D13A0); an OverrideLocation list replaces the location list, but the return
        // task (the tag ReturnToGiver) gets its template list again and the destination of the instance (0x446272B to 0x446273F).
        if (row.territoryOverride != 0) {
            task.territory = row.territoryOverride;
        }
        if (!row.poiTags.empty()) {
            task.poiTags = row.poiTags;
        }
        if (!row.location.empty() && !(task.type == task_type::kInteract && task.interactTag == "returntogiver")) {
            task.pin = true;
        }
        switch (task.type) {
        case task_type::kKillContribution:
            // Case 13 (0x446229F to 0x4462364): the category and the quantity of the row, always (a blank cell is the CRC 0 and 0).
            task.targets = row.killCategory.empty() ? std::vector<std::string>{} : std::vector<std::string>{lower_text(row.killCategory)};
            task.targetQty = static_cast<std::int64_t>(row.killQuantity.value_or(0));
            target = row.killQuantity.value_or(0);
            break;
        case task_type::kHaveItems:
        case task_type::kHaveAndReturnItems:
        case task_type::kGatherItems:
        case task_type::kGatherAndReturnItems:
            // The item branch 0x4463798 to 0x446395C: the item, the quantity, the two drop chances, and the drop sources of the row, always
            // (a blank cell is 0 or empty); the pause of the loot and the global storage only when the row has the cell (+0x141, +0x147).
            task.itemName = row.item;
            task.targetQty = static_cast<std::int64_t>(row.itemQuantity.value_or(0));
            target = row.itemQuantity.value_or(0);
            task.dropChance = row.itemDropChance.value_or(0.0);
            task.chestChance = row.chestDropChance.value_or(0.0);
            task.dropSources = row.dropSources;
            if (row.pauseLoot) {
                task.pauseLoot = *row.pauseLoot;
            }
            if (row.useGlobalStorage) {
                task.useGlobalStorage = *row.useGlobalStorage;
            }
            if (!row.lootTag.empty()) {
                result.refusals.push_back("LootTagOverride " + row.lootTag + " on " + task.id + ": the backend has no rule of a loot tag");
            }
            break;
        case task_type::kGiveItem:
        case task_type::kGiveAndTakeItem:
        case task_type::kRemoveItem:
            // Cases 33, 34, 36: the item of TaskGiveAndTakeItemOverride, always; the quantity of the template.
            task.itemName = row.giveItem;
            break;
        case task_type::kTimer:
            // Case 64 (0x44628E0): the duration of TaskTimerOverride, always (a blank cell is 0).
            task.duration = row.timerSeconds.value_or(0.0);
            break;
        case task_type::kPatrol: {
            // Case 66 (0x4461465 to 0x446195A): the duration of PatrolDuration and the POIs of PatrolPoi1 to PatrolPoi4. One POI makes one
            // TaskGoToDuration with the id, the CRC, and the index of the template task. No POI makes no task, and more POIs make a child for
            // each POI and a container: both change the tree of the template (no row of the supported build has them). Code review of stage
            // QG6b, finding QG6BR-R4: the one-POI branch gives the existing common block to the constructor (0x4461912, 0x446191A): the
            // place is the effective common POITag (POITagOverride, else the template), not PatrolPoi1 (it only selects the branch, 0x4461547).
            if (row.patrolPois.size() != 1) {
                result.refusals.push_back("PatrolPoi with " + std::to_string(row.patrolPois.size()) + " POIs on " + task.id
                                          + ": the patrol tree of the client is not the tree of the template (no row of the build)");
            }
            task.duration = row.patrolSeconds.value_or(0.0);
            target = patrol_target(task);
            break;
        }
        default:
            break;
        }
        if (!same_effect(task, original) || target != node.target) {
            node.target = target;
            node.task = static_cast<std::uint32_t>(m_tasks.size());
            m_tasks.push_back(std::move(task));
            changed = true;
        }
    }
    if (changed) {
        result.variant = std::move(variant);
    }
    return result;
}

bool QuestData::same_effect(const QuestTask& left, const QuestTask& right) {
    return left.territory == right.territory && left.poiTags == right.poiTags && left.pin == right.pin && left.targets == right.targets
           && left.targetQty == right.targetQty && left.itemName == right.itemName && left.dropChance == right.dropChance
           && left.chestChance == right.chestChance && left.dropSources == right.dropSources && left.pauseLoot == right.pauseLoot
           && left.useGlobalStorage == right.useGlobalStorage && left.duration == right.duration;
}

std::uint32_t QuestData::patrol_target(const QuestTask& task) {
    // The constructor of TaskGoToDuration (0x3B00DB1, 0x3B00F29): the whole seconds of the duration; a duration of exactly 0 has the target 1.
    const double seconds = task.duration.value_or(0.0);
    if (seconds <= 0.0) {
        return 1;
    }
    return static_cast<std::uint32_t>(std::min(std::floor(seconds), 4294967295.0));
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

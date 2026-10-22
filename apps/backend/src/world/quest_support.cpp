#include "world/quest_support.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "gamedata/conversation_data.h"
#include "gamedata/crafting_data.h"
#include "gamedata/creature_data.h"
#include "gamedata/gathering_data.h"
#include "gamedata/interaction_data.h"
#include "gamedata/item_catalog.h"
#include "gamedata/loot_tables.h"
#include "gamedata/poi_data.h"
#include "gamedata/progression_tables.h"
#include "gamedata/quest_data.h"
#include "gamedata/trigger_data.h"
#include "world/ownership.h"
#include "world/data_rules.h"
#include "world/persons.h"
#include "world/progress.h"
#include "world/quest_objects.h"
#include "world/quests.h"
#include "world/skills.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

using gamedata::lower_text;
using gamedata::QuestNode;
using gamedata::QuestObjective;
using gamedata::QuestTask;
using nlohmann::json;
namespace task_type = gamedata::task_type;

/** The level of a history of decision D2 (an existing character of level 60: the brief of stage 1.8e). */
constexpr std::uint32_t kTemplateLevel = 60;
/** The start territory of a new character (decision D1 of the user; Quests::new_character_rows). */
constexpr std::uint16_t kNewStartTerritory = 6;

/** @return The achievement ids of a conditional text (the names between the operators !, &&, ||, and the parentheses). */
std::vector<std::string> conditional_ids(std::string_view text) {
    std::vector<std::string> ids;
    std::string current;
    for (const char character : text) {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_' || character == '-' || character == '.'
            || character == '*') {
            current.push_back(character);
        } else if (!current.empty()) {
            ids.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) {
        ids.push_back(current);
    }
    return ids;
}

/** The rows of the developer worlds (the folders 74_devworld_red and 74_devworld_blue: research quest-engine-3.md, 77 rows). */
bool is_developer(const std::string& table) {
    return table.find("devworld") != std::string::npos;
}

/** The archetype of the default of the creation (the Soldier; Quests::new_character_rows): the primary history of the manifest. */
constexpr const char* kPrimaryArchetype = "Archetype_Soldier";

/** @return The repeat of a row (Repeatable and ScheduleId: none, hourly, daily, weekly; "no schedule" for a row with no known schedule). */
std::string repeat_text(const gamedata::QuestData& data, const QuestObjective& objective) {
    if (!objective.repeatable) {
        return "none";
    }
    switch (data.schedule_kind(objective.schedule)) {
    case gamedata::ScheduleKind::hourly:
        return "hourly";
    case gamedata::ScheduleKind::daily:
        return "daily";
    case gamedata::ScheduleKind::weekly:
        return "weekly";
    default:
        return "no schedule";
    }
}

/** @return The kind of a reason of a block (the counts of the manifest): the reason with its names of the data cut away. */
std::string reason_kind(const std::string& reason) {
    std::string text = reason;
    if (text.rfind("task ", 0) == 0) {
        const std::size_t colon = text.find(": ");
        text = colon == std::string::npos ? text : "task: " + text.substr(colon + 2);
    }
    static const std::vector<std::pair<std::string, std::string>> kinds{
        {"the engine has no evaluator for the task type ", "task: no evaluator for the type "},
        {"no trigger volume of the tag", "task: no trigger volume of its tag"},
        {"no point of interest shape", "task: no point of interest shape of its tags"},
        {"no creature of a spawn point", "task: no creature of its categories"},
        {"KillEnemyWeaponID", "task: the item of a kill (KillEnemyWeaponID)"},
        {"no producer of the game event", "task: no producer of its game event"},
        {"no object of the data has the interact tag", "task: no object with its interact tag"},
        {"the item catalog has no", "task: an item that the catalog does not have"},
        {"no supported source gives the item", "task: no source of its item"},
        {"no gatherable node of the world has", "task: no gatherable node with its item"},
        {"has no resolved person slot", "task: a person with no slot"},
        {"the conversation data has no", "task: a topic or a state that the conversation data does not have"},
        {"the crafting data has no recipe", "task: a recipe that the crafting data does not have"},
        {"has a rule with no code", "task: a recipe with a rule that the backend does not have"},
        {"is not a refining recipe", "task: a recipe that is not a refining recipe"},
        {"the quest data has no child objective", "task: a child objective that the data does not have"},
        {"the tree has no completion path", "the tree has no completion path"},
        {"rewards: the reward ", "rewards: "},
        {"rewards: the item catalog has no", "rewards: an item that the catalog does not have"},
        {"rewards: the standing", "rewards: a standing with no territory or a rank-up"},
        {"rewards: the quest data has no", "rewards: a game event or a next objective that the data does not have"},
        {"rewards: the reward item", "rewards: a loot container with no table"},
        {"rewards: a negative", "rewards: a negative quantity"},
        {"gate: RequiredLevel", "gate: RequiredLevel above the level cap"},
        {"gate: RequiredFaction", "gate: RequiredFaction needs a faction"},
        {"gate: QuestArcTag", "gate: QuestArcTag"},
        {"gate: RequiredWorldTags", "gate: RequiredWorldTags"},
        {"gate: RequiredGameModeId", "gate: RequiredGameModeId"},
        {"gate: RequiredProgression", "gate: RequiredProgression"},
    };
    for (const auto& [needle, kind] : kinds) {
        const std::size_t found = text.find(needle);
        if (found == std::string::npos) {
            continue;
        }
        if (kind == "task: no evaluator for the type ") {
            const std::size_t end = text.find(' ', found + needle.size());
            return kind + text.substr(found + needle.size(), end == std::string::npos ? std::string::npos : end - found - needle.size());
        }
        if (kind == "rewards: ") {
            // "the reward FactionReputation 50 of EVENT has no owner": the column.
            const std::size_t start = found + needle.size();
            const std::size_t end = text.find(' ', start);
            return kind + text.substr(start, end == std::string::npos ? std::string::npos : end - start) + " has no owner";
        }
        return kind;
    }
    return text;
}

} // namespace

/** The indexes of the world content that the rows name (one pass over the data at the start of a manifest). */
struct QuestSupport::Index {
    const gamedata::QuestData* quests{nullptr};
    /** The table path of each objective (the developer rows: the folders of the developer worlds). */
    std::vector<std::string> tables;
    /** The person slots of each NPC id (lower case): the binding and the object. */
    std::unordered_map<std::string, std::vector<const gamedata::InteractionObject*>> persons;
    /** The offers of each objective id (lower case): the NPC row and the index of its state. */
    std::unordered_map<std::string, std::vector<std::pair<const gamedata::ConversationNpc*, std::uint32_t>>> offers;
    /** The objectives whose ObjectiveReward is the objective, and whose TaskObjective names it (lower case id to objective indexes). */
    std::unordered_map<std::string, std::vector<std::uint32_t>> rewardParents;
    std::unordered_map<std::string, std::vector<std::uint32_t>> childParents;
    /** The backstory templates that start the objective (lower case id). */
    std::unordered_map<std::string, std::vector<std::string>> templates;
    /** The territories whose start objective it is. */
    std::unordered_map<std::string, std::vector<std::uint16_t>> territories;
    /** The vitals categories and the kill events of the creatures of the spawn points (lower case). */
    std::unordered_set<std::string> killCategories;
    std::unordered_set<std::string> killEvents;
    /** The game events of the gatherable rows with a node in the world: the kind of the node (lower case event id). */
    std::unordered_map<std::string, std::string> gatherEvents;
    /** The gatherable rows with a node in the world (lower case id). */
    std::unordered_set<std::string> gatherRows;
    /** The interact tags of the quest objects and of the bounds of the interaction data (lower case). */
    std::unordered_set<std::string> objectTags;
    /** The items that a supported source gives (a loot table, a recipe, a quest reward or give): lower case id to the first source. */
    std::unordered_map<std::string, std::string> items;
    /** The items of a gather loot root (TaskRequireGatherItems). */
    std::unordered_set<std::string> gatherItems;
    /** The achievements of the lore pages (the bits that a read gives at each time; stage 2.5a). */
    std::unordered_set<std::string> worldBits;
    /** The level cap of the progression tables (the level that kills give a new character). */
    std::uint32_t levelCap{60};
};

/** The facts of one task of a row. */
struct TaskFact {
    std::uint8_t index{0};
    bool supported{true};
    std::string reason;
    std::string supplier;
};

/** The evaluation of one objective row (no history). */
struct QuestSupport::Row {
    std::string kind;
    std::vector<TaskFact> tasks;
    /** The tree has a completion path whose leaves are each supported (containers: simple and consecutive AND, partial OR). */
    bool tree{false};
    std::vector<std::string> blocked;
    json grants = json::array();
    bool grantKind{false};
    std::vector<std::string> gates;
    bool rewards{false};
    std::string rewardDetail;
    json rewardJson = json::object();
    std::vector<std::string> unknownAchievements;
    json world = json::object();
    bool supported{false};
};

/** The history of one profile. */
struct QuestSupport::History {
    Entity scratch;
    std::set<std::uint32_t> bits;
    SkillSet skills;
    std::vector<bool> played;
    std::vector<std::uint16_t> origin;
    std::vector<std::string> order;
    std::vector<std::uint32_t> round;
    /** The last cause why an objective was not played (the primary profile reports it for each row). */
    std::vector<std::string> cause;
    std::uint32_t rounds{0};
};

QuestSupport::QuestSupport(World& world) : m_world(world) {}

QuestSupport::~QuestSupport() = default;

void QuestSupport::build() {
    m_index = std::make_unique<Index>();
    Index& index = *m_index;
    const WorldOptions& options = m_world.options();
    index.quests = m_world.quests().data();
    if (index.quests == nullptr) {
        return;
    }
    const gamedata::QuestData& data = *index.quests;
    const auto& objectives = data.objectives();
    index.tables.reserve(objectives.size());
    for (std::uint32_t at = 0; at < objectives.size(); ++at) {
        const QuestObjective& objective = objectives[at];
        index.tables.push_back(lower_text(objective.table));
        if (!objective.objectiveReward.empty()) {
            index.rewardParents[lower_text(objective.objectiveReward)].push_back(at);
        }
        for (const QuestNode& node : objective.nodes) {
            if (node.task == gamedata::kNoTask) {
                continue;
            }
            const QuestTask& task = data.task(node.task);
            if (task.type == task_type::kObjective && !task.itemName.empty()) {
                index.childParents[lower_text(task.itemName)].push_back(at);
            }
            // The items that a quest gives: an item action of a task, the item of the game event of a task.
            if ((task.type == task_type::kGiveItem || task.type == task_type::kGiveAndTakeItem) && !task.itemName.empty()) {
                index.items.emplace(lower_text(task.itemName), "the task " + task.id);
            }
            if (task.type == task_type::kTriggerGameEvent && !task.targets.empty()) {
                if (const gamedata::QuestGameEvent* event = data.game_event(task.targets.front()); event != nullptr && !event->item.empty()) {
                    index.items.emplace(lower_text(event->item), "the game event " + event->id);
                }
            }
        }
        // The reward items of a hand-in.
        if (!objective.itemReward.empty()) {
            index.items.emplace(lower_text(objective.itemReward), "the reward of " + objective.id);
        }
        if (const gamedata::QuestGameEvent* event = objective.successEvent.empty() ? nullptr : data.game_event(objective.successEvent);
            event != nullptr && !event->item.empty()) {
            index.items.emplace(lower_text(event->item), "the reward of " + objective.id);
        }
    }
    for (const gamedata::QuestBackstory& backstory : data.backstories()) {
        if (backstory.archetype) {
            continue;
        }
        for (const std::string& objective : backstory.objectives) {
            index.templates[lower_text(objective)].push_back(backstory.id);
        }
    }
    for (std::uint16_t territory = 0; territory < 512; ++territory) {
        if (const QuestObjective* start = data.territory_objective(territory)) {
            index.territories[lower_text(start->id)].push_back(territory);
        }
    }
    // The persons of the interaction data (a person slot with its binding) and the offers of the conversation data.
    if (const gamedata::InteractionData* interactions = options.interactionData; interactions != nullptr) {
        for (const gamedata::InteractionObject& object : interactions->objects()) {
            if (object.person) {
                index.persons[lower_text(object.person->npc)].push_back(&object);
            }
            for (const gamedata::InteractionBound& bound : object.bounds) {
                if (!bound.tag.empty()) {
                    index.objectTags.insert(lower_text(bound.tag));
                }
                if (bound.lore >= 0 && static_cast<std::size_t>(bound.lore) < interactions->achievements().size()) {
                    index.worldBits.insert(lower_text(interactions->achievements()[static_cast<std::size_t>(bound.lore)].id));
                }
            }
        }
    }
    if (const gamedata::ConversationData* conversations = m_world.persons().data(); conversations != nullptr) {
        for (std::uint32_t at = 0; at < conversations->npc_count(); ++at) {
            const gamedata::ConversationNpc* npc = &conversations->npc_at(at);
            for (const std::uint32_t state : npc->states) {
                for (const std::string& objective : conversations->state(state).objectives) {
                    index.offers[lower_text(objective)].emplace_back(npc, state);
                }
            }
        }
    }
    // The creatures of the spawn points: their vitals categories (the loot data, as Rewards::killed reads them) and their kill events.
    if (const gamedata::CreatureData* creatures = options.creatureData; creatures != nullptr) {
        std::vector<bool> placed(creatures->kinds().size(), false);
        for (const gamedata::SpawnPoint& point : creatures->spawn_points()) {
            if (point.kind < placed.size()) {
                placed[point.kind] = true;
            }
        }
        for (std::uint32_t at = 0; at < creatures->kinds().size(); ++at) {
            if (!placed[at]) {
                continue;
            }
            const gamedata::CreatureKind& kind = creatures->kinds()[at];
            const gamedata::LootVitals* vitals = options.lootTables != nullptr ? options.lootTables->vitals_of(kind.vitals) : nullptr;
            std::vector<std::string> categories = vitals != nullptr ? vitals->categories : std::vector<std::string>{};
            if (categories.empty()) {
                categories = {lower_text(kind.vitals), lower_text(kind.category)};
            }
            index.killCategories.insert(categories.begin(), categories.end());
            index.killEvents.insert(vitals != nullptr && !vitals->event.empty() ? vitals->event : std::string(gamedata::kKillEvent));
        }
    }
    // The gatherable rows with a node in the world: the records of the regions (natural nodes), the capital nodes and quest objects, the
    // skinning of the creature slices; their game events and their loot items.
    if (const gamedata::GatheringData* gathering = options.gatheringData; gathering != nullptr) {
        const auto add_row = [&](std::uint32_t row, const char* kind) {
            if (row >= gathering->rows().size()) {
                return;
            }
            const gamedata::GatherableRow& gatherable = gathering->rows()[row];
            index.gatherRows.insert(lower_text(gatherable.id));
            if (!gatherable.event.empty()) {
                index.gatherEvents.emplace(lower_text(gatherable.event), kind);
            }
            if (options.lootTables != nullptr && !gatherable.loot.empty()) {
                // The items of the loot root of the row (each table that it reaches).
                std::vector<std::string> pending{gatherable.loot};
                std::set<std::string> seen;
                while (!pending.empty()) {
                    const std::string id = pending.back();
                    pending.pop_back();
                    if (!seen.insert(lower_text(id)).second) {
                        continue;
                    }
                    const gamedata::LootTable* table = options.lootTables->table(id);
                    if (table == nullptr) {
                        continue;
                    }
                    for (const gamedata::LootEntry& entry : table->entries) {
                        if (entry.kind == gamedata::LootEntryKind::table) {
                            pending.push_back(entry.id);
                        } else if (entry.kind == gamedata::LootEntryKind::item) {
                            index.gatherItems.insert(lower_text(entry.id));
                        }
                    }
                }
            }
        };
        for (const gamedata::GatheringRegion& region : gathering->regions()) {
            if (region.records + region.tails == 0) {
                continue;
            }
            for (const std::int32_t row : region.rows) {
                if (row >= 0) {
                    add_row(static_cast<std::uint32_t>(row), "a natural node");
                }
            }
        }
        for (const auto& [key, link] : gathering->capitals()) {
            const bool quest = link.row < gathering->rows().size() && gathering->rows()[link.row].quest;
            add_row(link.row, quest ? "a quest object" : "a capital node");
            if (!link.interactTag.empty()) {
                index.objectTags.insert(link.interactTag);
            }
        }
        for (const auto& [slice, link] : gathering->creature_links()) {
            add_row(link.row, "the skinning of a creature");
        }
    }
    // Stage 1.8e: the destructible quest objects (game data quest-objects.json): their game events, their tags, and their rows (a quest drop
    // of a gather names the row).
    if (const gamedata::QuestObjectData* objects = m_world.quest_objects().data(); objects != nullptr) {
        for (const gamedata::QuestObjectPlacement& object : objects->objects()) {
            const gamedata::QuestObjectRow& row = objects->rows()[object.row];
            if (!row.event.empty()) {
                index.gatherEvents.emplace(lower_text(row.event), "a destructible quest object");
            }
            if (!object.tag.empty()) {
                index.objectTags.insert(object.tag);
            }
            index.gatherRows.insert(lower_text(row.id));
        }
    }
    // The items of a quest drop of a task whose source is in the world (a kill of a spawn point, a gather of a node or of an object).
    for (const QuestObjective& objective : objectives) {
        for (const QuestNode& node : objective.nodes) {
            if (node.task == gamedata::kNoTask) {
                continue;
            }
            const QuestTask& task = data.task(node.task);
            if (!gamedata::is_possession(task.type) || task.minGearScore != 0 || !(task.dropChance > 0.0) || task.itemName.empty()) {
                continue;
            }
            const bool gather = task.type == task_type::kGatherItems || task.type == task_type::kGatherAndReturnItems;
            for (const std::string& source : task.dropSources) {
                if (gather ? index.gatherRows.count(source) != 0 : index.killCategories.count(source) != 0) {
                    index.items.emplace(lower_text(task.itemName), "the quest drop of " + source);
                    break;
                }
            }
        }
    }
    // The items of a supported source: each item of a loot table (a kill, a chest, a gather), the output of a recipe that the backend crafts.
    if (options.lootTables != nullptr) {
        for (const gamedata::LootTable& table : options.lootTables->data().tables) {
            for (const gamedata::LootEntry& entry : table.entries) {
                if (entry.kind == gamedata::LootEntryKind::item) {
                    index.items.emplace(lower_text(entry.id), "the loot table " + table.id);
                }
            }
        }
    }
    if (const gamedata::CraftingData* crafting = options.craftingData; crafting != nullptr) {
        for (const gamedata::Recipe& recipe : crafting->recipes()) {
            if (recipe.unsupported.empty() && !recipe.item.empty()) {
                index.items.emplace(lower_text(recipe.item), "the recipe " + recipe.id);
            }
        }
    }
    if (options.progressionTables != nullptr && options.progressionTables->loaded()) {
        index.levelCap = options.progressionTables->level_cap();
    }
}

QuestSupport::Row QuestSupport::evaluate(std::uint32_t at) const {
    const Index& index = *m_index;
    const gamedata::QuestData& data = *index.quests;
    const QuestObjective& objective = data.objectives()[at];
    const WorldOptions& options = m_world.options();
    const std::string id = lower_text(objective.id);
    Row row;
    row.kind = is_developer(index.tables[at])     ? "developer"
               : data_rules::same_text(objective.type, "Mission") ? "missionTemplate"
               : data_rules::same_text(objective.type, "CommunityGoal") ? "communityGoal"
                                                                       : "objective";
    // The grant sources (research quest-engine-3.md Q1) and the kind of each one that the backend runs.
    const auto add_grant = [&](const std::string& source, const std::string& detail, bool supported, const std::string& reason) {
        row.grants.push_back(json{{"source", source}, {"detail", detail}, {"supported", supported}, {"reason", reason}});
        row.grantKind = row.grantKind || supported;
    };
    if (const auto found = index.territories.find(id); found != index.territories.end()) {
        for (const std::uint16_t territory : found->second) {
            const bool start = territory == kNewStartTerritory;
            add_grant("territory", std::to_string(territory), start,
                      start ? "" : "the start territory of a new character is Windsward (decision D1 of the user)");
        }
    }
    if (const auto found = index.templates.find(id); found != index.templates.end()) {
        for (const std::string& backstory : found->second) {
            add_grant("template", backstory, true, "");
        }
    }
    if (const auto found = index.offers.find(id); found != index.offers.end()) {
        for (const auto& [npc, state] : found->second) {
            const auto persons = index.persons.find(lower_text(npc->id));
            std::string reason;
            bool supported = false;
            if (persons == index.persons.end()) {
                reason = "the NPC " + npc->id + " has no person slot in the interaction data";
            } else {
                for (const gamedata::InteractionObject* object : persons->second) {
                    if (object->person->unresolved.empty()) {
                        supported = true;
                        break;
                    }
                    reason = "the person slot of " + npc->id + " is not resolved: " + object->person->unresolved;
                }
            }
            add_grant("offer", npc->id + " " + m_world.persons().data()->state(state).id, supported, supported ? "" : reason);
        }
    }
    if (const auto found = index.rewardParents.find(id); found != index.rewardParents.end()) {
        for (const std::uint32_t parent : found->second) {
            add_grant("reward", data.objectives()[parent].id, true, "");
        }
    }
    if (const auto found = index.childParents.find(id); found != index.childParents.end()) {
        for (const std::uint32_t parent : found->second) {
            add_grant("child", data.objectives()[parent].id, true, "");
        }
    }
    if (objective.pushOnRequirement) {
        add_grant("push", objective.required, false, "PushObjectiveOnReqMet is not applied (a decision of the user is open)");
    }
    if (row.kind == "missionTemplate") {
        add_grant("mission", objective.type, false, "the mission boards and the mission definitions have no contract (roadmap 4.5, 5.4)");
    } else if (row.kind == "communityGoal") {
        add_grant("communityGoal", objective.type, false, "the community goals have no contract (roadmap 5.4)");
    }
    if (row.grants.empty()) {
        add_grant("none", "", false, "the data of the backend has no source that grants the objective");
    }
    // The gates whose rule is known before a history: the gates with no service rule, the faction of an unaligned character, a level above
    // the cap.
    if (!objective.gameMode.empty()) {
        row.gates.push_back("RequiredGameModeId " + objective.gameMode + " has no rule");
    }
    if (!objective.questArc.empty()) {
        row.gates.push_back("QuestArcTag " + objective.questArc + " has no rule");
    }
    if (!objective.worldTags.empty()) {
        row.gates.push_back("RequiredWorldTags " + objective.worldTags + " has no rule");
    }
    if (!objective.requiredProgression.empty()) {
        row.gates.push_back("RequiredProgression " + objective.requiredProgression + " has no rule");
    }
    // The faction test of the eligibility (0x3A4C166) for a character with no faction: bit 0 of the mask (bit 1 is Syndicate; the second
    // computation of the counts found the test of bit 1 here).
    if ((objective.factionMask & 0x1U) == 0) {
        row.gates.push_back("RequiredFaction needs a faction (each character is unaligned: roadmap 4.5)");
    }
    if (objective.requiredLevel > index.levelCap) {
        row.gates.push_back("RequiredLevel " + std::to_string(objective.requiredLevel) + " is above the level cap " + std::to_string(index.levelCap));
    }
    // The tasks and their suppliers.
    std::vector<bool> ok(objective.nodes.size(), false);
    json triggers = json::array();
    json places = json::array();
    json personsUsed = json::array();
    json objects = json::array();
    json creatures = json::array();
    for (std::uint8_t node = 0; node < objective.nodes.size(); ++node) {
        const QuestNode& entry = objective.nodes[node];
        if (entry.task == gamedata::kNoTask) {
            continue;
        }
        const QuestTask& task = data.task(entry.task);
        TaskFact fact;
        fact.index = node;
        const auto block = [&](std::string reason) {
            fact.supported = false;
            fact.reason = std::move(reason);
        };
        const auto item_source = [&](const std::string& item) -> std::optional<std::string> {
            if (options.itemCatalog == nullptr || options.itemCatalog->find(item) == nullptr) {
                return std::nullopt;
            }
            const auto found = index.items.find(lower_text(item));
            if (found != index.items.end()) {
                return found->second;
            }
            return std::nullopt;
        };
        switch (task.type) {
        case task_type::kSimpleContainer:
        case task_type::kConsecutiveContainer:
        case task_type::kPartialContainer:
            fact.supplier = "a container";
            break;
        case task_type::kUnlockAchievement:
        case task_type::kLockAchievement:
        case task_type::kTemporaryUnlockAchievement:
            fact.supplier = "an achievement action";
            if (!m_world.ownership().achievement_bit(task.itemName)) {
                row.unknownAchievements.push_back(task.itemName);
            }
            break;
        case task_type::kAchievement:
            fact.supplier = "the achievement bits (the history decides)";
            break;
        case task_type::kTimer:
            fact.supplier = "the world clock";
            break;
        case task_type::kDie:
            fact.supplier = "the health system";
            break;
        case task_type::kOpenUiScreen:
            fact.supplier = "message 2617";
            break;
        case task_type::kCutsceneComplete:
            fact.supplier = "messages 6703, 6706, 6705 (the client asks for " + task.itemName + ")";
            break;
        case task_type::kCinematicStart:
        case task_type::kCinematicComplete:
            fact.supplier = "messages 6606, 6542 (" + task.itemName + ")";
            break;
        case task_type::kTriggerGameEvent:
            fact.supplier = "the activation (the game event " + (task.targets.empty() ? std::string{} : task.targets.front()) + ")";
            break;
        case task_type::kTriggerArea:
            triggers.push_back(task.targetTag);
            if (options.triggerData == nullptr || !options.triggerData->knows(task.targetTag)) {
                block("no trigger volume of the tag " + task.targetTag + " in the trigger data");
            } else {
                fact.supplier = "the trigger volume " + task.targetTag;
            }
            break;
        case task_type::kGoToPoi:
        case task_type::kGoToPoiOnTerritory:
        case task_type::kExitPoi: {
            std::string tags;
            for (const std::string& tag : task.poiTags) {
                places.push_back(tag);
                tags += (tags.empty() ? "" : ",") + tag;
            }
            if (options.poiData == nullptr || !options.poiData->knows(task.poiTags)) {
                block("no point of interest shape of the tags " + tags + " in the POI data");
            } else {
                fact.supplier = "the point of interest " + tags;
            }
            break;
        }
        case task_type::kKillContribution: {
            std::string found;
            for (const std::string& target : task.targets) {
                creatures.push_back(target);
                if (found.empty() && index.killCategories.count(target) != 0) {
                    found = target;
                }
            }
            if (!task.weapon.empty()) {
                block("KillEnemyWeaponID " + task.weapon + ": the backend has no item of a kill");
            } else if (found.empty()) {
                block("no creature of a spawn point has a vitals category of the task");
            } else {
                fact.supplier = "the kills of the category " + found;
            }
            break;
        }
        case task_type::kGameEvent: {
            std::string found;
            for (const std::string& target : task.targets) {
                if (!found.empty()) {
                    break;
                }
                if (index.killEvents.count(target) != 0) {
                    found = "the kill event " + target;
                } else if (const auto gather = index.gatherEvents.find(target); gather != index.gatherEvents.end()) {
                    found = gather->second + " (" + target + ")";
                }
            }
            if (found.empty()) {
                block("no producer of the game event " + (task.targets.empty() ? std::string{} : task.targets.front())
                      + " (no creature, gatherable, or destructible object of the data gives it)");
            } else {
                fact.supplier = found;
            }
            break;
        }
        case task_type::kConversationState:
        case task_type::kConversationTopic: {
            personsUsed.push_back(task.itemName);
            const auto persons = index.persons.find(lower_text(task.itemName));
            bool resolved = false;
            if (persons != index.persons.end()) {
                for (const gamedata::InteractionObject* object : persons->second) {
                    resolved = resolved || object->person->unresolved.empty();
                }
            }
            const gamedata::ConversationData* conversations = m_world.persons().data();
            const bool known = conversations != nullptr
                               && (task.type == task_type::kConversationTopic
                                       ? conversations->topic_of_crc(gamedata::quest_crc(task.screen)).has_value()
                                       : conversations->state_index(task.screen).has_value());
            if (!resolved) {
                block("the person " + task.itemName + " has no resolved person slot");
            } else if (!known) {
                block("the conversation data has no " + std::string(task.type == task_type::kConversationTopic ? "topic " : "state ") + task.screen);
            } else {
                fact.supplier = "the person " + task.itemName;
            }
            break;
        }
        case task_type::kInteract:
            objects.push_back(task.interactTag);
            if (index.objectTags.count(task.interactTag) == 0) {
                block("no object of the data has the interact tag " + task.interactTag);
            } else {
                fact.supplier = "the objects of the tag " + task.interactTag;
            }
            break;
        case task_type::kHaveItems:
        case task_type::kHaveAndReturnItems:
        case task_type::kGatherItems:
        case task_type::kGatherAndReturnItems:
        case task_type::kEquipItem: {
            if (!task.interactTag.empty()) {
                objects.push_back(task.interactTag);
            }
            const bool drop = task.type != task_type::kEquipItem && task.minGearScore == 0 && task.dropChance > 0.0;
            std::string dropSource;
            if (drop) {
                const bool gather = task.type == task_type::kGatherItems || task.type == task_type::kGatherAndReturnItems;
                for (const std::string& source : task.dropSources) {
                    if (gather ? index.gatherRows.count(source) != 0 : index.killCategories.count(source) != 0) {
                        dropSource = source;
                        break;
                    }
                }
            }
            if (options.itemCatalog == nullptr || options.itemCatalog->find(task.itemName) == nullptr) {
                block("the item catalog has no " + task.itemName);
            } else if (!dropSource.empty()) {
                fact.supplier = "the quest drop of " + dropSource;
            } else if (const std::optional<std::string> source = item_source(task.itemName)) {
                fact.supplier = *source;
            } else {
                block("no supported source gives the item " + task.itemName + " (no quest drop, loot table, recipe, or quest reward)");
            }
            break;
        }
        case task_type::kRequireGatherItems:
            if (index.gatherItems.count(lower_text(task.itemName)) == 0) {
                block("no gatherable node of the world has " + task.itemName + " in its loot");
            } else {
                fact.supplier = "a gather of " + task.itemName;
            }
            break;
        case task_type::kGiveItem:
        case task_type::kGiveAndTakeItem:
        case task_type::kRemoveItem:
            if (options.itemCatalog == nullptr || options.itemCatalog->find(task.itemName) == nullptr) {
                block("the item catalog has no " + task.itemName);
            } else {
                fact.supplier = "an item action";
            }
            break;
        case task_type::kCraftRecipe:
        case task_type::kRefineRecipe:
        case task_type::kCraftMultipleRecipes: {
            if (task.anyRecipe) {
                fact.supplier = "each recipe";
                break;
            }
            std::vector<std::string> recipes = task.type == task_type::kCraftMultipleRecipes ? task.dropSources
                                                                                             : std::vector<std::string>{task.recipe};
            std::string found;
            std::string cause = "no recipe";
            for (const std::string& recipe : recipes) {
                const gamedata::Recipe* row = options.craftingData != nullptr ? options.craftingData->recipe_of(recipe) : nullptr;
                if (row == nullptr) {
                    cause = "the crafting data has no recipe " + recipe;
                } else if (!row->unsupported.empty()) {
                    cause = "the recipe " + recipe + " has a rule with no code: " + row->unsupported.front();
                } else if (task.type == task_type::kRefineRecipe && !row->refining) {
                    cause = "the recipe " + recipe + " is not a refining recipe";
                } else {
                    found = recipe;
                    break;
                }
            }
            if (found.empty()) {
                block(cause);
            } else {
                fact.supplier = "the recipe " + found;
            }
            break;
        }
        case task_type::kObjective:
            if (data.objective(task.itemName) == nullptr) {
                block("the quest data has no child objective " + task.itemName);
            } else {
                fact.supplier = "the child objective " + task.itemName;
            }
            break;
        default:
            block("the engine has no evaluator for the task type " + (task.typeName.empty() ? std::to_string(task.type) : task.typeName)
                  + " (a later stage)");
            break;
        }
        if (!fact.supported) {
            row.blocked.push_back("task " + std::to_string(node) + " " + task.id + ": " + fact.reason);
        }
        row.tasks.push_back(std::move(fact));
    }
    // The completion path: the children are before their container (the index rule), so one pass in index order.
    std::vector<bool> leafOk(objective.nodes.size(), false);
    for (const TaskFact& fact : row.tasks) {
        leafOk[fact.index] = fact.supported;
    }
    for (std::uint8_t node = 0; node < objective.nodes.size(); ++node) {
        const QuestNode& entry = objective.nodes[node];
        if (entry.task == gamedata::kNoTask) {
            ok[node] = false;
            continue;
        }
        const QuestTask& task = data.task(entry.task);
        if (!gamedata::is_container(task.type)) {
            ok[node] = leafOk[node];
            continue;
        }
        if (task.type == task_type::kPartialContainer) {
            ok[node] = entry.children.empty() || std::any_of(entry.children.begin(), entry.children.end(), [&](std::uint8_t child) { return ok[child]; });
        } else {
            ok[node] = std::all_of(entry.children.begin(), entry.children.end(), [&](std::uint8_t child) { return ok[child]; });
        }
    }
    row.tree = !objective.nodes.empty() && ok[objective.root];
    if (!row.tree && row.blocked.empty()) {
        row.blocked.push_back("the tree has no completion path");
    }
    // The rewards with an empty history (the standing of a history: the closure checks it again).
    RewardJoin join;
    std::string detail;
    const QuestError reward = m_world.quests().reward_join(SkillSet{}, objective, objective.exclusiveTerritory.value_or(kNewStartTerritory), join, detail);
    row.rewards = reward == QuestError::none;
    row.rewardDetail = detail;
    json items = json::array();
    for (const RewardItem& item : join.items) {
        items.push_back(item.itemId + " x" + std::to_string(item.quantity));
    }
    row.rewardJson = json{{"payable", row.rewards},
                          {"refusal", detail},
                          {"experience", join.eventExperience + join.rowExperience},
                          {"coin", join.coin},
                          {"items", items},
                          {"standing", join.standing},
                          {"azoth", join.azoth},
                          {"next", join.next != nullptr ? join.next->id : ""},
                          {"repeat", repeat_text(data, objective)}};
    if (!row.rewards) {
        row.blocked.push_back("rewards: " + detail);
    }
    if (!row.grantKind) {
        row.blocked.push_back("grant: no source of a kind that the backend runs");
    }
    for (const std::string& gate : row.gates) {
        row.blocked.push_back("gate: " + gate);
    }
    row.world = json{{"triggerTags", triggers}, {"poiTags", places}, {"persons", personsUsed}, {"objectTags", objects}, {"killTargets", creatures}};
    row.supported = row.kind == "objective" && row.tree && row.rewards && row.grantKind && row.gates.empty();
    return row;
}

std::vector<SupportProfile> QuestSupport::default_profiles() const {
    std::vector<SupportProfile> profiles;
    const gamedata::QuestData* data = m_world.quests().data();
    if (data == nullptr) {
        return profiles;
    }
    std::uint32_t cap = 60;
    if (m_world.options().progressionTables != nullptr && m_world.options().progressionTables->loaded()) {
        cap = m_world.options().progressionTables->level_cap();
    }
    const QuestObjective* start = data->territory_objective(kNewStartTerritory);
    // The archetype rows, the default of the creation first (the primary history of the manifest).
    std::vector<const gamedata::QuestBackstory*> archetypes;
    for (const gamedata::QuestBackstory& backstory : data->backstories()) {
        if (backstory.archetype) {
            archetypes.push_back(&backstory);
        }
    }
    std::stable_partition(archetypes.begin(), archetypes.end(),
                          [](const gamedata::QuestBackstory* row) { return lower_text(row->id) == lower_text(kPrimaryArchetype); });
    for (const gamedata::QuestBackstory* row : archetypes) {
        const gamedata::QuestBackstory& backstory = *row;
        SupportProfile profile;
        profile.id = "new:" + backstory.id;
        profile.group = "new character";
        profile.achievements = backstory.achievements;
        if (start != nullptr) {
            profile.starts.push_back(start->id);
        }
        profile.startTerritory = kNewStartTerritory;
        profile.level = cap;
        profiles.push_back(std::move(profile));
    }
    SupportProfile none;
    none.id = "level60:none";
    none.group = "no template (decision D2 option A)";
    none.level = kTemplateLevel;
    profiles.push_back(std::move(none));
    for (const gamedata::QuestBackstory& backstory : data->backstories()) {
        if (backstory.archetype) {
            continue;
        }
        SupportProfile profile;
        profile.id = "template:" + backstory.id;
        profile.group = backstory.objectives.empty() ? "a template with no start objective" : backstory.objectives.front();
        profile.achievements = backstory.achievements;
        profile.starts = backstory.objectives;
        profile.level = kTemplateLevel;
        profiles.push_back(std::move(profile));
    }
    return profiles;
}

void QuestSupport::play(const SupportProfile& profile, const std::vector<Row>& facts, History& history) const {
    const Index& index = *m_index;
    const gamedata::QuestData& data = *index.quests;
    const auto& objectives = data.objectives();
    const Ownership& ownership = m_world.ownership();
    const Quests& quests = m_world.quests();
    Persons& persons = m_world.persons();
    const gamedata::ConversationData* conversations = persons.data();
    history.played.assign(objectives.size(), false);
    history.origin.assign(objectives.size(), 0);
    history.round.assign(objectives.size(), 0);
    history.cause.assign(objectives.size(), std::string{});
    history.scratch = Entity{};
    history.scratch.kind = EntityKind::player;
    history.scratch.characterId = "support-manifest";
    history.scratch.quests.loaded = true;
    history.scratch.progression.level = profile.level;
    for (const std::string& achievement : profile.achievements) {
        if (const std::optional<std::uint32_t> bit = ownership.achievement_bit(achievement)) {
            history.bits.insert(*bit);
        }
    }
    const auto sync = [&]() { history.scratch.achievements.assign(history.bits.begin(), history.bits.end()); };
    std::set<std::string> starts;
    for (const std::string& start : profile.starts) {
        starts.insert(lower_text(start));
    }
    const auto set_bit = [&](std::set<std::uint32_t>& bits, const std::string& id, bool on) {
        if (const std::optional<std::uint32_t> bit = ownership.achievement_bit(id)) {
            if (on) {
                bits.insert(*bit);
            } else {
                bits.erase(*bit);
            }
        }
    };
    // Plays the tree of one objective on a copy of the bits: the achievement actions in index order, the achievement waits (with a lore page
    // that the player reads then), the child objectives, and the completion. @return The cause of a stop, or an empty text.
    std::function<std::string(std::uint32_t, std::uint16_t, std::set<std::uint32_t>&, SkillSet&, std::vector<std::uint32_t>&, int)> run;
    run = [&](std::uint32_t at, std::uint16_t origin, std::set<std::uint32_t>& bits, SkillSet& skills, std::vector<std::uint32_t>& done,
              int depth) -> std::string {
        const QuestObjective& objective = objectives[at];
        const Row& fact = facts[at];
        if (depth > 8) {
            return "the child objectives are too deep";
        }
        if (fact.kind != "objective" || !fact.tree || !fact.rewards || !fact.gates.empty()) {
            return fact.blocked.empty() ? "not supported" : fact.blocked.front();
        }
        std::vector<std::string> temporary;
        // The rules of the engine (Quests::Plan::activate): a consecutive container activates its first child that is not done, a simple
        // or a partial container each child; an achievement action runs at its activation and completes; a partial container completes
        // with its first done child. The player does each supported task of the world (a place, a kill, a gather, a cutscene, a timer) when
        // it is Active; an achievement wait completes when its conditional is true (with a lore page that the player reads then).
        enum class Node : std::uint8_t { inactive, active, done };
        std::vector<Node> state(objective.nodes.size(), Node::inactive);
        std::vector<bool> supported(objective.nodes.size(), true);
        for (const TaskFact& item : fact.tasks) {
            supported[item.index] = item.supported;
        }
        std::string stop;
        std::function<void(std::uint8_t)> activate;
        std::function<void(std::uint8_t)> complete;
        const auto parent_of = [&](std::uint8_t node) { return objective.nodes[node].parent; };
        complete = [&](std::uint8_t node) {
            if (state[node] == Node::done) {
                return;
            }
            state[node] = Node::done;
            const std::optional<std::uint8_t> parent = parent_of(node);
            if (!parent || state[*parent] != Node::active) {
                return;
            }
            const QuestNode& entry = objective.nodes[*parent];
            const QuestTask& task = data.task(entry.task);
            if (task.type == task_type::kPartialContainer) {
                for (const std::uint8_t child : entry.children) {
                    if (state[child] == Node::active) {
                        state[child] = Node::inactive; // The unfinished children of a partial container end (no StayActive rule here).
                    }
                }
                complete(*parent);
            } else if (task.type == task_type::kConsecutiveContainer) {
                for (const std::uint8_t child : entry.children) {
                    if (state[child] != Node::done) {
                        activate(child);
                        return;
                    }
                }
                complete(*parent);
            } else if (std::all_of(entry.children.begin(), entry.children.end(), [&](std::uint8_t child) { return state[child] == Node::done; })) {
                complete(*parent);
            }
        };
        activate = [&](std::uint8_t node) {
            const QuestNode& entry = objective.nodes[node];
            if (entry.task == gamedata::kNoTask || state[node] != Node::inactive) {
                return;
            }
            state[node] = Node::active;
            const QuestTask& task = data.task(entry.task);
            switch (task.type) {
            case task_type::kConsecutiveContainer:
                for (const std::uint8_t child : entry.children) {
                    if (state[child] != Node::done) {
                        activate(child);
                        return;
                    }
                }
                complete(node);
                return;
            case task_type::kSimpleContainer:
            case task_type::kPartialContainer:
                if (entry.children.empty()) {
                    complete(node);
                    return;
                }
                for (const std::uint8_t child : entry.children) {
                    if (state[node] != Node::active) {
                        return;
                    }
                    activate(child);
                }
                return;
            case task_type::kUnlockAchievement:
                set_bit(bits, task.itemName, true);
                complete(node);
                return;
            case task_type::kLockAchievement:
                set_bit(bits, task.itemName, false);
                complete(node);
                return;
            case task_type::kTemporaryUnlockAchievement:
                set_bit(bits, task.itemName, true);
                temporary.push_back(task.itemName);
                complete(node);
                return;
            case task_type::kTriggerGameEvent:
                if (!task.targets.empty()) {
                    if (const gamedata::QuestWhisper* whisper = data.whisper_of_event(task.targets.front());
                        whisper != nullptr && !whisper->achievement.empty()) {
                        set_bit(bits, whisper->achievement, true);
                    }
                    if (const gamedata::QuestGameEvent* event = data.game_event(task.targets.front());
                        event != nullptr && !event->achievement.empty()) {
                        set_bit(bits, event->achievement, true);
                    }
                }
                complete(node);
                return;
            case task_type::kGiveItem:
            case task_type::kGiveAndTakeItem:
            case task_type::kRemoveItem:
            case task_type::kDie:
                if (supported[node]) {
                    complete(node);
                }
                return;
            case task_type::kTimer:
                if (task.failOnCondition) {
                    complete(node); // A failure timer is satisfied while it runs (Quests::Plan::activate).
                }
                return;
            case task_type::kObjective: {
                const QuestObjective* child = data.objective(task.itemName);
                if (child == nullptr) {
                    return;
                }
                const auto childAt = static_cast<std::uint32_t>(child - objectives.data());
                if (history.played[childAt] || std::find(done.begin(), done.end(), childAt) != done.end()) {
                    return; // A child that the history completed: no second instance; the task waits (Quests::Plan::grant_child).
                }
                if (const std::string cause = run(childAt, origin, bits, skills, done, depth + 1); !cause.empty()) {
                    stop = "the child objective " + child->id + ": " + cause;
                    return;
                }
                complete(node);
                return;
            }
            default:
                return; // A wait: the steps below.
            }
        };
        activate(objective.root);
        // The steps of the player: each Active wait in index order, until the root is done or a step changes nothing.
        for (std::size_t step = 0; step < objective.nodes.size() * 4 + 8 && state[objective.root] != Node::done && stop.empty(); ++step) {
            bool progress = false;
            for (std::uint8_t node = 0; node < objective.nodes.size() && state[objective.root] != Node::done; ++node) {
                const QuestNode& entry = objective.nodes[node];
                if (state[node] != Node::active || entry.task == gamedata::kNoTask) {
                    continue;
                }
                const QuestTask& task = data.task(entry.task);
                if (gamedata::is_container(task.type)) {
                    continue;
                }
                if (task.type == task_type::kAchievement) {
                    history.scratch.achievements.assign(bits.begin(), bits.end());
                    if (quests.conditional(history.scratch, task.itemName)) {
                        complete(node);
                        progress = true;
                        continue;
                    }
                    // A lore page that the player reads now (stage 2.5a: the read gives its bit at each time).
                    std::set<std::uint32_t> withPages = bits;
                    bool page = false;
                    for (const std::string& name : conditional_ids(task.itemName)) {
                        if (index.worldBits.count(lower_text(name)) != 0) {
                            set_bit(withPages, name, true);
                            page = true;
                        }
                    }
                    history.scratch.achievements.assign(withPages.begin(), withPages.end());
                    if (page && quests.conditional(history.scratch, task.itemName)) {
                        bits = std::move(withPages);
                        complete(node);
                        progress = true;
                    }
                    continue;
                }
                if (supported[node] && task.type != task_type::kObjective) {
                    if (task.type == task_type::kConversationTopic && conversations != nullptr) {
                        if (const std::optional<std::uint32_t> topic = conversations->topic_of_crc(gamedata::quest_crc(task.screen));
                            topic && !conversations->topic(*topic).achievementUnlock.empty()) {
                            set_bit(bits, conversations->topic(*topic).achievementUnlock, true);
                        }
                    }
                    complete(node);
                    progress = true;
                }
            }
            if (!progress) {
                break;
            }
        }
        if (!stop.empty()) {
            return stop;
        }
        if (state[objective.root] != Node::done) {
            // The first Active task that waits: the cause of the stop.
            for (std::uint8_t node = 0; node < objective.nodes.size(); ++node) {
                const QuestNode& entry = objective.nodes[node];
                if (state[node] == Node::active && entry.task != gamedata::kNoTask && !gamedata::is_container(data.task(entry.task).type)) {
                    const QuestTask& task = data.task(entry.task);
                    const auto reason = std::find_if(fact.tasks.begin(), fact.tasks.end(), [&](const TaskFact& item) { return item.index == node; });
                    return "task " + std::to_string(node) + " " + task.id + " waits: "
                           + (task.type == task_type::kAchievement ? "the achievement condition " + task.itemName + " is false in this history"
                              : reason != fact.tasks.end() && !reason->reason.empty() ? reason->reason
                                                                                      : std::string("no step completes it"));
                }
            }
            return "the root task does not complete";
        }
        // The hand-in: the reward join with the standing of the history, then the completion achievements; the temporary unlocks revert.
        RewardJoin join;
        std::string detail;
        if (quests.reward_join(skills, objective, origin, join, detail) != QuestError::none) {
            return "rewards: " + detail;
        }
        if (join.standing > 0) {
            Skills::Plan plan;
            plan.skills = skills;
            if (m_world.skills().add_standing(plan, join.standingTerritory, join.standing) == ProgressError::none) {
                skills = plan.skills;
            }
        }
        for (const std::string& name : temporary) {
            set_bit(bits, name, false);
        }
        for (const std::string& name : join.eventAchievements) {
            set_bit(bits, name, true);
        }
        if (!join.achievement.empty()) {
            set_bit(bits, join.achievement, true);
        }
        done.push_back(at);
        return std::string{};
    };
    // The closure in rounds (data order in each round).
    std::vector<bool> startGiven(objectives.size(), false);
    for (std::uint32_t roundNumber = 1; roundNumber <= objectives.size() + 1; ++roundNumber) {
        bool changed = false;
        for (std::uint32_t at = 0; at < objectives.size(); ++at) {
            if (history.played[at]) {
                continue;
            }
            const QuestObjective& objective = objectives[at];
            const Row& fact = facts[at];
            if (fact.kind != "objective") {
                history.cause[at] = fact.blocked.empty() ? fact.kind : fact.blocked.front();
                continue;
            }
            const std::string id = lower_text(objective.id);
            // The sources that grant the objective now, with the origin territory of the instance.
            std::optional<std::uint16_t> origin;
            bool checkAvailability = true;
            if (starts.count(id) != 0 && !startGiven[at]) {
                origin = profile.startTerritory;
                checkAvailability = false; // A one-time grant (Quests::grant): no availability check.
            }
            if (!origin) {
                if (const auto found = index.rewardParents.find(id); found != index.rewardParents.end()) {
                    for (const std::uint32_t parent : found->second) {
                        if (history.played[parent]) {
                            origin = history.origin[parent];
                            checkAvailability = false; // The hand-in grants ObjectiveReward in its plan.
                            break;
                        }
                    }
                }
            }
            if (!origin) {
                if (const auto found = index.offers.find(id); found != index.offers.end() && conversations != nullptr) {
                    sync();
                    for (const auto& [npc, state] : found->second) {
                        const auto slots = index.persons.find(lower_text(npc->id));
                        if (slots == index.persons.end()) {
                            continue;
                        }
                        const bool present = std::any_of(slots->second.begin(), slots->second.end(), [&](const gamedata::InteractionObject* object) {
                            return object->person->unresolved.empty() && persons.present(history.scratch, *object->person);
                        });
                        if (present && persons.selected_state(history.scratch, *npc) == static_cast<std::int32_t>(state)) {
                            origin = objective.exclusiveTerritory.value_or(0);
                            break;
                        }
                    }
                }
            }
            if (!origin) {
                if (history.cause[at].empty()) {
                    history.cause[at] = "no source grants it in this history"; // A refusal of an earlier round stays the cause.
                }
                continue;
            }
            if (checkAvailability) {
                sync();
                if (const QuestError error = quests.available(history.scratch, objective); error != QuestError::none) {
                    history.cause[at] = std::string("the offer is not available: ") + quest_error_text(error);
                    continue;
                }
            }
            std::set<std::uint32_t> bits = history.bits;
            SkillSet skills = history.skills;
            std::vector<std::uint32_t> done;
            if (const std::string cause = run(at, *origin, bits, skills, done, 0); !cause.empty()) {
                history.cause[at] = cause;
                continue;
            }
            history.bits = std::move(bits);
            history.skills = std::move(skills);
            startGiven[at] = true;
            for (const std::uint32_t item : done) {
                history.played[item] = true;
                history.origin[item] = *origin;
                history.round[item] = roundNumber;
                history.order.push_back(objectives[item].id);
                history.cause[item].clear();
            }
            changed = true;
        }
        history.rounds = roundNumber;
        if (!changed) {
            break;
        }
    }
    sync();
}

json QuestSupport::manifest(const std::vector<SupportProfile>& profiles) {
    build();
    json document{{"format", 1},
                  {"rules", "docs/systems/quests.md, section The support manifest (stage 1.8e)"},
                  {"objectives", json::array()},
                  {"missionTemplates", json::array()},
                  {"profiles", json::array()}};
    if (m_index->quests == nullptr) {
        document["counts"] = json{{"rows", 0}};
        return document;
    }
    const gamedata::QuestData& data = *m_index->quests;
    const auto& objectives = data.objectives();
    std::vector<Row> rows;
    rows.reserve(objectives.size());
    std::map<std::string, std::uint64_t> kinds;
    std::map<std::string, std::uint64_t> firstReasons;
    std::uint64_t supported = 0;
    for (std::uint32_t at = 0; at < objectives.size(); ++at) {
        rows.push_back(evaluate(at));
        const Row& row = rows.back();
        ++kinds[row.kind];
        if (row.supported) {
            ++supported;
        } else if (row.kind == "objective") {
            ++firstReasons[reason_kind(row.blocked.empty() ? "not supported" : row.blocked.front())];
        }
    }
    // The histories.
    std::map<std::string, std::pair<std::uint64_t, std::uint64_t>> groups;
    std::map<std::string, std::uint64_t> groupSizes;
    std::vector<History> histories(profiles.size());
    for (std::size_t at = 0; at < profiles.size(); ++at) {
        play(profiles[at], rows, histories[at]);
        const std::uint64_t played = static_cast<std::uint64_t>(std::count(histories[at].played.begin(), histories[at].played.end(), true));
        auto [entry, inserted] = groups.emplace(profiles[at].group, std::make_pair(played, played));
        if (!inserted) {
            entry->second.first = std::min(entry->second.first, played);
            entry->second.second = std::max(entry->second.second, played);
        }
        ++groupSizes[profiles[at].group];
        document["profiles"].push_back(json{{"id", profiles[at].id},
                                            {"group", profiles[at].group},
                                            {"level", profiles[at].level},
                                            {"starts", profiles[at].starts},
                                            {"completable", played},
                                            {"rounds", histories[at].rounds},
                                            {"played", histories[at].order}});
    }
    // The primary history (the first new character: the creation default) gives the cause of each row.
    const History* primary = histories.empty() ? nullptr : &histories.front();
    for (std::uint32_t at = 0; at < objectives.size(); ++at) {
        const QuestObjective& objective = objectives[at];
        const Row& row = rows[at];
        json tasks = json::array();
        for (const TaskFact& fact : row.tasks) {
            const QuestTask& task = data.task(objective.nodes[fact.index].task);
            tasks.push_back(json{{"index", fact.index},
                                 {"id", task.id},
                                 {"type", task.typeName},
                                 {"supported", fact.supported},
                                 {"supplier", fact.supplier},
                                 {"reason", fact.reason}});
        }
        json entry{{"id", objective.id},
                   {"type", objective.type},
                   {"kind", row.kind},
                   {"grants", row.grants},
                   {"gates", json{{"required", objective.required},
                                  {"requiredLevel", objective.requiredLevel},
                                  {"factionMask", objective.factionMask},
                                  {"blocked", row.gates}}},
                   {"tasks", tasks},
                   {"effects", json{{"unknownAchievements", row.unknownAchievements}}},
                   {"world", row.world},
                   {"rewards", row.rewardJson},
                   {"supported", row.supported},
                   {"blocked", row.blocked}};
        if (primary != nullptr) {
            entry["primary"] = primary->played[at] ? json{{"played", true}, {"round", primary->round[at]}}
                                                   : json{{"played", false}, {"cause", primary->cause[at]}};
        }
        document[row.kind == "missionTemplate" ? "missionTemplates" : "objectives"].push_back(std::move(entry));
    }
    json groupCounts = json::array();
    for (const auto& [group, range] : groups) {
        groupCounts.push_back(json{{"group", group}, {"profiles", groupSizes[group]}, {"completableMin", range.first}, {"completableMax", range.second}});
    }
    json reasons = json::object();
    for (const auto& [reason, count] : firstReasons) {
        reasons[reason] = count;
    }
    document["counts"] = json{{"rows", objectives.size()},
                              {"kinds", kinds},
                              {"supported", supported},
                              {"firstReasonOfTheOthers", reasons},
                              {"generatedMissions", "the mission definitions (MissionData) are not in the data of the backend: no row"},
                              {"levelCap", m_index->levelCap},
                              {"groups", groupCounts},
                              {"primaryProfile", profiles.empty() ? std::string{} : profiles.front().id},
                              {"primaryCompletable", primary == nullptr ? 0 : std::count(primary->played.begin(), primary->played.end(), true)}};
    return document;
}

} // namespace soulwarden::backend::world

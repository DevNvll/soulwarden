#include "world/quest_support.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "gamedata/chest_data.h"
#include "gamedata/conversation_data.h"
#include "gamedata/crafting_data.h"
#include "gamedata/creature_data.h"
#include "gamedata/gathering_data.h"
#include "gamedata/interaction_data.h"
#include "gamedata/item_catalog.h"
#include "gamedata/mission_data.h"
#include "gamedata/loot_tables.h"
#include "gamedata/poi_data.h"
#include "gamedata/progression_tables.h"
#include "gamedata/quest_data.h"
#include "gamedata/trigger_data.h"
#include "world/ownership.h"
#include "world/data_rules.h"
#include "world/inventory.h"
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

/**
 * Stage QG2a (research quest-gaps.md section 2): the never-true trigger tags of the data (the spellings of the build: Quest_16006,
 * Quest_16008, and the other Dunwood rows): a task with such a tag never completes; no volume has it.
 */
bool never_true_tag(const std::string& tag) {
    std::string key;
    for (const char character : tag) {
        if (character != ' ') {
            key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
        }
    }
    return key == "nevertrue" || key == "nevetrue" || key == "nveretrue" || key == "nevercomplete";
}

/**
 * Stage QG3: @return The causes of the data for the tags with no shape (" (TAG: CAUSE; ...)"; the member `unshaped` of the POI tool), or
 *         an empty text (no data, or a file with no causes).
 */
std::string poi_causes(const gamedata::PoiData* data, const std::vector<std::string>& tags) {
    if (data == nullptr || !data->loaded()) {
        return {};
    }
    std::string text;
    for (const std::string& tag : tags) {
        const std::string cause = data->cause(tag);
        if (!cause.empty()) {
            text += (text.empty() ? " (" : "; ") + tag + ": " + cause;
        }
    }
    return text.empty() ? text : text + ")";
}

/**
 * Code review of stage QG2a, finding QG2R-R2: @return True for the task types whose runtime path applies the location rule
 * (EnforceLocationRequirement): the possession and the equip predicates (Plan::location_ok) and the counters of an event
 * (Quests::matched_tasks: the kills, the game events, the gathers, the crafts, the refines, the screens, the interactions, the cutscenes, the
 * cinematics, the states of a person). TaskAchievement, TaskTriggerArea, TaskHaveLevel, and TaskConversationTopic (its progress is direct,
 * Quests::topics_finished) do not test it; a place task tests its own shape.
 */
bool enforces_location(std::uint8_t type) {
    switch (type) {
    case task_type::kEquipItem:
    case task_type::kKillContribution:
    case task_type::kGameEvent:
    case task_type::kRequireGatherItems:
    case task_type::kCraftRecipe:
    case task_type::kCraftMultipleRecipes:
    case task_type::kRefineRecipe:
    case task_type::kOpenUiScreen:
    case task_type::kInteract:
    case task_type::kCutsceneComplete:
    case task_type::kCinematicStart:
    case task_type::kCinematicComplete:
    case task_type::kConversationState:
        return true;
    default:
        return gamedata::is_possession(type);
    }
}

/** Stage QG7: @return The text with no spaces at its two ends. */
std::string trimmed_copy(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t");
    return first == std::string::npos ? std::string{} : text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

/** @return True for a blank cell text. */
bool trimmed_blank(const std::string& text) {
    return text.find_first_not_of(" \t") == std::string::npos;
}

/**
 * Stage QG2a (research quest-gaps.md section 8, "The 66 missing item rows"): @return The reason of a task with no ItemName: the item of a
 * mission or community goal template comes from its mission instance (the MissionData overrides: roadmap 4.5, 5.4), not from the catalog.
 */
std::string blank_item(const std::string& kind) {
    return kind == "missionTemplate" || kind == "communityGoal"
               ? "the task has no ItemName: the mission instance of a template gives it (the mission boards and the community goals: roadmap "
                 "4.5, 5.4)"
               : "the task has no ItemName (a blank cell of the data)";
}

/**
 * Stage QG5a: the trade skills of a gather (their ranks rise with the gathers: the manifest takes their rank gates as reachable). Stage QG5b: a
 * rank gate of another trade skill (AzothStaff) needs the rank in the history (the categorical progression reward of its hand-ins).
 */
constexpr std::array<const char*, 4> kGatheringSkills{"Mining", "Logging", "Harvesting", "Skinning"};

/** Stage QG2a: @return True for a KillEnemyWeaponID that names an ability table of a weapon (BowAbilityTable): a filter, not an item. */
bool weapon_filter(const std::string& weapon) {
    const std::string key = lower_text(weapon);
    return key.size() > 12 && key.compare(key.size() - 12, 12, "abilitytable") == 0;
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
        // Stage QG9a: the game modes that the backend does not run (one class for each cause).
        {"TaskGameMode with GameModeMustBeMutated", "task: TaskGameMode with GameModeMustBeMutated (no mutation context)"},
        {"TaskGameMode of the mode ", "task: TaskGameMode of a mode that the backend does not run"},
        {"the task counts only in the game mode ", "task: a task of a game mode that the backend does not run"},
        {"the location rule (EnforceLocationRequirement)", "task: the location rule with no point of interest shape (EnforceLocationRequirement)"},
        {"no trigger volume of the tag", "task: no trigger volume of its tag"},
        {"no point of interest shape", "task: no point of interest shape of its tags"},
        {"no creature of a spawn point", "task: no creature of its categories"},
        {"is a weapon filter", "task: a weapon filter of a kill (KillEnemyWeaponID: an ability table)"},
        {"names no item and no ability table", "task: KillEnemyWeaponID names no item and no ability table"},
        {"KillEnemyWeaponID", "task: the item of a kill (KillEnemyWeaponID)"},
        {"the task has no TP_TargetTag", "task: a pin or a puzzle task with no trigger tag (never completes)"},
        {"not in the open world (it belongs to its instance stage)", "task: a trigger volume of an instance (its instance stage)"},
        {"the task has no POITag: the mission instance", "task: a place of a template (the mission instance gives it)"},
        {"the task has no POITag: a pin task", "task: a pin task with no POITag (never completes)"},
        {"the task has no POITag: a blank cell", "task: a place task with no POITag (never completes)"},
        {"is a never-true tag of the data", "task: a never-true trigger tag of the data (a sentinel)"},
        {"the task names no game event", "task: a pin or a map task with no game event (never completes)"},
        {"the mission instance of a template gives it", "task: a blank item of a template (the mission instance gives it)"},
        {"the task has no ItemName", "task: a blank item (no ItemName)"},
        {"TaskHaveLevel of the categorical progression", "task: TaskHaveLevel of a faction progression (no owner)"},
        {"TaskHaveLevel with the TargetQty", "task: TaskHaveLevel that never completes (TargetQty below 1)"},
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
        {"has a reward with no owner in the open (", "rewards: a loot container whose salvage event pays "},
        {"has no row in the loot data", "rewards: a loot container whose salvage event has no row"},
        {"rewards: the reward item", "rewards: a loot container with no table and no salvage event"},
        {"rewards: the reward ", "rewards: "},
        {"rewards: the item catalog has no", "rewards: an item that the catalog does not have"},
        {"rewards: the standing", "rewards: a standing with no territory"},
        {"rewards: the quest data has no", "rewards: a game event or a next objective that the data does not have"},
        {"rewards: a negative", "rewards: a negative quantity"},
        {"gate: RequiredLevel", "gate: RequiredLevel above the level cap"},
        {"gate: RequiredFaction", "gate: RequiredFaction needs a faction"},
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
        if (kind == "rewards: a loot container whose salvage event pays ") {
            // "... has a reward with no owner in the open (EntitlementId X)": the column.
            const std::size_t start = found + needle.size();
            const std::size_t end = text.find_first_of(" )", start);
            return kind + text.substr(start, end == std::string::npos ? std::string::npos : end - start);
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

/**
 * Stage QG4: the cause of an NPC with no person slot of the open world (the member personsApart of the interaction data): a short group
 * (the first reasons of the counts) and the detail (the cause, the world, the slice). No person apart: the data has no placement of it.
 */
std::pair<std::string, std::string> apart_cause(const gamedata::InteractionData* interactions, const std::string& npc) {
    const std::vector<const gamedata::PersonApart*> apart = interactions != nullptr ? interactions->persons_apart(npc) : std::vector<const gamedata::PersonApart*>{};
    if (apart.empty()) {
        return {"a person with no placement in the data", "the data has no placement of the NPC " + npc + " in any world (no spawn is made for it)"};
    }
    // The order of the groups: an instance first (a level script of a dungeon, a dungeon world), then the other causes.
    static const std::vector<std::string> kOrder{"levelScript", "otherWorld", "event", "nested", "alias", "idTaken"};
    const gamedata::PersonApart* first = apart.front();
    for (const std::string& cause : kOrder) {
        const auto found = std::find_if(apart.begin(), apart.end(), [&](const gamedata::PersonApart* person) { return person->cause == cause; });
        if (found != apart.end()) {
            first = *found;
            break;
        }
    }
    std::string group;
    if (first->cause == "otherWorld" && first->world.find("ftue") != std::string::npos) {
        group = "a person of the old introduction world";
    } else if (first->cause == "otherWorld" && first->world.find("season") != std::string::npos) {
        group = "a person of a seasonal world";
    } else if (first->cause == "otherWorld" || first->cause == "levelScript") {
        group = "a person of an instance";
    } else if (first->cause == "event") {
        group = "a person of an event spawn (no owner)";
    } else if (first->cause == "nested") {
        group = "a person below a nested spawner (not proved)";
    } else if (first->cause == "alias") {
        group = "a person of an alias choice";
    } else {
        group = "a person whose client id another slot has";
    }
    return {group, "the data has the person apart: " + first->cause + " in the world " + first->world + " (" + first->slice + ")"};
}

/**
 * Stage QG4: @return The item level of an artifact (the gear score of its item row: GearScoreOverride, else MinGearScore) by the rule of
 * ProgressionTables::item_level, or 0 when the item catalog or the progression tables have no value.
 */
std::uint32_t artifact_level(const WorldOptions& options, const std::string& itemId) {
    const gamedata::ItemDefinition* definition = options.itemCatalog != nullptr ? options.itemCatalog->find(itemId) : nullptr;
    if (definition == nullptr || options.progressionTables == nullptr || !options.progressionTables->loaded()) {
        return 0;
    }
    const std::uint32_t gearScore = definition->gearScoreOverride != 0 ? definition->gearScoreOverride : definition->minGearScore;
    return gearScore == 0 ? 0 : options.progressionTables->item_level(gearScore);
}

} // namespace

/** The indexes of the world content that the rows name (one pass over the data at the start of a manifest). */
/**
 * Stage QG6a (docs/systems/mission-boards.md): one mission that a town board of the interaction data gives: its binding (the effective
 * objective), the key of the board, and the territory of the board.
 */
struct QuestSupport::BoardMission {
    const gamedata::MissionBinding* binding{nullptr};
    std::string board;
    std::uint16_t territory{0};
};

struct QuestSupport::Index {
    const gamedata::QuestData* quests{nullptr};
    /**
     * Stage QG6a: the missions that the town boards of the data give, by the CRC-32 of their template: a town row of an always available
     * project of the board town (the project rule of 0x3A362A0) whose territory rule passes for the territory of the board (0x39D3EC0), with
     * a binding of the quest data. The first board of the data that gives a mission keeps it. The level, the trade skill, and the time window
     * of the offer are the gates of a history: no row has a level gate of the template.
     */
    std::unordered_map<std::uint32_t, std::vector<BoardMission>> boardMissions;
    std::size_t boardCount{0};
    /** Stage QG6b: the faction boards of the data (the representatives with an ObjectiveProvider of a faction). */
    std::size_t factionBoardCount{0};
    /**
     * Stage QG7: the rank-up rows of the validation lists of the base attributes (entry K + 1 of RankupObjectiveIds; lower case): the
     * faction system grants each one after the trial achievement K (Quests::next_rank_up).
     */
    std::unordered_set<std::string> rankUpRows;
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
    /** Stage QG4: the artifacts whose equip grants the objective (lower case objective id; OnEquipObjectiveId of the item catalog). */
    std::unordered_map<std::string, std::vector<const gamedata::ArtifactObjective*>> equips;
    /** The vitals categories and the kill events of the creatures of the spawn points (lower case). */
    std::unordered_set<std::string> killCategories;
    std::unordered_set<std::string> killEvents;
    /**
     * Stage QG9a: the modes that the backend runs (lower case GameModeId: an entrance of the interaction data and the content of the
     * encounter system, Encounters::runs_mode), and the kill categories of the members of the instance recipes of each such mode.
     */
    std::unordered_set<std::string> modes;
    std::unordered_map<std::string, std::unordered_set<std::string>> modeKillCategories;
    /** Stage QG9b: the reason of the setting of each mode of an entrance that the setting turns off (encounter_rules::mode_off). */
    std::unordered_map<std::string, std::string> modesOff;
    /** Stage QG2a: the categories of the quest drops of a kill (the rule of the kill with no id of the base row). */
    std::unordered_set<std::string> dropCategories;
    /** Stage QG6b: the drop categories of the placed creatures whose body can be skinned (a gather drop of a body matches them). */
    std::unordered_set<std::string> skinCategories;
    /**
     * The producers of the game events of a gather or of an encounter (lower case event id): each with its kind, the rank gate of a trade
     * skill that only a hand-in gives (stage QG5b: AzothStaff; empty: none), and the item of its restriction ItemRequired (empty: none).
     * Code review of stage QG5c2, findings QG5C2R-R5 and R6: the gates of one producer stay together; a task takes one producer as a whole
     * (the start object of an encounter with the gates of its gathering row; no minimum rank of one row with the free item of an other).
     */
    struct GatherSource {
        std::string kind;
        std::string rankSkill;
        std::uint32_t rankLevel{0};
        std::string item;
    };
    std::unordered_map<std::string, std::vector<GatherSource>> gatherSources;
    /** The gatherable rows with a node in the world (lower case id). */
    std::unordered_set<std::string> gatherRows;
    /**
     * Stage QG8: the rows of the quest objects of the world (a gatherable row of the quest form that the backend gathers, a destructible or a
     * proximity quest object): a possession task (TaskHaveItems, TaskHaveAndReturnItems) whose ItemDropVC names one drops at its gather
     * (Quests::plan_drops, questObject).
     */
    std::unordered_set<std::string> questObjectRows;
    /**
     * Stage QG5a: the cause of a game event whose only gatherable rows of the world the backend does not gather (a refusal cause of the row,
     * or a rank of a trade skill with no source: AzothStaff), for the block of its task (lower case event id).
     */
    std::unordered_map<std::string, std::string> refusedEvents;
    /**
     * Stage QG5a: the tags of the points of interest of the ordinary chests (each tag, and each tag with the territory of its point of interest),
     * for the quest drops of a chest (ChestDropProbability; Quests::plan_chest_drops); `anyChest` with no tag.
     */
    std::unordered_set<std::string> chestTags;
    std::set<std::pair<std::string, std::uint16_t>> chestTagTerritories;
    bool anyChest{false};
    /** The interact tags of the quest objects and of the bounds of the interaction data (lower case). */
    std::unordered_set<std::string> objectTags;
    /**
     * Stage 1.8f (code review of stage 1.8e, finding QR8E-R5): the items that a source of the world gives in each history (lower case id to
     * the first source): the loot roots of the creatures of the spawn points, of the chests, and of the gatherable nodes of the world, the
     * loot containers of these items, and the recipes whose ingredients these items give.
     */
    std::unordered_map<std::string, std::string> items;
    /**
     * The items that only a quest gives (lower case id to the first source): the reward of a hand-in, a give or the game event of a task, a
     * quest drop. The history decides: a reward after the hand-in of its objective, the other ones only in the tree of their objective.
     */
    std::unordered_map<std::string, std::string> questItems;
    /** The items that the tasks of each objective give in its own tree (a give, the item of the game event of a task, a quest drop). */
    std::vector<std::vector<std::string>> gives;
    /** The items of a gather loot root (TaskRequireGatherItems). */
    std::unordered_set<std::string> gatherItems;
    /**
     * Stage QG8: the declared sources of an item that no supported source gives (the reason of its task in the manifest): the loot tables
     * that have the item (an entry, or a candidate of a bucket of the table), the tables that name each table, the roots of the loot data
     * above a table (the loot of a vitals row, a loot container item, a chest root), and the recipes of each output item.
     */
    std::unordered_map<std::string, std::vector<std::uint32_t>> itemTables;
    std::vector<std::vector<std::uint32_t>> tableParents;
    std::unordered_map<std::uint32_t, std::vector<std::string>> tableRoots;
    std::unordered_map<std::string, std::vector<std::uint32_t>> itemRecipes;
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
    /** Stage 1.8f (finding QR8E-R5): the item (lower case) of a task that only a quest gives: the history must have it. Empty: none. */
    std::string questItem;
    /**
     * Stage QG8: the class of the first declared source of an item that no supported source gives ("task drop", "chest drop", "loot table",
     * "recipe", "none"; the reason has each source with its cause). Empty: the item has a source, or the task has no item.
     */
    std::string itemSource;
    /**
     * Stage QG5b: the rank of a trade skill that the producer of the game event of the task needs (a gatherable row with a rank gate of a
     * skill that only a hand-in gives: AzothStaff): the history must have it. An empty skill: none.
     */
    std::string rankSkill;
    std::uint32_t rankLevel{0};
    /**
     * Code review of stage QG5c3, finding QG5C3R-R4: the other producers of the event of the task that a history can use, each with its own
     * gates (an item that only a quest gives, a rank of a trade skill with no source); a history takes the task when the gates of the
     * first producer or of one of these pass.
     */
    struct Alternative {
        std::string questItem;
        std::string rankSkill;
        std::uint32_t rankLevel{0};
    };
    std::vector<Alternative> alternatives;
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
    /** Stage 1.8f (finding QR8E-R5): the items of the history (lower case): the start items, the reward items of its hand-ins. */
    std::set<std::string> items;
    std::vector<bool> played;
    std::vector<std::uint16_t> origin;
    std::vector<std::string> order;
    std::vector<std::uint32_t> round;
    /** The last cause why an objective was not played (the primary profile reports it for each row). */
    std::vector<std::string> cause;
    std::uint32_t rounds{0};
};

namespace {

/**
 * Stage QG8: @return The items (lower case) that a loot table reaches: its item entries, the candidates of its buckets, and the same of the
 * tables that it names (each table one time). No data or no table: none.
 */
std::vector<std::string> table_items(const gamedata::LootTables* tables, const std::string& root) {
    std::vector<std::string> items;
    const gamedata::LootTable* first = tables != nullptr && tables->loaded() ? tables->table(root) : nullptr;
    if (first == nullptr) {
        return items;
    }
    const gamedata::LootData& loot = tables->data();
    std::vector<std::size_t> pending{static_cast<std::size_t>(first - loot.tables.data())};
    std::unordered_set<std::size_t> seen;
    while (!pending.empty()) {
        const std::size_t at = pending.back();
        pending.pop_back();
        if (at >= loot.tables.size() || !seen.insert(at).second) {
            continue;
        }
        for (const gamedata::LootEntry& entry : loot.tables[at].entries) {
            if (entry.kind == gamedata::LootEntryKind::table) {
                pending.push_back(entry.index);
            } else if (entry.kind == gamedata::LootEntryKind::bucket && entry.index < loot.buckets.size()) {
                for (const gamedata::BucketCandidate& candidate : loot.buckets[entry.index].candidates) {
                    items.push_back(lower_text(candidate.item));
                }
            } else if (entry.kind == gamedata::LootEntryKind::item) {
                items.push_back(lower_text(entry.id));
            }
        }
    }
    return items;
}

/**
 * Stage QG5a: @return True when an ordinary chest of the world can give the quest drop of a task (ChestDropProbability; the rule of
 * Quests::plan_chest_drops): a chest in a point of interest of a tag of the task (of its TerritoryID when it has one); a task with no tag:
 * each chest.
 */
bool chest_drop(bool anyChest, const std::unordered_set<std::string>& tags, const std::set<std::pair<std::string, std::uint16_t>>& territories,
                const gamedata::QuestTask& task) {
    if (task.poiTags.empty()) {
        return anyChest;
    }
    return std::any_of(task.poiTags.begin(), task.poiTags.end(), [&](const std::string& tag) {
        return task.territory == 0 ? tags.count(tag) != 0 : territories.count({tag, task.territory}) != 0;
    });
}

} // namespace

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
    // Stage QG9a: the modes that the backend runs: an entrance of the interaction data (the kind dungeonEntrance, its m_gameMode) and the
    // content of the encounter system (Encounters::runs_mode: each instance recipe of the mode that is not a daily alternative is taken).
    if (const gamedata::InteractionData* interactions = m_world.interactions().data()) {
        for (const gamedata::InteractionObject& object : interactions->objects()) {
            if (object.entrance && m_world.encounters().runs_mode(object.entrance->mode)) {
                index.modes.insert(lower_text(object.entrance->mode));
            }
            if (object.entrance) {
                if (const std::string_view off = encounter_rules::mode_off(object.entrance->mode, options.questMedusaRuns); !off.empty()) {
                    index.modesOff[lower_text(object.entrance->mode)] = std::string(off);
                }
            }
        }
    }
    index.gives.assign(objectives.size(), {});
    if (const gamedata::ProgressionTables* tables = options.progressionTables; tables != nullptr && tables->loaded()) {
        for (const gamedata::FactionRow& faction : tables->factions()) {
            for (std::size_t at = 1; at < faction.rankUps.size(); ++at) {
                index.rankUpRows.insert(lower_text(faction.rankUps[at]));
            }
        }
    }
    // Stage QG6a: the missions of the town boards of the data.
    if (const gamedata::MissionData* missions = m_world.missions().data();
        missions != nullptr && options.interactionData != nullptr && options.interactionData->loaded()) {
        std::set<std::uint32_t> seen;
        // The boards that the client makes (level objects of the kind missionBoard: interaction data format 5).
        for (const std::uint32_t board : options.interactionData->boards()) {
            const gamedata::InteractionObject& child = options.interactionData->objects()[board];
            if (!child.provider || !child.provider->communityGoal) {
                continue;
            }
            ++index.boardCount;
            const std::uint16_t territory = options.poiData != nullptr && options.poiData->loaded()
                                                ? options.poiData->territory_at(child.position.x, child.position.y)
                                                : 0;
            const std::vector<std::uint32_t> projects = m_world.missions().town_projects(territory);
            std::set<std::uint32_t> allowed;
            for (const std::uint32_t project : projects) {
                const gamedata::MissionProject* active = missions->project(project);
                for (const gamedata::MissionProject& other : missions->projects()) {
                    if (active != nullptr && other.type == active->type && other.level <= active->level) {
                        allowed.insert(other.crc);
                    }
                }
            }
            for (const gamedata::TownBucket& bucket : missions->town_buckets()) {
                for (const std::uint32_t id : bucket.missions) {
                    const gamedata::MissionRow* row = missions->mission(id);
                    if (row == nullptr || !row->enabled || allowed.count(row->projectCrc) == 0 || seen.count(id) != 0) {
                        continue;
                    }
                    if (!row->territories.empty()
                        && (std::find(row->territories.begin(), row->territories.end(), territory) != row->territories.end()) == row->excludeTerritories) {
                        continue;
                    }
                    const gamedata::MissionBinding* binding = data.mission(id);
                    if (binding == nullptr || binding->templateObjective == nullptr) {
                        continue;
                    }
                    seen.insert(id);
                    index.boardMissions[binding->templateObjective->crc].push_back(BoardMission{binding, child.key, territory});
                }
            }
        }
        // Stage QG6b: the faction boards of the data (the persons with an ObjectiveProvider of a faction: the representatives). A board gives
        // each world row of its faction that is enabled, not PvP, not an outpost destination, and whose territory rule passes its territory
        // (the offer rule of Missions::faction_candidate; the level, the trade skill, the achievement conditional, and the window of the offer
        // are the gates of a history).
        for (std::size_t at = 0; at < options.interactionData->objects().size(); ++at) {
            const gamedata::InteractionObject& person = options.interactionData->objects()[at];
            if (person.kind == gamedata::InteractionKind::missionBoard || !person.provider || person.provider->communityGoal) {
                continue;
            }
            std::uint8_t faction = 0;
            for (const std::string& type : person.provider->missionTypes) {
                const std::uint8_t mask = gamedata::faction_mask(type);
                faction = mask == 0x2 ? 1 : mask == 0x4 ? 2 : mask == 0x8 ? 3 : faction;
            }
            if (faction == 0) {
                continue;
            }
            ++index.factionBoardCount;
            const std::uint16_t territory = options.poiData != nullptr && options.poiData->loaded()
                                                ? options.poiData->territory_at(person.position.x, person.position.y)
                                                : 0;
            for (const gamedata::MissionRow& row : missions->missions()) {
                if (row.table != gamedata::MissionTable::world || !row.enabled || row.pvp || row.outpostDestination || seen.count(row.crc) != 0
                    || (gamedata::faction_mask(row.requiredFaction) & (1U << faction)) == 0) {
                    continue;
                }
                if (!row.territories.empty()
                    && (std::find(row.territories.begin(), row.territories.end(), territory) != row.territories.end()) == row.excludeTerritories) {
                    continue;
                }
                const gamedata::MissionBinding* binding = data.mission(row.crc);
                if (binding == nullptr || binding->templateObjective == nullptr) {
                    continue;
                }
                seen.insert(row.crc);
                index.boardMissions[binding->templateObjective->crc].push_back(BoardMission{binding, person.key, territory});
            }
        }
    }
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
            // The items that a quest gives in its own tree: an item action of a task, the item of the game event of a task.
            if ((task.type == task_type::kGiveItem || task.type == task_type::kGiveAndTakeItem) && !task.itemName.empty()) {
                index.gives[at].push_back(lower_text(task.itemName));
                index.questItems.emplace(lower_text(task.itemName), "the task " + task.id);
            }
            if (task.type == task_type::kTriggerGameEvent && !task.targets.empty()) {
                // Code review of stage QG8, findings QG8R-R4 and QG8R-R5: an item of the event only for a quantity that the world gives (above
                // 0; for a loot table not above kRewardTableRolls: Quests::Plan::apply_event refuses a larger one).
                const gamedata::QuestGameEvent* event = data.game_event(task.targets.front());
                const bool table = event != nullptr && !gamedata::reward_table(event->item).empty();
                if (event != nullptr && !event->item.empty() && event->itemQty > 0
                    && (!table || event->itemQty <= static_cast<std::int64_t>(kRewardTableRolls))) {
                    // Stage QG8: an ItemReward "[LTID]NAME" gives the items of its loot table (Quests::Plan::apply_event): each item that
                    // the table reaches (the rule of the world items: the tags and the conditions are not tested). A table that the loot
                    // data does not have gives nothing.
                    if (const std::string table = gamedata::reward_table(event->item); !table.empty()) {
                        for (const std::string& item : table_items(options.lootTables, table)) {
                            index.gives[at].push_back(item);
                            index.questItems.emplace(item, "the loot table " + table + " of the game event " + event->id);
                        }
                    } else {
                        index.gives[at].push_back(lower_text(event->item));
                        index.questItems.emplace(lower_text(event->item), "the game event " + event->id);
                    }
                }
            }
        }
        // The reward items of a hand-in (the history has them after the hand-in).
        if (!objective.itemReward.empty()) {
            index.questItems.emplace(lower_text(objective.itemReward), "the reward of " + objective.id);
        }
        if (const gamedata::QuestGameEvent* event = objective.successEvent.empty() ? nullptr : data.game_event(objective.successEvent);
            event != nullptr && !event->item.empty()) {
            index.questItems.emplace(lower_text(event->item), "the reward of " + objective.id);
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
    // Stage QG4: the artifact grants of an equip (the item catalog).
    if (const gamedata::ItemCatalog* catalog = options.itemCatalog; catalog != nullptr) {
        for (const gamedata::ArtifactObjective& artifact : catalog->artifact_objectives()) {
            index.equips[lower_text(artifact.objectiveId)].push_back(&artifact);
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
    // The loot roots of the sources of the world (finding QR8E-R5): the tables that a kill, the open of a chest, or a gather rolls.
    std::vector<std::pair<std::string, std::string>> roots;
    // The creatures of the spawn points: their vitals categories (the loot data, as Rewards::killed reads them) and their kill events.
    if (const gamedata::CreatureData* creatures = options.creatureData; creatures != nullptr) {
        std::vector<bool> placed(creatures->kinds().size(), false);
        for (const gamedata::SpawnPoint& point : creatures->spawn_points()) {
            if (point.kind < placed.size()) {
                placed[point.kind] = true;
            }
        }
        // Stage QG5c2: the creature members of the encounter recipes that the runner takes (world/encounters.h) are sources too. Stage
        // QG9a: an instance recipe only for a mode that the backend runs (its entrance and its content), and its kinds are the kill sources
        // of the tasks of that mode too.
        std::vector<std::string> modeOf(creatures->kinds().size());
        for (const EncounterPlacement& placement : m_world.encounters().loaded_placements()) {
            if (!placement.refused.empty()) {
                continue;
            }
            const std::string mode = lower_text(placement.recipe->mode);
            if (!mode.empty() && index.modes.count(mode) == 0) {
                continue;
            }
            for (const gamedata::CreatureKind* kind : placement.kinds) {
                if (kind != nullptr && !mode.empty()) {
                    modeOf[static_cast<std::size_t>(kind - creatures->kinds().data())] = mode;
                }
            }
            for (const gamedata::CreatureKind* kind : placement.kinds) {
                if (kind != nullptr) {
                    placed[static_cast<std::size_t>(kind - creatures->kinds().data())] = true;
                }
            }
            // Code review of stage QG5c5, finding QG5C5R-R1: each resolved part of a group member is a creature of the world too (the runner
            // spawns one entity for each part kind; the group entry of `kinds` is null).
            for (const auto& parts : placement.partKinds) {
                for (const gamedata::CreatureKind* kind : parts) {
                    if (kind != nullptr) {
                        placed[static_cast<std::size_t>(kind - creatures->kinds().data())] = true;
                    }
                }
            }
        }
        for (std::uint32_t at = 0; at < creatures->kinds().size(); ++at) {
            if (!placed[at]) {
                continue;
            }
            const gamedata::CreatureKind& kind = creatures->kinds()[at];
            const gamedata::LootVitals* vitals = options.lootTables != nullptr ? options.lootTables->vitals_of(kind.vitals) : nullptr;
            // Stage QG2a: the rule of the kill credit and of the quest drops (LootTables::kill_categories: the base row, the spawn category,
            // the direct parent of each; the kill also the id of the base row).
            const std::vector<std::string> categories = options.lootTables != nullptr
                                                            ? options.lootTables->kill_categories(vitals, kind.vitals, kind.category, true)
                                                            : std::vector<std::string>{lower_text(kind.vitals), lower_text(kind.category)};
            const std::vector<std::string> drops = options.lootTables != nullptr
                                                       ? options.lootTables->kill_categories(vitals, kind.vitals, kind.category, false)
                                                       : std::vector<std::string>{lower_text(kind.vitals), lower_text(kind.category)};
            index.killCategories.insert(categories.begin(), categories.end());
            if (!modeOf[at].empty()) {
                index.modeKillCategories[modeOf[at]].insert(categories.begin(), categories.end());
            }
            index.dropCategories.insert(drops.begin(), drops.end());
            // Stage QG6b (GATHERDROP.md): the gather of a skinned body matches a category of its creature too (Gathering::complete).
            // Code review of stage QG6b, finding QG6BR-R6: only a skinning row that the backend gathers (the rule of add_row below: no
            // refusal cause, no rank gate of a trade skill that is not a gathering skill; Gathering::check_gates).
            const bool skinnable = options.gatheringData != nullptr
                                   && std::any_of(kind.bodies.begin(), kind.bodies.end(), [&](const gamedata::CreatureBody& body) {
                                          const gamedata::CreatureSlice& slice = creatures->slices()[body.slice];
                                          const std::optional<gamedata::GatheringLink> link = options.gatheringData->creature(slice.asset, slice.subId);
                                          if (!link || link->row >= options.gatheringData->rows().size()) {
                                              return false;
                                          }
                                          const gamedata::GatherableRow& skin = options.gatheringData->rows()[link->row];
                                          return skin.unsupported.empty()
                                                 && !(skin.level > 0 && !skin.tradeskill.empty()
                                                      && std::find(kGatheringSkills.begin(), kGatheringSkills.end(), skin.tradeskill)
                                                             == kGatheringSkills.end());
                                      });
            if (skinnable) {
                index.skinCategories.insert(drops.begin(), drops.end());
            }
            index.killEvents.insert(vitals != nullptr && !vitals->event.empty() ? vitals->event : std::string(gamedata::kKillEvent));
            if (vitals != nullptr && vitals->canDropLoot && vitals->table && *vitals->table < options.lootTables->data().tables.size()) {
                roots.emplace_back(options.lootTables->data().tables[*vitals->table].id, "the loot of the creature " + kind.vitals);
            }
        }
    }
    // The gatherable rows with a node in the world: the records of the regions (natural nodes), the capital nodes and quest objects, the
    // skinning of the creature slices; their game events and their loot items.
    const gamedata::GatheringData* gathering = options.gatheringData;
    // The item of the restriction ItemRequired of a gatherable row (stage QG5b: Gathering::check_gates: the bag needs it; 0 needs nothing).
    const auto item_of = [](const gamedata::GatherableRow& gatherable) {
        return gatherable.restriction == gamedata::kRestrictionItemRequired && !gatherable.item.empty() && gatherable.itemCount > 0
                   ? lower_text(gatherable.item)
                   : std::string{};
    };
    const auto add_row = [&](std::uint32_t row, const std::string& kind) {
        if (gathering == nullptr || row >= gathering->rows().size()) {
            return;
        }
        const gamedata::GatherableRow& gatherable = gathering->rows()[row];
        // Stage QG5a: only a row that the backend gathers is a source (Gathering::begin): no refusal cause of the row, and a rank gate of
        // a trade skill whose ranks a history can reach. The gathering trade skills rise with the gathers [agent: the manifest does not
        // model the ranks of the gathering skills]. Stage QG5b: the ranks of another trade skill (AzothStaff of the glyph locks) come from
        // the categorical progression reward of a hand-in: the history must have the rank (TaskFact::rankSkill).
        std::string cause;
        if (!gatherable.unsupported.empty()) {
            cause = "its gatherable row " + gatherable.id + " is refused (" + gatherable.unsupported.front() + ")";
        } else if (gatherable.level > 0 && !gatherable.tradeskill.empty()
                   && std::find(kGatheringSkills.begin(), kGatheringSkills.end(), gatherable.tradeskill) == kGatheringSkills.end()) {
            if (!gatherable.event.empty()) {
                // Code review of stage QG5b, finding QG5BR-R3: a ranked row keeps its ItemRequired item too (the runtime checks both
                // gates: 1625_0701_AncientCodePillar needs the rank 5 of AzothStaff and one 1625_0601_EyeOfHorus). Findings QG5C1R-R3
                // and QG5C2R-R6: each ranked row is its own alternative with its own rank and item.
                index.gatherSources[lower_text(gatherable.event)].push_back(
                    Index::GatherSource{kind, gatherable.tradeskill, gatherable.level, item_of(gatherable)});
            }
            return;
        }
        if (!cause.empty()) {
            if (!gatherable.event.empty()) {
                index.refusedEvents.emplace(lower_text(gatherable.event), cause);
            }
            return;
        }
        index.gatherRows.insert(lower_text(gatherable.id));
        if (gatherable.quest) {
            index.questObjectRows.insert(lower_text(gatherable.id));
        }
        if (!gatherable.event.empty()) {
            index.gatherSources[lower_text(gatherable.event)].push_back(Index::GatherSource{kind, "", 0, item_of(gatherable)});
        }
        if (options.lootTables != nullptr && !gatherable.loot.empty()) {
            roots.emplace_back(gatherable.loot, "the loot of " + kind + " (" + gatherable.id + ")");
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
    if (gathering != nullptr) {
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
    // Stage QG5c2: the encounter recipes that the runner takes (world/encounters.h): the success event of each one (its participants get it)
    // and the game event of its start object (its gatherer gets it). A refused recipe gives its cause to the events that only it gives.
    // Stage QG5c3: the event of a new participant (m_participantAddedGameEventId) and the event of a proximity object (each character that
    // finds it: no gate but its quest tag) are free; finding QG5C2R-R5: the event of a gather start object has the gates of its gathering row.
    if (const gamedata::EncounterData* encounters = m_world.encounters().data(); encounters != nullptr) {
        for (const EncounterPlacement& placement : m_world.encounters().loaded_placements()) {
            const gamedata::EncounterRecipe& recipe = *placement.recipe;
            // Stage QG9a: an instance recipe produces its events only in an instance of a mode that the backend runs.
            if (!recipe.mode.empty() && index.modes.count(lower_text(recipe.mode)) == 0) {
                continue;
            }
            std::vector<std::pair<std::string, std::string>> events;
            std::vector<std::pair<std::uint32_t, std::string>> rows;
            if (!recipe.successEvent.empty()) {
                events.emplace_back(lower_text(recipe.successEvent), "the success of the encounter " + recipe.rootName);
            }
            if (!recipe.participantEvent.empty()) {
                events.emplace_back(lower_text(recipe.participantEvent), "a participant of the encounter " + recipe.rootName);
            }
            for (const gamedata::EncounterStage& stage : recipe.stages) {
                for (const gamedata::EncounterSpawn& spawn : stage.spawns) {
                    const gamedata::EncounterMember* member = encounters->member(spawn.member);
                    if (member == nullptr || member->kind != gamedata::EncounterMemberKind::object || member->event.empty()) {
                        continue;
                    }
                    const std::optional<gamedata::GatheringLink> link =
                        gathering != nullptr ? gathering->encounter_object(member->slice) : std::nullopt;
                    if (member->proximity) {
                        events.emplace_back(lower_text(member->event), "the proximity object of the encounter " + recipe.rootName);
                    } else if (placement.refused.empty() && link) {
                        rows.emplace_back(link->row, "the start object of the encounter " + recipe.rootName);
                    } else {
                        events.emplace_back(lower_text(member->event), "the start object of the encounter " + recipe.rootName);
                    }
                }
            }
            for (const auto& [event, source] : events) {
                if (placement.refused.empty()) {
                    index.gatherSources[event].push_back(Index::GatherSource{source, "", 0, ""});
                } else {
                    index.refusedEvents.emplace(event, "the encounter " + recipe.rootName + " is refused: " + placement.refused.front());
                }
            }
            for (const auto& [row, source] : rows) {
                add_row(row, source);
            }
        }
        // Stage QG9d: the quest objects of the mode roots of the modes that the backend runs (the heartgems of QuestCaerDun: the instance
        // system makes each one while a member has the task of its event, Instances::look_quest_objects): their gathering rows.
        const gamedata::InteractionData* catalog = m_world.interactions().data();
        for (const gamedata::InstanceObject& object : encounters->instance_objects()) {
            const std::optional<std::uint32_t> templateIndex = catalog != nullptr ? catalog->instance_object(object.slice) : std::nullopt;
            const std::optional<gamedata::GatheringLink> link = gathering != nullptr ? gathering->encounter_object(object.slice) : std::nullopt;
            if (templateIndex && link && catalog->templates()[*templateIndex].kind == gamedata::InteractionKind::gathering
                && index.modes.count(lower_text(object.mode)) != 0) {
                add_row(link->row, "the quest object " + object.spawner + " of the mode " + object.mode);
            }
        }
    }
    // Stage 1.8e: the destructible quest objects (game data quest-objects.json): their game events, their tags, and their rows (a quest drop
    // of a gather names the row).
    if (const gamedata::QuestObjectData* objects = m_world.quest_objects().data(); objects != nullptr) {
        for (const gamedata::QuestObjectPlacement& object : objects->objects()) {
            const gamedata::QuestObjectRow& row = objects->rows()[object.row];
            if (!row.event.empty()) {
                // Stage QG5a: a proximity object (the trails and the torches) gives its event when its character is in its sphere.
                index.gatherSources[lower_text(row.event)].push_back(
                    Index::GatherSource{object.proximity ? "a proximity quest object" : "a destructible quest object", "", 0, ""});
            }
            if (!object.tag.empty()) {
                index.objectTags.insert(object.tag);
            }
            index.gatherRows.insert(lower_text(row.id));
            index.questObjectRows.insert(lower_text(row.id));
        }
    }
    // Stage QG5a: the points of interest of the ordinary chests of the chest data (the quest drops of a chest: a chest in a point of interest
    // of the tags of the task, of its TerritoryID; a task with no tag takes each chest).
    if (options.chestData != nullptr) {
        for (const gamedata::ChestDefinition& chest : options.chestData->chests()) {
            if (!chest.available) {
                continue;
            }
            index.anyChest = true;
            for (const gamedata::Poi* poi : options.poiData != nullptr ? options.poiData->at(chest.position.x, chest.position.y)
                                                                         : std::vector<const gamedata::Poi*>{}) {
                for (const std::string& tag : poi->tags) {
                    index.chestTags.insert(tag);
                    index.chestTagTerritories.emplace(tag, poi->territory);
                }
            }
        }
    }
    // The items of a quest drop of a task whose source is in the world (a kill of a spawn point, a gather of a node or of an object): an item
    // of the tree of its objective (finding QR8E-R5: the drop needs its task, which only that objective has).
    for (std::uint32_t at = 0; at < objectives.size(); ++at) {
        const QuestObjective& objective = objectives[at];
        for (const QuestNode& node : objective.nodes) {
            if (node.task == gamedata::kNoTask) {
                continue;
            }
            const QuestTask& task = data.task(node.task);
            if (!gamedata::is_possession(task.type) || task.minGearScore != 0 || task.itemName.empty()) {
                continue;
            }
            if (task.chestChance > 0.0 && chest_drop(index.anyChest, index.chestTags, index.chestTagTerritories, task)) {
                index.gives[at].push_back(lower_text(task.itemName));
                index.questItems.emplace(lower_text(task.itemName), "the quest drop of a chest");
            }
            if (!(task.dropChance > 0.0)) {
                continue;
            }
            const bool gather = task.type == task_type::kGatherItems || task.type == task_type::kGatherAndReturnItems;
            for (const std::string& source : task.dropSources) {
                // Stage QG8: a possession task also drops at the gather of a quest object of its ItemDropVC (Quests::plan_drops).
                if (gather ? index.gatherRows.count(source) != 0 || index.skinCategories.count(source) != 0
                           : index.dropCategories.count(source) != 0 || index.questObjectRows.count(source) != 0) {
                    index.gives[at].push_back(lower_text(task.itemName));
                    index.questItems.emplace(lower_text(task.itemName), "the quest drop of " + source);
                    break;
                }
            }
        }
    }
    // The items of the world in each history (finding QR8E-R5): the items that the loot roots of the sources of the world reach (the tables,
    // the buckets), then a fixed point of the loot containers of these items (the loot table of a container item) and of the recipes that the
    // backend crafts whose ingredients these items give. A table, a container, or a recipe that no source reaches gives nothing, and a cycle
    // with no first item gives nothing. Rules of this project: the loot tags and the conditions of an entry are not tested (each entry of a
    // reached table counts), and a currency ingredient passes (the coin and the Azoth have sources in each history).
    if (options.lootTables != nullptr) {
        const gamedata::LootData& loot = options.lootTables->data();
        std::vector<bool> walked(loot.tables.size(), false);
        std::vector<bool> bucketsWalked(loot.buckets.size(), false);
        const auto walk = [&](const std::string& rootId, const std::string& source) {
            const gamedata::LootTable* root = options.lootTables->table(rootId);
            if (root == nullptr) {
                return;
            }
            std::vector<std::size_t> pending{static_cast<std::size_t>(root - loot.tables.data())};
            while (!pending.empty()) {
                const std::size_t table = pending.back();
                pending.pop_back();
                if (table >= loot.tables.size() || walked[table]) {
                    continue;
                }
                walked[table] = true;
                for (const gamedata::LootEntry& entry : loot.tables[table].entries) {
                    if (entry.kind == gamedata::LootEntryKind::table) {
                        pending.push_back(entry.index);
                    } else if (entry.kind == gamedata::LootEntryKind::bucket) {
                        if (entry.index < loot.buckets.size() && !bucketsWalked[entry.index]) {
                            bucketsWalked[entry.index] = true;
                            for (const gamedata::BucketCandidate& candidate : loot.buckets[entry.index].candidates) {
                                index.items.emplace(lower_text(candidate.item), source);
                            }
                        }
                    } else {
                        index.items.emplace(lower_text(entry.id), source);
                    }
                }
            }
        };
        if (const gamedata::ChestData* chests = options.chestData; chests != nullptr) {
            for (const gamedata::ChestDefinition& chest : chests->chests()) {
                if (chest.available && !chest.root.empty()) {
                    roots.emplace_back(chest.root, "the loot of the chest " + chest.root);
                }
            }
        }
        for (const auto& [root, source] : roots) {
            walk(root, source);
        }
        const gamedata::CraftingData* crafting = options.craftingData;
        std::vector<bool> crafted(crafting != nullptr ? crafting->recipes().size() : 0, false);
        std::unordered_set<std::string> opened;
        for (bool changed = true; changed;) {
            changed = false;
            for (const auto& [item, root] : loot.containerRoots) {
                // Code review of stage QG2a, finding QG2R-R3: a container that the runtime refuses to open (a salvage event with a reward
                // that has no owner) supplies none of its items.
                if (opened.count(item) == 0 && index.items.count(item) != 0 && m_world.item_containers().openable(item)) {
                    opened.insert(item);
                    walk(root, "the loot container " + item);
                    changed = true;
                }
            }
            for (std::size_t at = 0; at < crafted.size(); ++at) {
                const gamedata::Recipe& recipe = crafting->recipes()[at];
                if (crafted[at] || !recipe.unsupported.empty() || recipe.item.empty()) {
                    continue;
                }
                const bool inputs = std::all_of(recipe.ingredients.begin(), recipe.ingredients.end(), [&](const gamedata::RecipeIngredient& ingredient) {
                    switch (ingredient.type) {
                    case gamedata::IngredientType::item:
                        return index.items.count(lower_text(ingredient.id)) != 0;
                    case gamedata::IngredientType::categoryAny:
                    case gamedata::IngredientType::categoryOnly: {
                        const auto* members = crafting->members(ingredient.id);
                        return members != nullptr && std::any_of(members->begin(), members->end(), [&](const auto& member) {
                                   return index.items.count(lower_text(member.second)) != 0;
                               });
                    }
                    case gamedata::IngredientType::currency:
                        return true;
                    case gamedata::IngredientType::invalid:
                        return false;
                    }
                    return false;
                });
                if (inputs) {
                    crafted[at] = true;
                    changed = index.items.emplace(lower_text(recipe.item), "the recipe " + recipe.id).second || changed;
                }
            }
        }
    }
    // Stage QG8: the declared loot and recipe sources of the items (the reason of an item with no source; no rule of a source).
    if (options.lootTables != nullptr && options.lootTables->loaded()) {
        const gamedata::LootData& loot = options.lootTables->data();
        index.tableParents.assign(loot.tables.size(), {});
        std::vector<std::vector<std::uint32_t>> bucketTables(loot.buckets.size());
        for (std::uint32_t table = 0; table < loot.tables.size(); ++table) {
            for (const gamedata::LootEntry& entry : loot.tables[table].entries) {
                if (entry.kind == gamedata::LootEntryKind::table && entry.index < loot.tables.size()) {
                    index.tableParents[entry.index].push_back(table);
                } else if (entry.kind == gamedata::LootEntryKind::bucket && entry.index < loot.buckets.size()) {
                    bucketTables[entry.index].push_back(table);
                } else if (entry.kind == gamedata::LootEntryKind::item) {
                    index.itemTables[lower_text(entry.id)].push_back(table);
                }
            }
        }
        for (std::uint32_t bucket = 0; bucket < loot.buckets.size(); ++bucket) {
            for (const gamedata::BucketCandidate& candidate : loot.buckets[bucket].candidates) {
                std::vector<std::uint32_t>& tables = index.itemTables[lower_text(candidate.item)];
                tables.insert(tables.end(), bucketTables[bucket].begin(), bucketTables[bucket].end());
            }
        }
        for (const gamedata::LootVitals& base : loot.bases) {
            if (base.table && *base.table < loot.tables.size()) {
                index.tableRoots[*base.table].push_back("the loot of the vitals row " + base.id);
            }
        }
        for (const auto& [item, root] : loot.containerRoots) {
            if (const std::optional<std::uint32_t> table = options.lootTables->table_index(root)) {
                index.tableRoots[*table].push_back("the loot container item " + item);
            }
        }
        for (const std::string& root : loot.chestRoots) {
            if (const std::optional<std::uint32_t> table = options.lootTables->table_index(root)) {
                index.tableRoots[*table].push_back("the chest root " + root);
            }
        }
    }
    if (const gamedata::CraftingData* crafting = options.craftingData; crafting != nullptr) {
        for (std::uint32_t at = 0; at < crafting->recipes().size(); ++at) {
            if (!crafting->recipes()[at].item.empty()) {
                index.itemRecipes[lower_text(crafting->recipes()[at].item)].push_back(at);
            }
        }
    }
    if (options.progressionTables != nullptr && options.progressionTables->loaded()) {
        index.levelCap = options.progressionTables->level_cap();
    }
}

QuestSupport::Row QuestSupport::evaluate(std::uint32_t at, const BoardMission* mission) const {
    const Index& index = *m_index;
    const gamedata::QuestData& data = *index.quests;
    // Stage QG6a: the effective objective of a board mission (the template with the values of its row), else the row of the data.
    const QuestObjective& objective = mission != nullptr && mission->binding->objective != nullptr ? *mission->binding->objective
                                                                                                   : data.objectives()[at];
    const WorldOptions& options = m_world.options();
    const std::string id = lower_text(objective.id);
    // Stage QG8: the declared sources of the item of a possession or equip task that no supported source gives, each with the cause that the
    // backend does not run it, and the class of the first one (the precedence of the census of stage QG8: the task drop, the chest drop, a
    // loot table, a recipe; none). No rule of a source: only the facts of the data files.
    const auto item_causes = [&](const QuestTask& task, bool drop) -> std::pair<std::string, std::string> {
        std::string source;
        std::vector<std::string> causes;
        const auto add = [&](const char* kind, std::string cause) {
            if (source.empty()) {
                source = kind;
            }
            causes.push_back(std::move(cause));
        };
        const bool gather = task.type == task_type::kGatherItems || task.type == task_type::kGatherAndReturnItems;
        if (task.type != task_type::kEquipItem && task.dropChance > 0.0 && !task.dropSources.empty()) {
            if (!drop) {
                add("task drop", "the quest drop of " + task.dropSources.front() + ": MinGearScore stops the drops (0x825F5C0)");
            }
            for (std::size_t at = 0; drop && at < task.dropSources.size() && at < 3; ++at) {
                const std::string& name = task.dropSources[at];
                const gamedata::GatheringData* gathering = options.gatheringData;
                const std::optional<std::uint32_t> found = gathering != nullptr ? gathering->find_row(name) : std::nullopt;
                const std::string* kept = options.creatureData != nullptr ? options.creatureData->kept_out(name) : nullptr;
                std::string cause;
                if (found && !gathering->rows()[*found].unsupported.empty()) {
                    cause = "the gatherable row is refused (" + gathering->rows()[*found].unsupported.front() + ")";
                } else if (found && !gather && !gathering->rows()[*found].quest) {
                    cause = "a gatherable row that is not a quest object (a possession task drops only at a kill or at a quest object)";
                } else if (found) {
                    cause = "no node or quest object of the open world has the row";
                } else if (gather && index.dropCategories.count(name) != 0) {
                    cause = "a creature of the world has the category, and the backend gathers no skinning row of it (a refused skinning row, "
                            "or no skinning)";
                } else if (kept != nullptr) {
                    cause = "no creature of the open world has the category (" + *kept + ")";
                } else {
                    cause = "no creature of the open world data has the category, and the gathering data has no row of it (an instance, an "
                            "object that the data tools do not find, or no placement)";
                }
                add("task drop", "the quest drop of " + name + ": " + cause);
            }
        }
        if (task.type != task_type::kEquipItem && task.chestChance > 0.0) {
            std::string cause = task.minGearScore != 0 ? "MinGearScore stops the drops"
                                : task.poiTags.empty()   ? "no available chest of the chest data"
                                                         : "no available chest of the chest data in a point of interest of the tags";
            for (const std::string& tag : task.poiTags) {
                if (const std::string shape = options.poiData != nullptr ? options.poiData->cause(tag) : std::string{}; !shape.empty()) {
                    cause += " (" + tag + ": " + shape + ")";
                    break;
                }
            }
            add("chest drop", "the quest drop of the chests: " + cause);
        }
        const std::string item = lower_text(task.itemName);
        if (const auto tables = index.itemTables.find(item); tables != index.itemTables.end() && options.lootTables != nullptr) {
            // The roots above the first table (each table one time, at most 64 steps).
            std::vector<std::string> roots;
            std::vector<std::uint32_t> pending = tables->second;
            std::unordered_set<std::uint32_t> seen;
            for (std::size_t steps = 0; !pending.empty() && steps < 64 && roots.size() < 3; ++steps) {
                const std::uint32_t table = pending.back();
                pending.pop_back();
                if (!seen.insert(table).second) {
                    continue;
                }
                if (const auto found = index.tableRoots.find(table); found != index.tableRoots.end()) {
                    roots.insert(roots.end(), found->second.begin(), found->second.end());
                }
                pending.insert(pending.end(), index.tableParents[table].begin(), index.tableParents[table].end());
            }
            roots.resize(std::min<std::size_t>(roots.size(), 3));
            std::string above;
            for (const std::string& root : roots) {
                above += (above.empty() ? "" : ", ") + root;
            }
            add("loot table", "the loot table " + options.lootTables->table(tables->second.front()).id + ": no source of the world reaches it ("
                                  + (above.empty() ? std::string("no root of the loot data above it") : "above it: " + above) + ")");
        }
        if (const auto recipes = index.itemRecipes.find(item); recipes != index.itemRecipes.end() && options.craftingData != nullptr) {
            const gamedata::Recipe& recipe = options.craftingData->recipes()[recipes->second.front()];
            add("recipe", "the recipe " + recipe.id + ": "
                              + (!recipe.unsupported.empty() ? "the crafting refuses it (" + recipe.unsupported.front() + ")"
                                                             : std::string("an ingredient with no supported source")));
        }
        if (causes.empty()) {
            add("none", "no task drop, chest drop, loot table, or recipe of the data files has it");
        }
        std::string text;
        for (const std::string& cause : causes) {
            text += (text.empty() ? "" : "; ") + cause;
        }
        return {source, text};
    };
    Row row;
    row.kind = is_developer(index.tables[at])     ? "developer"
               : data_rules::same_text(objective.type, "Mission") ? "missionTemplate"
               : data_rules::same_text(objective.type, "CommunityGoal") ? "communityGoal"
                                                                       : "objective";
    // The grant sources (research quest-engine-3.md Q1) and the kind of each one that the backend runs.
    // Stage QG4: the group of the first grant that the backend does not run (the line "grant: ..." of the blocks names it).
    std::string grantGroup;
    const auto add_grant = [&](const std::string& source, const std::string& detail, bool supported, const std::string& reason,
                               const std::string& group = std::string{}) {
        row.grants.push_back(json{{"source", source}, {"detail", detail}, {"supported", supported}, {"reason", reason}});
        row.grantKind = row.grantKind || supported;
        if (!supported && grantGroup.empty()) {
            grantGroup = group;
        }
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
            std::string group;
            bool supported = false;
            if (persons == index.persons.end()) {
                // Stage QG4: the cause of the missing slot (a person of an instance, of the old introduction world, of an event spawn, no
                // placement in the data).
                const auto [apartGroup, detail] = apart_cause(options.interactionData, npc->id);
                reason = "the NPC " + npc->id + " has no person slot in the interaction data; " + detail;
                group = apartGroup;
            } else {
                for (const gamedata::InteractionObject* object : persons->second) {
                    if (object->person->unresolved.empty()) {
                        supported = true;
                        break;
                    }
                    reason = "the person slot of " + npc->id + " is not resolved: " + object->person->unresolved;
                    group = "a person slot that is not resolved";
                }
            }
            add_grant("offer", npc->id + " " + m_world.persons().data()->state(state).id, supported, supported ? "" : reason, group);
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
    // Stage QG2a, decision B of the orchestrator: the push of PushObjectiveOnReqMet (18 rows; not the old start 01_SoleSurvivor2 and the two
    // debug seasonal rows). A push row with ExclusiveTerritory needs the character in that territory (no history has a territory).
    if (objective.pushOnRequirement) {
        const bool pushed = Quests::pushed(objective);
        const bool territory = objective.exclusiveTerritory.value_or(0) != 0;
        add_grant("push", objective.required, pushed && !territory,
                  !pushed     ? "decision B of the orchestrator: the push does not grant this row (the old start of the game or a debug seasonal row)"
                  : territory ? "the push of a row with ExclusiveTerritory needs the character in that territory"
                              : "");
    }
    // Stage QG4: the equip of an artifact (OnEquipObjectiveId of the item catalog): the backend runs it when the progression tables have
    // the item level of its gear score (ProgressionTables::item_level; 700: the level 65) and that level is not above the level cap.
    if (const auto found = index.equips.find(id); found != index.equips.end()) {
        for (const gamedata::ArtifactObjective* artifact : found->second) {
            const std::uint32_t level = artifact_level(options, artifact->itemId);
            const bool supported = level != 0 && level != gamedata::kNoItemLevel && level <= index.levelCap;
            add_grant("equip", artifact->itemId + (level != 0 && level != gamedata::kNoItemLevel ? " level " + std::to_string(level) : std::string{}),
                      supported,
                      supported   ? ""
                      : level == 0 ? "the progression tables or the item catalog have no item level of the artifact (equipGearScores)"
                                   : "the item level of the artifact is above the level cap",
                      "an artifact with no item level");
        }
    }
    // Stage QG7: the rank-up row of the validation lists (the faction system grants it after the trial achievement before it).
    if (index.rankUpRows.count(id) != 0) {
        add_grant("faction", "the rank-up row after a trial achievement (Progression Validation Achievement Data)", true, "");
    }
    if (mission != nullptr) {
        // Stage QG6a: a town board of the data gives the mission (Missions: the offers of the client rule, the accept 3819).
        add_grant("board", mission->binding->row->id + " at the board " + mission->board + " (territory " + std::to_string(mission->territory) + ")",
                  mission->binding->refusals.empty(), mission->binding->refusals.empty() ? "" : mission->binding->refusals.front(),
                  "a mission override with no rule");
    } else if (row.kind == "missionTemplate") {
        add_grant("mission", objective.type, false, "the mission boards and the mission definitions have no contract (roadmap 4.5, 5.4)");
    } else if (row.kind == "communityGoal") {
        add_grant("communityGoal", objective.type, false, "the community goals have no contract (roadmap 5.4)");
    }
    if (row.grants.empty()) {
        // Stage QG4: the kind of a row with no source, by its Type [data]: a rank-up row of a faction story (the faction rank system grants
        // it: the faction stage, roadmap 4.5), a mount race row (QuickCourseData names it; no grant row: the mount race system).
        if (objective.type.rfind("FactionStory_", 0) == 0) {
            // Stage QG7: the validation lists name one rank-up row of each rank (9806_*); the rows 9804_*, 9809_*, 9812_* of the same rank
            // have no source in the data.
            add_grant("none", objective.type, false,
                      "a rank-up row of a faction story (Type " + objective.type + ") that the validation lists of the base attributes do not "
                      "name: no source grants it (the faction system grants the rows of the lists)",
                      "a faction rank-up row that the validation lists do not name");
        } else if (data_rules::same_text(objective.type, "MountRace")) {
            add_grant("none", objective.type, false,
                      "a mount race row (Type MountRace; QuickCourseData names it): no grant row of the data; the mount race system has no owner",
                      "a mount race row (no grant)");
        } else {
            add_grant("none", "", false, "the data of the backend has no source that grants the objective");
        }
    }
    // The gates whose rule is known before a history: the gates with no service rule, the faction of an unaligned character, a level above
    // the cap.
    // Stage QG2a: RequiredGameModeId (row +0x34, IsInGameMode 0x32BA3B0) is false in the open world (no active game mode); QuestArcTag is no
    // gate (the eligibility 0x3A4BFE0 does not read row +0x20).
    if (!objective.gameMode.empty()) {
        row.gates.push_back("RequiredGameModeId " + objective.gameMode + " is false: no game mode is active in the open world");
    }
    if (!objective.worldTags.empty()) {
        row.gates.push_back("RequiredWorldTags " + objective.worldTags + " has no rule");
    }
    if (!objective.requiredProgression.empty()) {
        row.gates.push_back("RequiredProgression " + objective.requiredProgression + " has no rule");
    }
    // Stage QG7: RequiredFaction is a gate of a history (the faction that it joins): no gate of the row (each faction can join).
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
    // The node counters are std::size_t (code review of stage 1.8e, finding QR8E-R8: a tree of 256 nodes, task index 255, made an 8-bit
    // counter go round for ever); an index goes into the 8-bit fields after the range check of the loop.
    for (std::size_t node = 0; node < objective.nodes.size(); ++node) {
        const QuestNode& entry = objective.nodes[node];
        if (entry.task == gamedata::kNoTask) {
            continue;
        }
        const QuestTask& task = data.task(entry.task);
        TaskFact fact;
        fact.index = static_cast<std::uint8_t>(node);
        const auto block = [&](std::string reason) {
            fact.supported = false;
            fact.reason = std::move(reason);
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
            // Code review of stage QG8, finding QG8R-R4: the world refuses a loot table reward above its roll limit (the task waits).
            if (const gamedata::QuestGameEvent* event = task.targets.empty() ? nullptr : data.game_event(task.targets.front());
                event != nullptr && !gamedata::reward_table(event->item).empty() && event->itemQty > static_cast<std::int64_t>(kRewardTableRolls)) {
                block("the ItemRewardQty " + std::to_string(event->itemQty) + " of the game event " + event->id + " is above the limit of "
                      + std::to_string(kRewardTableRolls) + " rolls of a loot table (the task waits)");
            }
            break;
        case task_type::kTriggerArea:
            triggers.push_back(task.targetTag);
            // Stage QG2a (research quest-gaps.md section 2): a task with no tag (a pin or a cyclic puzzle: impossible_task, DummyPin) and a
            // task with a never-true tag (nevertrue, NEVERCOMPLETE) never complete; no volume of the data is missing for them.
            if (task.targetTag.empty()) {
                block("the task has no TP_TargetTag: a pin or a puzzle task of the data with no volume (it never completes)");
            } else if (never_true_tag(task.targetTag)) {
                block("the tag " + task.targetTag + " is a never-true tag of the data (a sentinel: no volume; the task never completes)");
            } else if (options.triggerData == nullptr || !options.triggerData->knows(task.targetTag)) {
                // Stage QG3: a volume of an instance (a dungeon, the introduction worlds) is never tested in the open world.
                const std::vector<std::string>* worlds = options.triggerData != nullptr ? options.triggerData->elsewhere(task.targetTag) : nullptr;
                if (worlds != nullptr) {
                    std::string names;
                    for (const std::string& world : *worlds) {
                        names += (names.empty() ? "" : ", ") + world;
                    }
                    block("the volume of the tag " + task.targetTag + " is in the instance " + names
                          + ", not in the open world (it belongs to its instance stage)");
                } else {
                    block("no trigger volume of the tag " + task.targetTag + " in the trigger data");
                }
            } else {
                fact.supplier = "the trigger volume " + task.targetTag;
            }
            break;
        case task_type::kGoToPoi:
        case task_type::kGoToPoiOnTerritory:
        case task_type::kExitPoi:
        case task_type::kPatrol: { // Stage QG6b: the patrol is the time in a POI of its tags (Quests: the dwell of the task).
            std::string tags;
            for (const std::string& tag : task.poiTags) {
                places.push_back(tag);
                tags += (tags.empty() ? "" : ",") + tag;
            }
            // Stage QG3: a task with no tag, an empty task of a template, and a tag with no shape (with the cause of the data) stay apart.
            if (task.poiTags.empty()) {
                block(row.kind == "missionTemplate" || row.kind == "communityGoal"
                          ? "the task has no POITag: the mission instance of a template gives the place (the mission boards and the community "
                            "goals: roadmap 4.5, 5.4)"
                          : (task.pin ? "the task has no POITag: a pin task (OverrideLocation) with no place of the data (it never completes)"
                                      : "the task has no POITag: a blank cell of the data, no place (it never completes)"));
            } else if (options.poiData == nullptr || !options.poiData->knows(task.poiTags)) {
                block("no point of interest shape of the tags " + tags + " in the POI data" + poi_causes(options.poiData, task.poiTags));
            } else {
                fact.supplier = "the point of interest " + tags;
            }
            break;
        }
        case task_type::kKillContribution: {
            std::string found;
            // Stage QG9a: a kill task with a GameModeID counts only in that mode: the members of the instance recipes of the mode.
            const auto modeCategories = index.modeKillCategories.find(lower_text(task.gameMode));
            const std::unordered_set<std::string>* categories =
                task.gameMode.empty() ? &index.killCategories
                                      : (modeCategories != index.modeKillCategories.end() ? &modeCategories->second : nullptr);
            for (const std::string& target : task.targets) {
                creatures.push_back(target);
                if (found.empty() && categories != nullptr && categories->count(target) != 0) {
                    found = target;
                }
            }
            // Stage QG2a (research quest-gaps.md section 8): KillEnemyWeaponID names an ability table of a weapon (a filter: the kill with
            // that weapon), or an item; one row names neither (Turkey).
            if (!task.weapon.empty() && weapon_filter(task.weapon)) {
                block("KillEnemyWeaponID " + task.weapon + " is a weapon filter (an ability table of a weapon, not an item): the backend has no "
                      "weapon of a kill");
            } else if (!task.weapon.empty() && (options.itemCatalog == nullptr || options.itemCatalog->find(task.weapon) == nullptr)) {
                block("KillEnemyWeaponID " + task.weapon + " names no item and no ability table of the data");
            } else if (!task.weapon.empty()) {
                block("KillEnemyWeaponID " + task.weapon + ": the backend has no item of a kill");
            } else if (found.empty()) {
                // Stage QG5c1: the cause of the creature data for a target whose entries the file does not have (the client does not show
                // the slice, an encounter entry, an override entry; the member keptOut of the creature tool).
                std::string cause;
                for (const std::string& target : task.targets) {
                    const std::string* kept = options.creatureData != nullptr ? options.creatureData->kept_out(target) : nullptr;
                    if (kept != nullptr) {
                        cause = " (" + target + ": " + *kept + ")";
                        break;
                    }
                }
                block("no creature of a spawn point has a vitals category of the task" + cause);
            } else {
                fact.supplier = "the kills of the category " + found;
            }
            break;
        }
        case task_type::kGameEvent: {
            if (task.targets.empty()) {
                // Stage QG2a: a pin or a map task of the data (PinTask, Dummy_WorldPin): no event, it never completes.
                block("the task names no game event: a pin or a map task of the data (it never completes)");
                break;
            }
            // The first target that a kill or a producer gives. Code review of stage QG5c2, finding QG5C2R-R6: a producer is taken as a
            // whole (its rank gate and its item together), in this order: no rank gate and no item or an item of a source of the world; the
            // smallest rank gate (stage QG5b: AzothStaff, the history must have the rank) with no item or an item of the world; no rank gate
            // and an item of a quest give (the history must have it); a rank gate and an item of a quest. Finding QG5BR-R3: a ranked producer
            // needs both of its gates.
            const Index::GatherSource* best = nullptr;
            const std::string* bestTarget = nullptr;
            int bestClass = 99;
            const std::string* itemBlocked = nullptr;
            const Index::GatherSource* blockedSource = nullptr;
            bool kill = false;
            for (const std::string& target : task.targets) {
                if (index.killEvents.count(target) != 0) {
                    fact.supplier = "the kill event " + target;
                    kill = true;
                    break;
                }
                const auto sources = index.gatherSources.find(target);
                if (sources == index.gatherSources.end()) {
                    continue;
                }
                for (const Index::GatherSource& source : sources->second) {
                    const bool world = source.item.empty() || index.items.count(source.item) != 0;
                    const bool quest = !world && index.questItems.count(source.item) != 0;
                    const int rank = source.rankSkill.empty() ? 0 : 1;
                    const int classOf = world ? rank : quest ? 2 + rank : 99;
                    if (classOf == 99) {
                        if (itemBlocked == nullptr) {
                            itemBlocked = &target;
                            blockedSource = &source;
                        }
                        continue;
                    }
                    if (classOf < bestClass || (classOf == bestClass && rank == 1 && source.rankLevel < best->rankLevel)) {
                        best = &source;
                        bestTarget = &target;
                        bestClass = classOf;
                    }
                }
                if (best != nullptr) {
                    break;
                }
            }
            if (kill) {
                break;
            }
            if (best == nullptr && itemBlocked != nullptr) {
                // Stage QG5b: a gather with the restriction ItemRequired needs its item in the bag: a source of the world, or a quest give.
                block("the gather of the game event " + *itemBlocked + " needs the item " + blockedSource->item
                      + " (the restriction ItemRequired), and no supported source gives it");
                break;
            }
            if (best == nullptr) {
                const auto refused = index.refusedEvents.find(task.targets.front());
                block("no producer of the game event " + task.targets.front()
                      + (refused != index.refusedEvents.end() ? " (" + refused->second + ")"
                                                              : std::string(" (no creature, gatherable, or destructible object of the data gives it)")));
                break;
            }
            if (best->rankSkill.empty()) {
                fact.supplier = best->kind + " (" + *bestTarget + ")";
            } else {
                fact.supplier = "a gather with the rank " + std::to_string(best->rankLevel) + " of the trade skill " + best->rankSkill + " ("
                                + *bestTarget + ")";
                fact.rankSkill = best->rankSkill;
                fact.rankLevel = best->rankLevel;
            }
            if (!best->item.empty()) {
                if (bestClass < 2) {
                    fact.supplier += " with the item " + best->item + " of a source of the world";
                } else {
                    fact.supplier += " with the item " + best->item + " of a quest (the history decides)";
                    fact.questItem = best->item;
                }
            }
            // Finding QG5C3R-R4: the other producers of the same event that a history can use, each with its own gates (a free producer
            // with the rank 5 of AzothStaff first, then an unranked producer with a key that a quest gives: a history with the key and no
            // rank takes the second one).
            if (bestClass >= 2 || !best->rankSkill.empty()) {
                for (const Index::GatherSource& source : index.gatherSources.at(*bestTarget)) {
                    const bool world = source.item.empty() || index.items.count(source.item) != 0;
                    if (&source == best || (!world && index.questItems.count(source.item) == 0)) {
                        continue;
                    }
                    TaskFact::Alternative alternative{world ? std::string() : source.item, source.rankSkill, source.rankLevel};
                    const bool same = (alternative.questItem == fact.questItem && alternative.rankSkill == fact.rankSkill
                                       && alternative.rankLevel == fact.rankLevel)
                                      || std::any_of(fact.alternatives.begin(), fact.alternatives.end(), [&](const TaskFact::Alternative& other) {
                                             return other.questItem == alternative.questItem && other.rankSkill == alternative.rankSkill
                                                    && other.rankLevel == alternative.rankLevel;
                                         });
                    if (!same) {
                        fact.alternatives.push_back(std::move(alternative)); // Each set of gates one time (the rows of one producer).
                    }
                }
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
            if (mission != nullptr && task.interactTag == "returntogiver") {
                // Stage QG6a: the return task of a mission template goes to its provider (factory 0x4461150, case 23): the open of the board.
                // Stage QG6b: by the tag only (0x4462726: the factory does not read ReturnToGiverOverride, row +0x14B; FACTORY.md section 3).
                fact.supplier = "the provider board of the mission (the return tag)";
            } else if (index.objectTags.count(task.interactTag) == 0) {
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
                    if (gather ? index.gatherRows.count(source) != 0 || index.skinCategories.count(source) != 0
                               : index.dropCategories.count(source) != 0 || index.questObjectRows.count(source) != 0) {
                        dropSource = source;
                        break;
                    }
                }
            }
            // Stage QG5a: the quest drop of a chest (ChestDropProbability): an ordinary chest in a point of interest of its tags.
            const bool chestDrop = task.type != task_type::kEquipItem && task.minGearScore == 0 && task.chestChance > 0.0 && chest_drop(index.anyChest, index.chestTags, index.chestTagTerritories, task);
            const std::string item = lower_text(task.itemName);
            if (trimmed_blank(task.itemName)) {
                block(blank_item(row.kind));
            } else if (options.itemCatalog == nullptr || options.itemCatalog->find(task.itemName) == nullptr) {
                block("the item catalog has no " + task.itemName);
            } else if (!dropSource.empty()) {
                fact.supplier = "the quest drop of " + dropSource;
            } else if (chestDrop) {
                fact.supplier = "the quest drop of the chests" + (task.poiTags.empty() ? std::string(" of the world") : " of " + task.poiTags.front());
            } else if (const auto world = index.items.find(item); world != index.items.end()) {
                fact.supplier = world->second;
            } else if (const auto quest = index.questItems.find(item); quest != index.questItems.end()) {
                // Only a quest gives the item (finding QR8E-R5): the history has it after the hand-in of a reward, or in the tree of the
                // objective of a give, of a game event, or of a quest drop.
                fact.supplier = quest->second + " (the history decides)";
                fact.questItem = item;
            } else {
                // Stage QG8: the declared sources of the item, each with the cause that the backend does not run it (the classes that the
                // manifest keeps apart: docs/systems/quests.md, the item sources of stage QG8).
                const auto [source, causes] = item_causes(task, drop);
                fact.itemSource = source;
                block("no supported source gives the item " + task.itemName + " (" + causes + ")");
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
            if (trimmed_blank(task.itemName)) {
                block(blank_item(row.kind));
            } else if (options.itemCatalog == nullptr || options.itemCatalog->find(task.itemName) == nullptr) {
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
        case task_type::kHaveLevel:
            // Stage QG2a (research quest-gaps.md section 8; client 0x3B04F40, 0x3B073A0): a blank ItemName is the level of the character; an
            // ItemName is the points or the rank of a categorical progression (Covenant, Marauder, Syndicate: a faction, roadmap 4.5).
            // Stage QG7: a faction progression of the progression tables (its points or its rank; the history decides).
            if (!trimmed_blank(task.itemName)) {
                const gamedata::ProgressionTables* tables = options.progressionTables;
                const bool faction = tables != nullptr && tables->loaded()
                                     && tables->faction_of_progression(gamedata::progression_id(trimmed_copy(task.itemName))) != nullptr;
                if (!faction) {
                    block("TaskHaveLevel of the categorical progression " + task.itemName + " (" + (task.checkPoints ? "points" : "rank") + " "
                          + std::to_string(task.targetQty.value_or(0)) + "): not a faction progression of the progression tables (no owner)");
                } else {
                    fact.supplier = "the " + std::string(task.checkPoints ? "points" : "rank") + " of the faction progression " + task.itemName + " ("
                                    + std::to_string(task.targetQty.value_or(0)) + " or more; the history decides)";
                }
            } else if (task.targetQty.value_or(0) < 1) {
                block("TaskHaveLevel with the TargetQty " + std::to_string(task.targetQty.value_or(0)) + " never completes (client 0x3B073A0)");
            } else {
                fact.supplier = "the level of the character (" + std::to_string(*task.targetQty) + " or more; the history decides)";
            }
            break;
        case task_type::kChooseFaction:
            // Stage QG7: the faction choice of a recruitment (3161 with the Faction_Selection service of the person; docs/systems/factions.md).
            if (m_world.factions().recruitment_faction(objective.achievement) == 0) {
                block("TaskChooseFaction of an objective that is not the recruitment of a faction of the faction data");
            } else {
                fact.supplier = "the faction choice of the recruitment (3161 with the Faction_Selection service of " + task.itemName + ")";
            }
            break;
        case task_type::kGameMode:
            // Stage QG9a (research story-instances.md 2): the membership of an instance of the mode (the entrance of the interaction data,
            // the content of the encounter system); GameModeMustBeMutated needs a mutation context, which the backend has not.
            if (task.modeMutated) {
                block("TaskGameMode with GameModeMustBeMutated: the backend has no mutation context of a game mode");
            } else if (const auto off = index.modesOff.find(lower_text(task.gameMode)); off != index.modesOff.end()) {
                block("TaskGameMode of the mode " + task.gameMode + ", which the setting of the mode turns off: " + off->second);
            } else if (index.modes.count(lower_text(task.gameMode)) == 0) {
                block("TaskGameMode of the mode " + task.gameMode + ", which the backend does not run (no entrance or no content of the mode; "
                      "docs/systems/instances.md)");
            } else {
                fact.supplier = "the membership of an instance of the mode " + task.gameMode + " (its entrance)";
            }
            break;
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
        // Stage QG9a: a task with a GameModeID counts only in that mode (Quests::matched_tasks): a mode that the backend does not run
        // gives it no fact.
        if (fact.supported && !task.gameMode.empty() && task.type != task_type::kGameMode && index.modes.count(lower_text(task.gameMode)) == 0) {
            block("the task counts only in the game mode " + task.gameMode + ", which the backend does not run (docs/systems/instances.md)");
        }
        // Stage QG9b: a task of a mode that the setting of the mode turns off has the reason of the setting (before each other reason).
        if (const auto off = index.modesOff.find(lower_text(task.gameMode));
            !task.gameMode.empty() && task.type != task_type::kGameMode && off != index.modesOff.end()) {
            block("the task counts only in the game mode " + task.gameMode + ", which the setting of the mode turns off: " + off->second);
        }
        // Stage QG2a: the location rule of a task (EnforceLocationRequirement with a POITag: the counters, the drops, the possession and the
        // equip tasks progress only in a point of interest of a tag; Quests::matched_tasks, plan_drops, Plan::location_ok). With no shape of
        // any tag of the task in the POI data the task never progresses (brightwood8 of 02A_Side06_01_Bears is a tag with no shape). A place
        // task has its own check above.
        if (fact.supported && task.enforceLocation && !task.poiTags.empty() && enforces_location(task.type)) {
            const bool shaped = options.poiData != nullptr && options.poiData->loaded()
                                && std::any_of(task.poiTags.begin(), task.poiTags.end(),
                                               [&](const std::string& tag) { return options.poiData->knows(std::vector<std::string>{tag}); });
            if (!shaped) {
                std::string tags;
                for (const std::string& tag : task.poiTags) {
                    tags += (tags.empty() ? "" : ",") + tag;
                }
                block("the location rule (EnforceLocationRequirement) needs a point of interest shape of the tags " + tags
                      + ": the POI data has none (the task never progresses)" + poi_causes(options.poiData, task.poiTags));
            }
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
    for (std::size_t node = 0; node < objective.nodes.size(); ++node) {
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
    // The rewards with an empty history (the standing of a history: the closure checks it again). Stage QG1: the standing goes to the
    // territory of the hand-in: the territory of the first resolved slot of the destination person (the bounds of the POI data at its
    // place), else ExclusiveTerritory, else the start territory (Quests::standing_territory).
    std::uint16_t destination = 0;
    if (const auto slots = index.persons.find(lower_text(objective.destination)); slots != index.persons.end() && options.poiData != nullptr
                                                                                   && options.poiData->loaded()) {
        for (const gamedata::InteractionObject* object : slots->second) {
            if (destination == 0 && object->person->unresolved.empty()) {
                destination = options.poiData->territory_at(object->position.x, object->position.y);
            }
        }
    }
    RewardJoin join;
    std::string detail;
    // Stage QG6a: a board mission pays the success event of its row, and its standing goes to the territory of the board.
    QuestObjective payer = objective;
    if (mission != nullptr) {
        if (!mission->binding->row->successEvent.empty()) {
            payer.successEvent = mission->binding->row->successEvent;
        }
        destination = mission->territory;
    }
    // Stage QG6b: the standing of a board mission goes to the territory of its board (the hand-in at the board, and the origin of the
    // instance for GrantRewardsToOriginTerritory: Quests::reward_join), with no fallback (a territory with no standing row refuses).
    const std::uint16_t standingAt = mission != nullptr ? mission->territory
                                                        : m_world.quests().standing_territory(destination,
                                                                                              objective.exclusiveTerritory.value_or(kNewStartTerritory));
    const QuestError reward = m_world.quests().reward_join(SkillSet{}, payer, standingAt, join, detail);
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
                          {"progression", join.progression.empty() ? std::string{} : join.progression + " " + std::to_string(join.progressionAmount)},
                          {"next", join.next != nullptr ? join.next->id : ""},
                          {"repeat", repeat_text(data, objective)}};
    if (!row.rewards) {
        row.blocked.push_back("rewards: " + detail);
    }
    if (!row.grantKind) {
        row.blocked.push_back("grant: no source of a kind that the backend runs" + (grantGroup.empty() ? std::string{} : " (" + grantGroup + ")"));
    }
    for (const std::string& gate : row.gates) {
        row.blocked.push_back("gate: " + gate);
    }
    row.world = json{{"triggerTags", triggers}, {"poiTags", places}, {"persons", personsUsed}, {"objectTags", objects}, {"killTargets", creatures}};
    row.supported = (row.kind == "objective" || mission != nullptr) && row.tree && row.rewards && row.grantKind && row.gates.empty();
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
        // The items of the creation (Inventory::start_items of the archetype, the rule of the character creation).
        if (const gamedata::ItemCatalog* catalog = m_world.options().itemCatalog; catalog != nullptr) {
            for (const state::ItemRow& item : start_items(*catalog, backstory.id)) {
                profile.items.push_back(lower_text(item.itemId));
            }
        }
        profiles.push_back(std::move(profile));
    }
    // Stage QG7: the first new character once more for each faction (SupportProfile::faction): the history joins only that faction. The
    // first history (the primary one) joins the faction of the first recruitment that it completes.
    if (!profiles.empty()) {
        for (std::uint8_t faction = 1; faction <= gamedata::kFactionCount; ++faction) {
            SupportProfile joined = profiles.front();
            joined.id += ":" + std::string(faction_name(faction));
            joined.group = "new character, faction " + std::string(faction_name(faction));
            joined.faction = faction;
            profiles.push_back(std::move(joined));
        }
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
    history.scratch.faction.faction = 0; // Stage QG7: a history joins its faction at a recruitment (SupportProfile::faction).
    for (const std::string& achievement : profile.achievements) {
        if (const std::optional<std::uint32_t> bit = ownership.achievement_bit(achievement)) {
            history.bits.insert(*bit);
        }
    }
    history.items.clear();
    history.items.insert(profile.items.begin(), profile.items.end());
    // The rules of the engine (Quests::Plan::activate) in the run below: the state of each node of a tree that runs.
    enum class Node : std::uint8_t { inactive, active, done };
    // Stage QG5b: the trees that run now and the states of their nodes, for the ObjectiveFlow of an NPA (Quests::conditional reads the
    // instances of the character: HasObjective and the task states). Each running tree is an instance of the scratch entity.
    std::vector<std::pair<std::uint32_t, const std::vector<Node>*>> runningStates;
    const auto quests_now = [&]() {
        history.scratch.quests.active.clear();
        for (const auto& [objectiveAt, nodes] : runningStates) {
            QuestInstance quest;
            quest.instance = objectiveAt + 1;
            quest.objective = objectives[objectiveAt].id;
            quest.objectiveCrc = objectives[objectiveAt].crc;
            quest.tasks.resize(nodes->size());
            for (std::size_t node = 0; node < nodes->size(); ++node) {
                quest.tasks[node].state = (*nodes)[node] == Node::done     ? quest_state::kComplete
                                          : (*nodes)[node] == Node::active ? quest_state::kActive
                                                                           : quest_state::kNew;
            }
            history.scratch.quests.active.push_back(std::move(quest));
        }
    };
    const auto sync = [&]() {
        history.scratch.achievements.assign(history.bits.begin(), history.bits.end());
        // Code review of stage QG7, finding QG7R-R5: the person selection and the hand-in checks read the skills of the scratch entity: the
        // categorical progressions (the faction reputation and tokens) that the history earned.
        history.scratch.skills = history.skills;
        quests_now();
    };
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
    // The hand-in of a completed tree (code review of stage 1.8e, finding QR8E-R6; Persons::hand_in): the NpcDestinationId of the objective
    // is the person of the conversation, and the person must be there for the character with the bits after the tasks (Persons::present).
    // @return The cause when the history cannot hand it in, or an empty text.
    // Stage QG1: `territory` gets the territory of the present slot of the hand-in person (the bounds of the POI data at its place: the
    // territory of the character at the hand-in, Quests::reward_join), or 0.
    const auto hand_in_cause = [&](const QuestObjective& objective, const std::set<std::uint32_t>& bits, std::uint16_t& territory) -> std::string {
        territory = 0;
        if (Quests::self_completes(objective)) {
            return std::string{}; // Stage QG7: a faction story row with no NpcDestinationId completes at its root (docs/systems/factions.md).
        }
        if (objective.destination.empty()) {
            return "the hand-in: the objective has no NpcDestinationId (the hand-in needs the person of the conversation)";
        }
        const auto slots = index.persons.find(lower_text(objective.destination));
        if (slots == index.persons.end()) {
            return "the hand-in: the NPC " + objective.destination + " has no person slot in the interaction data";
        }
        history.scratch.achievements.assign(bits.begin(), bits.end());
        quests_now();
        for (const gamedata::InteractionObject* object : slots->second) {
            if (object->person->unresolved.empty() && persons.present(history.scratch, *object->person)) {
                const gamedata::PoiData* pois = m_world.options().poiData;
                territory = pois != nullptr && pois->loaded() ? pois->territory_at(object->position.x, object->position.y) : 0;
                return std::string{};
            }
        }
        return "the hand-in: the person " + objective.destination + " is not there for the character (no resolved slot, or its presence rule)";
    };
    // The objectives whose tree runs now (a parent and its child objectives): their gives are items of the history while they run.
    std::vector<std::uint32_t> running;
    // Plays the tree of one objective on a copy of the bits and of the items: the achievement actions in index order, the achievement waits
    // (with a lore page that the player reads then), the child objectives, and the completion. @return The cause of a stop, or an empty text.
    std::function<std::string(std::uint32_t, std::uint16_t, std::set<std::uint32_t>&, SkillSet&, std::set<std::string>&, std::vector<std::uint32_t>&, int)>
        run;
    run = [&](std::uint32_t at, std::uint16_t origin, std::set<std::uint32_t>& bits, SkillSet& skills, std::set<std::string>& items,
              std::vector<std::uint32_t>& done, int depth) -> std::string {
        const QuestObjective& objective = objectives[at];
        const Row& fact = facts[at];
        if (depth > 8) {
            return "the child objectives are too deep";
        }
        if (fact.kind != "objective" || !fact.tree || !fact.rewards || !fact.gates.empty()) {
            return fact.blocked.empty() ? "not supported" : fact.blocked.front();
        }
        running.push_back(at);
        struct Leave {
            std::vector<std::uint32_t>& running;
            ~Leave() {
                running.pop_back();
            }
        } leave{running};
        // An item that only a quest gives (finding QR8E-R5): the items of the history, or a give of an objective whose tree runs now.
        const auto has_item = [&](const std::string& item) {
            return items.count(item) != 0 || std::any_of(running.begin(), running.end(), [&](std::uint32_t objectiveAt) {
                       const std::vector<std::string>& given = index.gives[objectiveAt];
                       return std::find(given.begin(), given.end(), item) != given.end();
                   });
        };
        std::vector<std::string> temporary;
        // The rules of the engine (Quests::Plan::activate): a consecutive container activates its first child that is not done, a simple
        // or a partial container each child; an achievement action runs at its activation and completes; a partial container completes
        // with its first done child. The player does each supported task of the world (a place, a kill, a gather, a cutscene, a timer) when
        // it is Active; an achievement wait completes when its conditional is true (with a lore page that the player reads then).
        std::vector<Node> state(objective.nodes.size(), Node::inactive);
        runningStates.emplace_back(at, &state);
        struct LeaveStates {
            std::vector<std::pair<std::uint32_t, const std::vector<Node>*>>& states;
            ~LeaveStates() {
                states.pop_back();
            }
        } leaveStates{runningStates};
        std::vector<bool> supported(objective.nodes.size(), true);
        std::vector<std::string> needs(objective.nodes.size());
        std::vector<const TaskFact*> ranks(objective.nodes.size(), nullptr);
        std::vector<const TaskFact*> facts(objective.nodes.size(), nullptr);
        for (const TaskFact& item : fact.tasks) {
            supported[item.index] = item.supported;
            needs[item.index] = item.questItem;
            facts[item.index] = &item;
            if (!item.rankSkill.empty()) {
                ranks[item.index] = &item;
            }
        }
        // Stage QG5b: the rank of the history for the rank gate of a producer (TaskFact::rankSkill).
        const auto rank_below = [&](std::uint8_t node) {
            return ranks[node] != nullptr && skills.rank(gamedata::progression_id(ranks[node]->rankSkill)) < ranks[node]->rankLevel;
        };
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
                if (const std::string cause = run(childAt, origin, bits, skills, items, done, depth + 1); !cause.empty()) {
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
            for (std::size_t at = 0; at < objective.nodes.size() && state[objective.root] != Node::done; ++at) {
                const auto node = static_cast<std::uint8_t>(at);
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
                    quests_now();
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
                    quests_now();
                    if (page && quests.conditional(history.scratch, task.itemName)) {
                        bits = std::move(withPages);
                        complete(node);
                        progress = true;
                    }
                    continue;
                }
                if (supported[node] && task.type != task_type::kObjective) {
                    // Only a quest gives the item, and the history does not have it (finding QR8E-R5); stage QG5b: the history does not
                    // have the rank of the producer (AzothStaff). Finding QG5C3R-R4: an other producer of the event whose own gates pass
                    // opens the task.
                    const bool first = (needs[node].empty() || has_item(needs[node])) && !rank_below(node);
                    const bool other = facts[node] != nullptr
                                       && std::any_of(facts[node]->alternatives.begin(), facts[node]->alternatives.end(),
                                                      [&](const TaskFact::Alternative& alternative) {
                                                          return (alternative.questItem.empty() || has_item(alternative.questItem))
                                                                 && (alternative.rankSkill.empty()
                                                                     || skills.rank(gamedata::progression_id(alternative.rankSkill))
                                                                            >= alternative.rankLevel);
                                                      });
                    if (!first && !other) {
                        continue;
                    }
                    // Stage QG2a: TaskHaveLevel of the level of the character: the level of the history (Quests::Plan::predicate). Stage
                    // QG7: of a faction progression: its points or its rank in the skills of the history.
                    if (task.type == task_type::kHaveLevel && trimmed_blank(task.itemName)
                        && static_cast<std::int64_t>(history.scratch.progression.level) < task.targetQty.value_or(0)) {
                        continue;
                    }
                    if (task.type == task_type::kHaveLevel && !trimmed_blank(task.itemName)) {
                        const ProgressionRank have = Skills::progression_of(skills, gamedata::progression_id(trimmed_copy(task.itemName)));
                        if (static_cast<std::int64_t>(task.checkPoints ? have.points : have.rank) < task.targetQty.value_or(0)) {
                            continue;
                        }
                    }
                    // Stage QG7: the faction choice: a history with no faction joins the faction of the recruitment (the faction of the
                    // profile only, when it has one); a history of that faction has it; else the task waits (the decline branch).
                    if (task.type == task_type::kChooseFaction) {
                        const std::uint8_t wanted = m_world.factions().recruitment_faction(objective.achievement);
                        std::uint8_t& have = history.scratch.faction.faction;
                        if (have == 0 && (profile.faction == 0 || profile.faction == wanted)) {
                            have = wanted;
                        }
                        if (have != wanted) {
                            continue;
                        }
                    }
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
            for (std::size_t at = 0; at < objective.nodes.size(); ++at) {
                const auto node = static_cast<std::uint8_t>(at);
                const QuestNode& entry = objective.nodes[node];
                if (state[node] == Node::active && entry.task != gamedata::kNoTask && !gamedata::is_container(data.task(entry.task).type)) {
                    const QuestTask& task = data.task(entry.task);
                    const auto reason = std::find_if(fact.tasks.begin(), fact.tasks.end(), [&](const TaskFact& item) { return item.index == node; });
                    return "task " + std::to_string(node) + " " + task.id + " waits: "
                           + (task.type == task_type::kAchievement ? "the achievement condition " + task.itemName + " is false in this history"
                              : task.type == task_type::kHaveLevel && supported[node]
                                  ? "the level " + std::to_string(history.scratch.progression.level) + " of the history is below "
                                        + std::to_string(task.targetQty.value_or(0))
                              : reason != fact.tasks.end() && !reason->reason.empty() ? reason->reason
                              : !needs[node].empty() ? "the history has no " + needs[node] + " (only a quest gives it: " + reason->supplier + ")"
                              : rank_below(node) ? "the history has the rank " + std::to_string(skills.rank(gamedata::progression_id(ranks[node]->rankSkill)))
                                                       + " of " + ranks[node]->rankSkill + ", and " + reason->supplier + " needs it"
                                                 : std::string("no step completes it"));
                }
            }
            return "the root task does not complete";
        }
        // The hand-in at the destination (finding QR8E-R6). A child objective with no hand-in still completes its task of the parent (the root
        // of the child is its completion for the parent: TaskObjective), but the history does not count it and gets none of its rewards.
        std::uint16_t handInTerritory = 0;
        if (std::string cause = hand_in_cause(objective, bits, handInTerritory); !cause.empty()) {
            return depth == 0 ? cause : std::string{};
        }
        // The hand-in: the reward join with the standing of the history (stage QG1: the territory of the hand-in person, else the origin
        // territory), then the completion achievements; the temporary unlocks revert.
        RewardJoin join;
        std::string detail;
        if (quests.reward_join(skills, objective, quests.standing_territory(handInTerritory, origin), join, detail,
                               history.scratch.faction.faction) != QuestError::none) {
            return "rewards: " + detail;
        }
        // Stage QG7: the faction rewards go into the skills of the history: the rank of ProgressionReward (a lower rank stays) and its
        // points, then the reputation and the tokens of the event for the faction of the history.
        {
            Skills::Plan plan;
            plan.skills = skills;
            if (join.rankRewardId != 0) {
                if (Skills::progression_of(plan.skills, join.rankRewardId).rank < join.rankRewardRank) {
                    (void)m_world.skills().set_faction_rank(plan, join.rankRewardId, join.rankRewardRank);
                }
                if (join.rankRewardPoints > 0) {
                    (void)m_world.skills().add_faction_points(plan, join.rankRewardId, static_cast<std::int64_t>(join.rankRewardPoints));
                }
            }
            const gamedata::ProgressionTables* tables = m_world.options().progressionTables;
            if (const gamedata::FactionRow* row = join.faction != 0 && tables != nullptr && tables->loaded() ? tables->faction(join.faction) : nullptr) {
                if (join.reputationGain > 0) {
                    (void)m_world.skills().add_faction_points(plan, row->reputationId, static_cast<std::int64_t>(join.reputationGain));
                }
                if (join.factionTokens > 0) {
                    (void)m_world.skills().add_faction_points(plan, row->tokenId, static_cast<std::int64_t>(join.factionTokens));
                }
            }
            skills = plan.skills;
        }
        if (join.standing > 0) {
            Skills::Plan plan;
            plan.skills = skills;
            if (m_world.skills().add_standing(plan, join.standingTerritory, join.standing) == ProgressError::none) {
                skills = plan.skills;
            }
        }
        // Stage QG5b: the categorical progression reward (the AzothStaff rank of the main story) goes into the skills of the history.
        if (!join.progression.empty()) {
            Skills::Plan plan;
            plan.skills = skills;
            if (m_world.skills().add_experience(plan, join.progression, join.progressionAmount) == ProgressError::none) {
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
        for (const RewardItem& item : join.items) {
            items.insert(lower_text(item.itemId));
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
            // Stage QG4: the equip of an artifact (Quests::item_equipped): the history has the item (a source of the world, or an item of the
            // history) and the item level of its gear score; the availability below decides.
            if (!origin) {
                if (const auto found = index.equips.find(id); found != index.equips.end()) {
                    for (const gamedata::ArtifactObjective* artifact : found->second) {
                        const std::string item = lower_text(artifact->itemId);
                        const std::uint32_t level = artifact_level(m_world.options(), artifact->itemId);
                        if ((index.items.count(item) != 0 || history.items.count(item) != 0) && level != 0 && level != gamedata::kNoItemLevel
                            && history.scratch.progression.level >= level) {
                            origin = objective.exclusiveTerritory.value_or(0);
                            break;
                        }
                    }
                }
            }
            // Stage QG2a: the push of PushObjectiveOnReqMet (Quests::push_changed): the availability below decides; a row with ExclusiveTerritory
            // needs the character in that territory (no history has one).
            if (!origin && Quests::pushed(objective) && objective.exclusiveTerritory.value_or(0) == 0) {
                origin = 0;
            }
            // Stage QG7: the next rank-up row of the faction of the history (Quests::next_rank_up, the validation lists).
            if (!origin && index.rankUpRows.count(id) != 0 && history.scratch.faction.faction != 0) {
                sync();
                if (quests.next_rank_up(history.scratch) == &objective) {
                    origin = 0;
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
            std::set<std::string> items = history.items;
            std::vector<std::uint32_t> done;
            const std::uint8_t factionBefore = history.scratch.faction.faction;
            if (const std::string cause = run(at, *origin, bits, skills, items, done, 0); !cause.empty()) {
                history.cause[at] = cause;
                history.scratch.faction.faction = factionBefore; // Stage QG7: a recruitment that does not complete joins no faction.
                continue;
            }
            history.bits = std::move(bits);
            history.skills = std::move(skills);
            history.items = std::move(items);
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
    // Stage QG6b: the items with a source of the world and the items that only a quest gives (the indexes of the possession facts): the
    // second computation decides each board mission with its effective items from them.
    std::set<std::string> worldItems;
    std::set<std::string> questItems;
    for (const auto& [item, source] : m_index->items) {
        worldItems.insert(item);
    }
    for (const auto& [item, source] : m_index->questItems) {
        questItems.insert(item);
    }
    document["itemSources"] = json{{"world", worldItems}, {"quest", questItems}};
    const gamedata::QuestData& data = *m_index->quests;
    const auto& objectives = data.objectives();
    std::vector<Row> rows;
    rows.reserve(objectives.size());
    std::map<std::string, std::uint64_t> kinds;
    std::map<std::string, std::uint64_t> firstReasons;
    std::map<std::string, std::uint64_t> itemSources;
    std::uint64_t supported = 0;
    std::map<std::string, std::uint64_t> supportedKinds;
    // Stage QG6a: the board mission of each template row whose evaluation the row keeps (the first supported one, else the first one).
    std::vector<const BoardMission*> missionOf(objectives.size(), nullptr);
    std::vector<json> missionResults(objectives.size(), json::array());
    std::uint64_t boardMissions = 0;
    std::uint64_t boardMissionsRun = 0;
    for (std::uint32_t at = 0; at < objectives.size(); ++at) {
        rows.push_back(evaluate(at));
        if (rows.back().kind == "missionTemplate" || rows.back().kind == "communityGoal") {
            const auto found = m_index->boardMissions.find(objectives[at].crc);
            for (std::size_t candidate = 0; found != m_index->boardMissions.end() && candidate < found->second.size(); ++candidate) {
                ++boardMissions;
                Row board = evaluate(at, &found->second[candidate]);
                boardMissionsRun += board.supported ? 1 : 0;
                // Stage QG6b: the result of each board mission of the row (the second computation compares each one).
                missionResults[at].push_back(json{{"id", found->second[candidate].binding->row->id}, {"supported", board.supported},
                                                  {"cause", board.blocked.empty() ? std::string() : board.blocked.front()}});
                if (missionOf[at] == nullptr || (board.supported && !rows.back().supported)) {
                    missionOf[at] = &found->second[candidate];
                    rows.back() = std::move(board);
                }
            }
        }
        const Row& row = rows.back();
        ++kinds[row.kind];
        if (row.supported) {
            ++supported;
            ++supportedKinds[row.kind];
        } else if (row.kind == "objective") {
            ++firstReasons[reason_kind(row.blocked.empty() ? "not supported" : row.blocked.front())];
            // Stage QG8: the item tasks with no supported source of the objectives that are not supported, by the class of the first
            // declared source.
            for (const TaskFact& fact : row.tasks) {
                if (!fact.itemSource.empty()) {
                    ++itemSources[fact.itemSource];
                }
            }
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
                                 {"questItem", fact.questItem},
                                 {"reason", fact.reason}});
            if (!fact.itemSource.empty()) {
                tasks.back()["itemSource"] = fact.itemSource; // Stage QG8: the class of the first declared source.
            }
            if (!fact.alternatives.empty()) {
                // Finding QG5C3R-R4: the other producers that a history can use, with their gates.
                json alternatives = json::array();
                for (const TaskFact::Alternative& alternative : fact.alternatives) {
                    alternatives.push_back(
                        {{"questItem", alternative.questItem}, {"rankSkill", alternative.rankSkill}, {"rankLevel", alternative.rankLevel}});
                }
                tasks.back()["alternatives"] = std::move(alternatives);
            }
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
        if (missionOf[at] != nullptr) {
            // Stage QG6a: the board mission of the evaluation of a template row.
            entry["mission"] = json{{"id", missionOf[at]->binding->row->id}, {"board", missionOf[at]->board}, {"territory", missionOf[at]->territory}};
            entry["boardMissions"] = missionResults[at];
        }
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
                              {"supportedByKind", supportedKinds},
                              {"firstReasonOfTheOthers", reasons},
                              {"itemTasksWithNoSource", itemSources},
                              {"boards", m_index->boardCount},
                              {"factionBoards", m_index->factionBoardCount},
                              {"boardMissions", boardMissions},
                              {"boardMissionsRunnable", boardMissionsRun},
                              {"levelCap", m_index->levelCap},
                              {"groups", groupCounts},
                              {"primaryProfile", profiles.empty() ? std::string{} : profiles.front().id},
                              {"primaryCompletable", primary == nullptr ? 0 : std::count(primary->played.begin(), primary->played.end(), true)}};
    return document;
}

} // namespace soulwarden::backend::world

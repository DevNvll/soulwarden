#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "gamedata/loot_tables.h"

// The quest data (stage 1.8a, docs/systems/quests.md): the objectives with their compiled task trees, the task rows, the achievement
// table (the bit of each achievement id), the non-persisted achievements (expressions of other bits), the start objectives of the
// territories, the backstories, the game events, the whispers, and the schedules of the game data.
//
// The data is game data: the tool tools/gamedata/quests.sh makes the file from the game copy of the user, and the file stays outside the
// repository. Without the file the backend has no quest: a new character gets no start quest, and the other systems do not change.
//
// The tree of an objective has the index rule of the client (builder 0x4466350, research quest-engine.md K2): a child gets its index
// before its container, and a container that the tree built before is a hole (no node). The tool compiles the tree; the loader checks it
// again and refuses a file with a missing task link, a CRC conflict, or an index above 255 (the task index of the wire is a u8).

namespace soulwarden::backend::gamedata {

/** File name of the quest data in the game data directory of the backend. */
inline constexpr const char* kQuestDataFile = "quest-data.json";

/** The task types of the client enum (table 0x3AA6A80; research quest-engine.md Q1) that the engine evaluates (stages 1.8a and 1.8c). */
namespace task_type {
inline constexpr std::uint8_t kSimpleContainer = 0;
inline constexpr std::uint8_t kConsecutiveContainer = 1;
inline constexpr std::uint8_t kPartialContainer = 2;
inline constexpr std::uint8_t kTriggerArea = 3;
inline constexpr std::uint8_t kGoToPoi = 6;
inline constexpr std::uint8_t kGoToPoiOnTerritory = 7;
inline constexpr std::uint8_t kExitPoi = 9;
inline constexpr std::uint8_t kKillContribution = 13;
inline constexpr std::uint8_t kGameEvent = 14;
inline constexpr std::uint8_t kTriggerGameEvent = 15;
inline constexpr std::uint8_t kAchievement = 18;
inline constexpr std::uint8_t kLockAchievement = 19;
inline constexpr std::uint8_t kUnlockAchievement = 20;
inline constexpr std::uint8_t kTemporaryUnlockAchievement = 22;
inline constexpr std::uint8_t kInteract = 23;
inline constexpr std::uint8_t kHaveItems = 25;
inline constexpr std::uint8_t kHaveAndReturnItems = 26;
inline constexpr std::uint8_t kGatherItems = 29;
inline constexpr std::uint8_t kGatherAndReturnItems = 30;
inline constexpr std::uint8_t kRequireGatherItems = 31;
inline constexpr std::uint8_t kGiveItem = 33;
inline constexpr std::uint8_t kGiveAndTakeItem = 34;
inline constexpr std::uint8_t kRemoveItem = 36;
inline constexpr std::uint8_t kCraftRecipe = 38;
inline constexpr std::uint8_t kRefineRecipe = 42;
inline constexpr std::uint8_t kEquipItem = 45;
inline constexpr std::uint8_t kCutsceneComplete = 49;
inline constexpr std::uint8_t kObjective = 52;
inline constexpr std::uint8_t kOpenUiScreen = 62;
inline constexpr std::uint8_t kTimer = 64;
inline constexpr std::uint8_t kDie = 65;
inline constexpr std::uint8_t kCraftMultipleRecipes = 86;
} // namespace task_type

/** @return True for the possession tasks: the count of an item in the bag (stage 1.8c). */
[[nodiscard]] constexpr bool is_possession(std::uint8_t type) {
    return type == task_type::kHaveItems || type == task_type::kHaveAndReturnItems || type == task_type::kGatherItems
           || type == task_type::kGatherAndReturnItems;
}

/** @return True for the possession tasks that remove their target count at the end of the objective (client 0x825F730, 0x825FB70). */
[[nodiscard]] constexpr bool is_return(std::uint8_t type) {
    return type == task_type::kHaveAndReturnItems || type == task_type::kGatherAndReturnItems;
}

/** @return True for the three container types. */
[[nodiscard]] constexpr bool is_container(std::uint8_t type) {
    return type <= task_type::kPartialContainer;
}

/** One task row of a tree. The cells keep the rule of the source: a blank cell has no value, an explicit 0 is 0. */
struct QuestTask {
    /** TaskID of the game table. */
    std::string id;
    /** The CRC-32 of the lower-case id: the task of a task element on the wire. */
    std::uint32_t crc{0};
    /** The value of the client enum. */
    std::uint8_t type{0};
    /** ItemName: the achievement of an achievement action, the conditional of TaskAchievement, an item, a cutscene. */
    std::string itemName;
    /** TargetQty. No value: a blank cell (research quest-engine.md Q1: blank and 0 are different rules). */
    std::optional<std::int64_t> targetQty;
    bool stayActive{false};
    bool failOnCondition{false};
    bool hidden{false};
    /** Duration (seconds) of a TaskTimer. No value: a blank cell. */
    std::optional<double> duration;
    /** TP_DescriptionTag: the text of the task line (empty: no line). */
    std::string description;
    // Stage 1.8c: the cells of the event tasks (the authoring texts of the client, 0x825DAD0 to 0x8262500; research quest-engine.md Q1).
    // A list cell is split at "," and each part is trimmed and in lower case.
    /** KillEnemyType: the vitals categories of TaskKillContribution, the game event of TaskGameEvent and TaskTriggerGameEvent. */
    std::vector<std::string> targets;
    /** KillEnemyWeaponID (lower case): the item of the kill. Empty: any. */
    std::string weapon;
    /** POITag: the tags of the points of interest of TaskGoToPOITag and TaskExitPOITag, and of EnforceLocationRequirement. */
    std::vector<std::string> poiTags;
    /**
     * TerritoryID: limits the points of interest of the tags to this territory (0x8262460). 0: none (a blank cell, "0", or a text that is
     * not a number: 23 rows of the data have a name such as "greatcleave4"; the engine of stage 1.8c reads only a number).
     */
    std::uint16_t territory{0};
    /** EnforceLocationRequirement: progress only inside a point of interest of `poiTags` (0x825DAD0, 0x825E860). */
    bool enforceLocation{false};
    /** ItemDropVC: the vitals categories (a possession task) or the gatherable ids (a gather task) of a quest drop. */
    std::vector<std::string> dropSources;
    /** ItemDropProbability: 0 to 100 (a blank cell: 0). */
    double dropChance{0.0};
    /** GuranteedDropAttempts (the spelling of the data): the next drop after this many misses is sure. 0: none. */
    std::uint32_t guaranteedAttempts{0};
    bool awardOnce{false};
    /** PauseAndClearLootOnTaskComplete: no drop when the character has the target count (0x825F3B0). */
    bool pauseLoot{false};
    bool useGlobalStorage{false};
    /** MinGearScore: the smallest gear score of an item (equip, craft); another value than 0 also stops the quest drops (0x825F5C0). */
    std::uint32_t minGearScore{0};
    bool resetOnCondition{false};
    /** CraftingRecipeId (lower case) of a craft or refine task; CraftAnyRecipe: each recipe counts. */
    std::string recipe;
    bool anyRecipe{false};
    /** InteractTag (lower case): the tag of an interaction of TaskInteract and of the quest objects of the task (0x8261FC0). */
    std::string interactTag;
    /** OnInteractStart: TaskInteract progresses at the start of the interaction, not at its end (0x825E470). */
    bool onInteractStart{false};
    /** UIScreenTag: the screen of TaskOpenUiScreen (message 2617 has the CRC-32 of the lower-case text). */
    std::string screen;
    std::uint32_t screenCrc{0};
};

/** One node of a tree: the task at an index, and the indexes of its children (a container). */
struct QuestNode {
    /** Index into QuestData::tasks(). kNoTask: a hole (a container that the tree built before). */
    std::uint32_t task{0};
    std::vector<std::uint8_t> children;
    /** The index of the container of the node; the root has none. */
    std::optional<std::uint8_t> parent;
    /**
     * The effective StayActive of the node (client 0x3ACDC10: a container inherits it from any child; 0x3B2BA40: a TaskTimer with
     * FailOnCondition has it): a satisfied task with it is CompleteActive (3), not Complete (2) (research quest-display-18a.md Q2, Q3).
     */
    bool stayActive{false};
    /**
     * The target of the node in the client (research quest-display-18a.md Q3): a leaf has its TargetQty (a blank cell: 1), a simple or a
     * consecutive container the number of its children (0x3AD0C60), a partial container 1. A task is satisfied when its progress reaches
     * its target; the progress of a container is the number of its satisfied children (0x3AD6C60, 0x3AD6D20).
     */
    std::uint32_t target{1};
};

inline constexpr std::uint32_t kNoTask = 0xFFFFFFFFU;

/** One objective. */
struct QuestObjective {
    std::string id;
    /** The CRC-32 of the lower-case id: the objective of an active-objective element. */
    std::uint32_t crc{0};
    /** Type (MainStoryQuest, Journey, ...). */
    std::string type;
    /** The value of the objective type of the client (EObjectiveType, table 0x3AA4A00), for the auto-pin flags of message 2709. No value: not known. */
    std::optional<std::uint8_t> typeIndex;
    /** The nodes by index (a hole has task kNoTask). The last node is the root. */
    std::vector<QuestNode> nodes;
    std::uint8_t root{0};
    /** NpcDestinationId, AchievementId, RequiredAchievementId, SuccessGameEventId of the row. */
    std::string destination;
    std::string achievement;
    std::string required;
    std::string successEvent;
    /** ExclusiveTerritory. No value: a blank cell. */
    std::optional<std::uint16_t> exclusiveTerritory;
    bool canAbandon{true};
    std::string title;
};

/** One non-persisted achievement (NPAData): the client computes it from other bits; the server does not store it. */
struct QuestNpa {
    /** ReqAchievementConditional: an expression of achievement ids. */
    std::string conditional;
    /** ReqStatusEffectId: a status effect that the character must have (stage 1.8a has no rule for it: the value is false). */
    std::string status;
};

/** One row of the backstory table with an override (a template of the developers, or an archetype row of the creation). */
struct QuestBackstory {
    std::string id;
    std::string name;
    std::string purpose;
    std::string playtest;
    /** LevelOverride (source text; the template command does not apply it: decision D2 option C). */
    std::string level;
    std::string faction;
    /** ObjectiveUnlockOverride: the start objectives (a note after ":" removed). */
    std::vector<std::string> objectives;
    std::string objectiveText;
    /** AchievementUnlockOverride: the achievements. */
    std::vector<std::string> achievements;
    /** True for an archetype row of the character creation (BackstoryID Archetype_...). */
    bool archetype{false};
};

class QuestData {
public:
    /**
     * Reads the file of the tool (format 1). A file that is not complete is not used, as a whole: a member with a wrong form, a task
     * link with no task row, a child index with no node, two objectives or two tasks with one CRC-32 or one id, an index above 255, two
     * achievements with one bit or one id.
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

    [[nodiscard]] const std::vector<QuestObjective>& objectives() const {
        return m_objectives;
    }
    [[nodiscard]] const std::vector<QuestTask>& tasks() const {
        return m_tasks;
    }
    [[nodiscard]] const QuestTask& task(std::uint32_t index) const {
        return m_tasks[index];
    }
    /** @return The objective of an id (any case), or null. */
    [[nodiscard]] const QuestObjective* objective(std::string_view id) const;
    /** @return The objective of a CRC-32, or null. */
    [[nodiscard]] const QuestObjective* objective_by_crc(std::uint32_t crc) const;
    /** @return The bit (AchievementIndex) of an achievement id (any case), or no value. */
    [[nodiscard]] std::optional<std::uint32_t> achievement_index(std::string_view id) const;
    /** @return The id of the achievement of a bit, or an empty text. */
    [[nodiscard]] const std::string& achievement_id(std::uint32_t index) const;
    /** @return The non-persisted achievement of an id (any case), or null. */
    [[nodiscard]] const QuestNpa* npa(std::string_view id) const;
    /** @return The start objective of a territory (TerritoryDefinition.ObjectiveID) that the data has, or null. */
    [[nodiscard]] const QuestObjective* territory_objective(std::uint16_t territory) const;
    [[nodiscard]] const std::vector<QuestBackstory>& backstories() const {
        return m_backstories;
    }
    /** @return The backstory row of an id (any case), or null. */
    [[nodiscard]] const QuestBackstory* backstory(std::string_view id) const;
    /** @return The number of achievement rows. */
    [[nodiscard]] std::size_t achievement_count() const {
        return m_achievementIds.size();
    }

private:
    bool fail(std::string cause);

    bool m_loaded{false};
    std::string m_error;
    std::vector<QuestObjective> m_objectives;
    std::vector<QuestTask> m_tasks;
    std::vector<QuestBackstory> m_backstories;
    std::unordered_map<std::string, std::size_t> m_objectiveById;
    std::unordered_map<std::uint32_t, std::size_t> m_objectiveByCrc;
    std::unordered_map<std::string, std::uint32_t> m_achievementByName;
    std::unordered_map<std::uint32_t, std::string> m_achievementIds;
    std::unordered_map<std::string, QuestNpa> m_npa;
    std::unordered_map<std::uint16_t, std::size_t> m_territoryObjective;
    std::unordered_map<std::string, std::size_t> m_backstoryById;
};

/** @return The CRC-32 of the lower-case text: the id of an objective or of a task on the wire. */
[[nodiscard]] std::uint32_t quest_crc(std::string_view text);

} // namespace soulwarden::backend::gamedata

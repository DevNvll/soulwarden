#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "gamedata/game_event.h"
#include "gamedata/loot_tables.h"

// The quest data (stage 1.8a, docs/systems/quests.md): the objectives with their compiled task trees, the task rows, the achievement
// table (the bit of each achievement id), the non-persisted achievements (expressions of other bits), the start objectives of the
// territories, the backstories, the game events, the whispers, and the schedules of the game data. Stage 1.8b reads the rewards of the
// objectives and of their success events, and the whispers.
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

/** The task types of the client enum (table 0x3AA6A80; research quest-engine.md Q1) that the engine evaluates (stages 1.8a to 1.8c). */
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
inline constexpr std::uint8_t kConversationState = 16;
inline constexpr std::uint8_t kConversationTopic = 17;
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
/** Stage QG2a: TaskHaveLevel (factory 0x4461150, case 48; research quest-gaps.md section 8). */
inline constexpr std::uint8_t kHaveLevel = 48;
/** Stage QG7: TaskChooseFaction (the factory case of the recruitment JoinFaction tasks; docs/systems/factions.md). */
inline constexpr std::uint8_t kChooseFaction = 81;
inline constexpr std::uint8_t kCutsceneComplete = 49;
inline constexpr std::uint8_t kCinematicStart = 50;
inline constexpr std::uint8_t kCinematicComplete = 51;
inline constexpr std::uint8_t kObjective = 52;
inline constexpr std::uint8_t kOpenUiScreen = 62;
inline constexpr std::uint8_t kTimer = 64;
inline constexpr std::uint8_t kDie = 65;
/**
 * Stage QG6b: PatrolTask (factory 0x4461150, case 66): the client makes a TaskGoToDuration (constructor 0x3B00D70) with the id and the index
 * of the row: the time in a POI of its tags sums across the visits and pauses outside (update 0x3B094A0); its target is the whole seconds of
 * its duration.
 */
inline constexpr std::uint8_t kPatrol = 66;
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

/** The rule of a TaskTriggerArea (TriggerAreaType: Enter 778 rows, Leave 10, Stay Inside 3; a blank cell is Enter). */
enum class AreaRule : std::uint8_t { enter, leave, stayInside };

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
    /** Stage 1.8e: the name of the type in the file ("taskgameevent"; the support manifest). Empty: a file with no name. */
    std::string typeName;
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
    /** Stage QG3: OverrideLocation has a text (the place of the pin of the task: three numbers). The support manifest only. */
    bool pin{false};
    /** ItemDropVC: the vitals categories (a possession task) or the gatherable ids (a gather task) of a quest drop. */
    std::vector<std::string> dropSources;
    /** ItemDropProbability: 0 to 100 (a blank cell: 0). */
    double dropChance{0.0};
    /**
     * Stage QG5a: ChestDropProbability, 0 to 100 (a blank cell: 0). The chance of the quest item at a successful open of a chest
     * ("0-100, probability to drop items for loot chests/POI loot": client 0x825F500; item task field +0x6A4, reflection 0x3B0AAF0).
     */
    double chestChance{0.0};
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
    // Stage 1.8b: the cells of the tasks of a person, a trigger area, a cutscene, a cinematic, and a game event of a task.
    /** TP_TargetTag (lower case): the tag of the trigger volume of a TaskTriggerArea (tools/gamedata/triggers.py). */
    std::string targetTag;
    /** TriggerAreaType of a TaskTriggerArea. */
    AreaRule areaRule{AreaRule::enter};
    /**
     * The CRC-32 of the lower-case ItemName: the cutscene of TaskCutsceneComplete (message 6703, 6706: 4817DD08 for
     * 06_GetOffBeach_MSQ_RevivalShoreKnight_WW_IGC; research conversations.md) and the cinematic of TaskCinematicStart and
     * TaskCinematicComplete (6606, 6542). 0 for an empty ItemName.
     */
    std::uint32_t nameCrc{0};
    /**
     * Stage QG2a: ShouldCheckPoints of TaskHaveLevel (constructor 0x3B01C80, +0x554): with an ItemName (a categorical progression, +0x558) the
     * points of the progression (true) or its rank (false); with a blank ItemName the level of the character (activation 0x3B04F40).
     */
    bool checkPoints{false};
};

/** The faction mask of a blank RequiredFaction (and of None): bit 0 bypasses the test (client parser 0x445CA30, eligibility 0x3A4C166). */
inline constexpr std::uint8_t kAnyFactionMask = 0x0F;

/** @return The mask of a RequiredFaction text (client parser 0x445CA30: one leading "!", the names Syndicate, Marauder, Covenant, Any). */
[[nodiscard]] std::uint8_t faction_mask(std::string_view text);

/** The kinds of the schedule rows of the data that the backend supports for a repeat (research quest-engine.md Q5). */
enum class ScheduleKind : std::uint8_t {
    /** No schedule row, or a row with dates (a seasonal window: decision D4 of the user is open). */
    none,
    /** StartDate *-*-*T*:00:00: each whole hour (UTC). */
    hourly,
    /** StartDate *-*-*TDailyCooldown: the daily reset (shipped default 05:00 UTC, client 0x616C670). */
    daily,
    /** StartWeekday WeeklyCooldown: the weekly reset (shipped default Tuesday 05:00 UTC, client 0x616C670). */
    weekly,
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
    /** Stage 1.8e: the path of the source table (the support manifest: the rows of the developer worlds). Empty: a file with no path. */
    std::string table;
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
    // Stage 1.8b: the rewards of the row (research quest-engine.md Q6). A blank cell has no value.
    /** ItemRewardName and ItemRewardQty (a blank quantity: 1; the loader 0x445CA30 reads a signed quantity, the data has none below 0). */
    std::string itemReward;
    std::int64_t itemRewardQty{1};
    /** ExperienceReward: the experience of the row (added to the event; no row of the beach chain has it). */
    std::uint64_t experienceReward{0};
    /** ProgressionReward (a categorical progression: faction tokens or reputation; a later stage) and ObjectiveReward (the next objective). */
    std::string progressionReward;
    std::string objectiveReward;
    /** Stage QG7: ShouldResetOnFactionChange (the faction trials and rank-up rows: "1"); docs/systems/factions.md. */
    bool resetOnFactionChange{false};
    // Stage 1.8d: the availability of the row (the eligibility of the client 0x3A4BFE0) and its repeat. A blank cell has no rule.
    /** RequiredLevel: the level of the screen (the level index plus 1, client 0x3A4C13E). 0: none. */
    std::uint32_t requiredLevel{0};
    /** RequiredFaction: the mask of the parser 0x445CA30 (bit 0 bypasses the test; bit N + 1 is the faction N). Blank: 0xF. */
    std::uint8_t factionMask{kAnyFactionMask};
    /**
     * QuestArcTag (row +0x20): a group key of the QuestArc meta-achievements, not a gate (stage QG2a: the eligibility 0x3A4BFE0 does not read
     * it; research quest-gaps-qg2b.md section 1). RequiredWorldTags, RequiredGameModeId (row +0x34, IsInGameMode 0x32BA3B0), and
     * RequiredProgression: gates whose service rule the backend does not have.
     */
    std::string questArc;
    std::string worldTags;
    std::string gameMode;
    std::string requiredProgression;
    /** SkipReqAchievmentWhenInvalid, PushObjectiveOnReqMet, PushObjectiveOnReqRetrigger (loader 0x445CA30 +0x36A, +0x368, +0x369). */
    bool skipInvalidRequirement{false};
    bool pushOnRequirement{false};
    bool pushRetrigger{false};
    /** Repeatable (1, true, TRUE: research Q6 census) and ScheduleId (Hourly, Daily, Weekly, a seasonal row). */
    bool repeatable{false};
    std::string schedule;
    /** ShouldResetInProgressQuestOnScheduleEnd (loader 0x445CE86). */
    bool resetOnScheduleEnd{false};
};

/** One whisper (WhisperData): the voice and the subtitles of the client (message 5070 with the CRC-32 of the lower-case id). */
struct QuestWhisper {
    std::string id;
    std::uint32_t crc{0};
    /** GameEvent: the event that plays it; AchievementId: the bit that the client and the data use for a whisper that played. */
    std::string gameEvent;
    std::string achievement;
};

/**
 * Stage QG5b: one clause of an ObjectiveFlow (the record of 12 bytes of the client parser 0x43BD930: the unlock byte, the modifier byte,
 * the objective hash, the task hash).
 */
struct QuestFlowClause {
    /** `u` (true: the clause unlocks) or `l` (false: it locks). */
    bool unlock{true};
    /**
     * The modifier: 'a' (no suffix, or a modifier that the parser does not know), 'c' (complete), 'r' (ready for completion; only with no
     * task: a task takes only a and c, 0x43BDB6C).
     */
    char modifier{'a'};
    /** The objective id and the task id (empty: the clause names the objective only), as the text of the data. */
    std::string objective;
    std::string task;
};

/**
 * Stage QG5b: @return The clauses of an ObjectiveFlow text in their order (the client parser 0x43BD930: the clauses are split at ",", a
 * clause is `u:` or `l:`, then OBJECTIVE, `/TASK`, and `:MODIFIER`; a clause with another prefix is left out, 0x43BE66E).
 */
[[nodiscard]] std::vector<QuestFlowClause> parse_objective_flow(const std::string& text);

/** One non-persisted achievement (NPAData): the client computes it from other bits; the server does not store it. */
struct QuestNpa {
    /** ReqAchievementConditional: an expression of achievement ids. */
    std::string conditional;
    /** ReqStatusEffectId: a status effect that the character must have (stage 1.8a has no rule for it: the value is false). */
    std::string status;
    /**
     * Code review of stage QG4, finding QG4R-R2: ObjectiveFlow, the rule of the quest state of the NPA ("u:Quest_18018,l:Quest_18018/
     * Quest_18018_Task_5:c"). Stage QG5b: its clauses (`flowClauses`) with the rule of the client (evaluator 0x318A4F0, clause matcher
     * 0x3189DA0; Quests::flow_passes): the NPA is true when its conditional (if any), its flow (if any), and its status (if any) pass.
     */
    std::string flow;
    std::vector<QuestFlowClause> flowClauses;
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

class MissionData;
struct MissionRow;

/**
 * Stage QG6a (docs/systems/mission-boards.md): the binding of one mission row to its template (QuestData::bind_missions). The client builds
 * the tree of the template with the values of the row (factory 0x4461150; only for a valid row with IsCustomObjective false: row +0x143,
 * +0x144): TaskKillContribution gets TaskKillContributionOverride as its category and TaskKillContributionQtyOverride as its TargetQty
 * (case 13). The server builds the same effective objective.
 */
struct MissionBinding {
    const MissionRow* row{nullptr};
    /** The template of the row (ObjectiveID). Null: the quest data has no such objective. */
    const QuestObjective* templateObjective{nullptr};
    /** The effective objective: a variant with the values of the row, or the template when no value of the row changes its tasks. */
    const QuestObjective* objective{nullptr};
    /** The causes why the backend cannot run the mission (an override with no rule, a missing template). Empty: it can. */
    std::vector<std::string> refusals;
};

/** Stage QG6b: the effective objective of one mission row (QuestData::apply_overrides). */
struct MissionOverrides {
    /** The template with the changed leaves (no value: no cell of the row changes a task of the template). */
    std::optional<QuestObjective> variant;
    std::vector<std::string> refusals;
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
    /**
     * Stage QG6a: @return The objective of an instance: the effective objective of its mission (`mission`, a missionId) when the binding of
     *         the mission has a variant of this template, else the template of the CRC-32 (a quest that is not a mission: `mission` 0).
     */
    [[nodiscard]] const QuestObjective* objective_by_crc(std::uint32_t crc, std::uint32_t mission) const;
    /**
     * Stage QG6a: binds the mission rows to their templates (after load; a second call replaces the bindings): the effective objective of
     * each row (a variant for a row whose kill override changes a TaskKillContribution leaf; its task rows are copies with the values of the
     * row), the refusals of the overrides with no rule, and the game events of the missions that the quest data does not have.
     */
    void bind_missions(const MissionData& missions);
    /** Stage QG6a: @return The binding of a missionId, or null (no mission data, or no such mission). */
    [[nodiscard]] const MissionBinding* mission(std::uint32_t missionCrc) const;
    /** Stage QG6b: @return The target of a patrol leaf (TaskGoToDuration 0x3B00DB1): the whole seconds of its duration; 0 seconds: 1. */
    [[nodiscard]] static std::uint32_t patrol_target(const QuestTask& task);
    /** Stage QG6a: @return The number of the bound missions and of the variants (a log line). */
    [[nodiscard]] std::size_t mission_count() const {
        return m_missions.size();
    }
    [[nodiscard]] std::size_t variant_count() const {
        return m_variants.size();
    }
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
    /** Stage 1.8b: @return The game event of an id (any case), or null. */
    [[nodiscard]] const QuestGameEvent* game_event(std::string_view id) const;
    /** Stage 1.8b: @return The whisper of a game event (the WhisperId of the event, else a whisper whose GameEvent is the event), or null. */
    [[nodiscard]] const QuestWhisper* whisper_of_event(std::string_view eventId) const;
    /** Stage 1.8b: @return The whisper of an id (any case), or null. */
    [[nodiscard]] const QuestWhisper* whisper(std::string_view id) const;
    /** @return The number of achievement rows. */
    [[nodiscard]] std::size_t achievement_count() const {
        return m_achievementIds.size();
    }
    /** Stage 1.8d: @return The kind of a schedule row (ScheduleId, any case); `none` for an unknown id or a row of dates. */
    [[nodiscard]] ScheduleKind schedule_kind(std::string_view id) const;

private:
    bool fail(std::string cause);
    /** Stage QG6b: the effective objective of a mission row (the rules of the task factory 0x4461150; FACTORY.md). */
    [[nodiscard]] MissionOverrides apply_overrides(const MissionRow& row, const QuestObjective& base);
    /** @return True when two tasks have the same values of each cell that a mission override changes. */
    [[nodiscard]] static bool same_effect(const QuestTask& left, const QuestTask& right);

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
    std::unordered_map<std::string, QuestGameEvent> m_events;
    std::vector<QuestWhisper> m_whispers;
    std::unordered_map<std::string, std::size_t> m_whisperById;
    std::unordered_map<std::string, std::size_t> m_whisperByEvent;
    std::unordered_map<std::string, ScheduleKind> m_schedules;
    /** Stage QG6a: the bindings of the missions, the variants (stable addresses), the variant of each (mission, template), and the number
     *  of the task rows of the file (the copies of the variants come after them). */
    std::unordered_map<std::uint32_t, MissionBinding> m_missions;
    std::deque<QuestObjective> m_variants;
    std::size_t m_fileTasks{0};
    std::vector<std::string> m_missionEvents;
};

/** @return The CRC-32 of the lower-case text: the id of an objective or of a task on the wire. */
[[nodiscard]] std::uint32_t quest_crc(std::string_view text);

} // namespace soulwarden::backend::gamedata

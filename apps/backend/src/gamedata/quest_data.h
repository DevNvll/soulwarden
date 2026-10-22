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
inline constexpr std::uint8_t kCutsceneComplete = 49;
inline constexpr std::uint8_t kCinematicStart = 50;
inline constexpr std::uint8_t kCinematicComplete = 51;
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
    // Stage 1.8d: the availability of the row (the eligibility of the client 0x3A4BFE0) and its repeat. A blank cell has no rule.
    /** RequiredLevel: the level of the screen (the level index plus 1, client 0x3A4C13E). 0: none. */
    std::uint32_t requiredLevel{0};
    /** RequiredFaction: the mask of the parser 0x445CA30 (bit 0 bypasses the test; bit N + 1 is the faction N). Blank: 0xF. */
    std::uint8_t factionMask{kAnyFactionMask};
    /** QuestArcTag, RequiredWorldTags, RequiredGameModeId, RequiredProgression: gates whose service rule the backend does not have. */
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

/**
 * One game event (GameEventData; research quest-engine.md Q6, quest-engine-3.md Q3): the reward of a success event of an objective, and the
 * whisper of an event of a TaskTriggerGameEvent. The cells keep the source text; the loader reads the ones of the engine.
 */
struct QuestGameEvent {
    std::string id;
    /** UniversalExpAmount and CurrencyReward (the units of the wallet: 150 is 1.50 coin). */
    std::uint64_t experience{0};
    std::uint64_t coin{0};
    /** CoinRewardChance: the loader of the client sets 1 when the cell is blank (0x31E70B0, row +0x38). */
    double coinChance{1.0};
    /**
     * ItemReward and ItemRewardQty (a blank quantity: 1, the client loader 0x31E7129; an explicit 0 stays 0). The quantity keeps its sign
     * (code review of stage 1.8b, finding QR8B-R10: a negative quantity is a removal with no rule, and the hand-in refuses it).
     */
    std::string item;
    std::int64_t itemQty{1};
    /**
     * Stage 1.8d: GearScoreRange (client parser 0x31E43A0: the text split at "-", one number is the smallest and the largest value). The
     * gear score of the item of the event. No value: a blank cell.
     */
    std::optional<std::pair<std::uint16_t, std::uint16_t>> gearScoreRange;
    /** Stage 1.8d: TerritoryStanding: the standing of the origin territory of the quest instance (decision D3 option B). */
    std::uint64_t standing{0};
    /**
     * Stage 1.8e: AzothReward: points of the Azoth balance (the categorical progression Azoth_Currency of stage 3.7c, research
     * quest-engine-3.md Q4). Only with AzothRewardChance 1: the data has no other chance with an amount (672 events have 1; 4 mission
     * events have a blank cell, and they stay unpaid: the default of the loader 0x31E70B0 for this column is not read).
     */
    std::uint64_t azoth{0};
    /** AchievementId: an achievement of the event. */
    std::string achievement;
    /** WhisperId or WhisperID: the whisper of the event (166 of 170 TaskTriggerGameEvent rows name an event with one). */
    std::string whisper;
    /**
     * The rewards of the event that the backend cannot pay yet (stage 1.8d and later: FactionReputation, FactionTokens, a categorical
     * progression, an Azoth reward with a chance that is not 1, the other currencies, an entitlement, a status effect, a TerritoryStanding
     * or a GearScoreRange that is not a whole number): "COLUMN VALUE" for each cell with a value that is not 0.
     */
    std::vector<std::string> unpaid;
};

/** One whisper (WhisperData): the voice and the subtitles of the client (message 5070 with the CRC-32 of the lower-case id). */
struct QuestWhisper {
    std::string id;
    std::uint32_t crc{0};
    /** GameEvent: the event that plays it; AchievementId: the bit that the client and the data use for a whisper that played. */
    std::string gameEvent;
    std::string achievement;
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
};

/** @return The CRC-32 of the lower-case text: the id of an objective or of a task on the wire. */
[[nodiscard]] std::uint32_t quest_crc(std::string_view text);

} // namespace soulwarden::backend::gamedata

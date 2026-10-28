#include "world/quests.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <iterator>

#include "core/logging/log.h"
#include "gamedata/crafting_data.h"
#include "gamedata/item_catalog.h"
#include "gamedata/progression_tables.h"
#include "world/inventory.h"
#include "world/skills.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

using gamedata::QuestObjective;
using gamedata::QuestTask;
using gamedata::lower_text;
namespace task_type = gamedata::task_type;

/** The first instance id of a database with no quest (an instance id is never 0). */
constexpr std::uint64_t kFirstInstance = 1;
/** The largest number of task activations and completions of one plan: a loop of achievement actions stops the plan. */
constexpr std::uint64_t kStepLimit = 100000;
/** The largest number of rounds of the TaskAchievement evaluator in one plan. */
constexpr int kSettleRounds = 64;
/** The depth of non-persisted achievements in one conditional (an NPA can name an NPA). */
constexpr int kNpaDepth = 8;
/** The auto-pin flag of the type CommunityGoal (6) is 0 in the default options of the client (research quests.md 3.4, run r056). */
constexpr std::uint8_t kCommunityGoalType = 6;
/** The number of result ids that the quest system keeps (a repeated delivery of a result counts one time; the store has a bound). */
constexpr std::size_t kResultWindow = 4096;
/**
 * The retry of an event whose plan the store refused (code review of stage 1.8c, finding QR8C-R1): after 2 s on the world clock (the time of
 * the retry of a timer, finding QR8A-R5), 5 times at most (a decision of an agent: a store that refuses for 10 s has a fault that the log
 * shows; the event does not loop).
 */
constexpr std::chrono::milliseconds kEventRetry{2000};
constexpr std::uint32_t kEventRetries = 5;
/** A timer whose completion write the store refused tries again after this time (code review of stage 1.8a, finding QR8A-R5). */
constexpr std::chrono::milliseconds kTimerRetry{2000};

bool done(std::uint8_t state) {
    return state == quest_state::kComplete || state == quest_state::kCompleteActive;
}

/** @return The text with no blank at its start and its end. */
std::string trimmed_text(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

/** Active (1) or CompleteActive (3): the activity predicate of the client (0x3AD5B80). */
bool running(std::uint8_t state) {
    return state == quest_state::kActive || state == quest_state::kCompleteActive;
}

bool is_counter(std::uint8_t type) {
    return type == task_type::kKillContribution || type == task_type::kGameEvent || type == task_type::kRequireGatherItems
           || type == task_type::kCraftRecipe || type == task_type::kCraftMultipleRecipes || type == task_type::kRefineRecipe
           || type == task_type::kOpenUiScreen || type == task_type::kInteract || type == task_type::kCutsceneComplete
           || type == task_type::kCinematicStart || type == task_type::kCinematicComplete || type == task_type::kConversationTopic
           || type == task_type::kConversationState;
}

/** Stage 1.8c: the tasks of a place; stage 1.8b: TaskTriggerArea (a volume of the trigger data and the accepted position). */
bool is_place(std::uint8_t type) {
    return type == task_type::kGoToPoi || type == task_type::kGoToPoiOnTerritory || type == task_type::kExitPoi || type == task_type::kTriggerArea;
}

/** The completed notice keeps the newest instances (field 2 of the Objectives state; a decision of an agent: a bounded list). */
constexpr std::size_t kCompletedNotices = 16;

/** @return The key of the index of a task of a person ("NPC|TOPIC" or "NPC|STATE", lower case). */
std::string person_key(std::string_view npc, std::string_view what) {
    return gamedata::lower_text(npc) + "|" + gamedata::lower_text(what);
}

/** @return The key of a result for one character and one kind of event (a kill gives its credit list one event each). */
std::uint64_t result_key(std::uint64_t result, EntityId character, QuestEventKind kind) {
    std::uint64_t key = result * 0x9E3779B97F4A7C15ULL;
    key ^= character + 0x632BE59BD9B4E019ULL + (key << 6) + (key >> 2);
    key ^= static_cast<std::uint64_t>(kind) * 0xBF58476D1CE4E5B9ULL;
    return key == 0 ? 1 : key;
}

std::string hex(std::uint64_t value) {
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
}

/** The faction of a character with no faction (the client enum: 0 unaligned, 1 Syndicate, 2 Marauder, 3 Covenant; roadmap 4.5 adds it). */
constexpr std::uint8_t kNoFaction = 0;

/** @return The faction test of the eligibility (0x3A4C166): bit 0 of the mask bypasses it, else the bit of the faction. */
bool faction_allowed(std::uint8_t mask, std::uint8_t faction) {
    return (mask & 0x1U) != 0 || ((mask >> faction) & 0x1U) != 0;
}

constexpr std::uint64_t kNsPerSecond = 1000000000ULL;
constexpr std::uint64_t kHourSeconds = 3600;
constexpr std::uint64_t kDaySeconds = 86400;
/**
 * The shipped reset defaults of the client (registration 0x616C670, instructions 0x6181D4B to 0x6181F87; research quest-engine.md Q5): the
 * daily reset at 05:00 UTC, the weekly reset on the weekday 2 (Tuesday, the gmtime index of 0x5C45A50) at 05:00 UTC. The backend has no
 * region offset (zero offset: a decision of an agent, the research names no service override).
 */
constexpr std::uint64_t kResetSecondOfDay = 5 * kHourSeconds;
constexpr std::uint64_t kResetWeekday = 2;

/**
 * @return The start of the next cycle of a schedule after a completion (Unix nanoseconds), or 0 for a kind with no cycle. A completion at the
 *         start of a cycle belongs to that cycle (research quests-18d rules Q4: "completionTime >= currentCycleStart blocks").
 */
std::uint64_t next_cycle(gamedata::ScheduleKind kind, std::uint64_t completedNs) {
    const std::uint64_t seconds = completedNs / kNsPerSecond;
    switch (kind) {
    case gamedata::ScheduleKind::hourly:
        return (seconds / kHourSeconds + 1) * kHourSeconds * kNsPerSecond;
    case gamedata::ScheduleKind::daily: {
        if (seconds < kResetSecondOfDay) {
            return kResetSecondOfDay * kNsPerSecond;
        }
        const std::uint64_t start = (seconds - kResetSecondOfDay) / kDaySeconds * kDaySeconds + kResetSecondOfDay;
        return (start + kDaySeconds) * kNsPerSecond;
    }
    case gamedata::ScheduleKind::weekly: {
        if (seconds < kResetSecondOfDay) {
            return 0; // Before the first reset of the Unix time: no real completion.
        }
        const std::uint64_t day = (seconds - kResetSecondOfDay) / kDaySeconds;
        const std::uint64_t weekday = (day + 4) % 7; // 1970-01-01 is a Thursday (gmtime weekday 4).
        const std::uint64_t sinceReset = (weekday + 7 - kResetWeekday) % 7;
        const std::uint64_t start = (day - std::min(day, sinceReset)) * kDaySeconds + kResetSecondOfDay;
        return (start + 7 * kDaySeconds) * kNsPerSecond;
    }
    case gamedata::ScheduleKind::none:
        break;
    }
    return 0;
}

} // namespace

const char* quest_error_text(QuestError error) {
    switch (error) {
    case QuestError::none:
        return "done";
    case QuestError::noData:
        return "the backend has no quest data (tools/gamedata/quests.sh)";
    case QuestError::unknownCharacter:
        return "the store has no such character";
    case QuestError::unknownObjective:
        return "the quest data has no such objective";
    case QuestError::alreadyActive:
        return "the character has this objective";
    case QuestError::unknownTemplate:
        return "the quest data has no such backstory template";
    case QuestError::storeFailed:
        return "the database did not take the change: nothing changed";
    case QuestError::noFixedPoint:
        return "the task rules did not end (a loop of achievement actions): nothing changed";
    case QuestError::unknownInstance:
        return "the character has no active quest with this instance";
    case QuestError::notAbandonable:
        return "the objective cannot be abandoned (CanBeAbandoned 0)";
    case QuestError::notInWorld:
        return "the character is not in the world";
    case QuestError::notReady:
        return "the root task of the objective is not complete";
    case QuestError::cannotPay:
        return "the objective has a reward that the backend cannot pay yet: no change";
    case QuestError::bagFull:
        return "the bag has no place for the reward items (decision D5 option A): no change";
    case QuestError::notAvailable:
        return "the RequiredAchievementId of the objective is false for the character";
    case QuestError::completedBefore:
        return "the character completed this objective before";
    case QuestError::levelTooLow:
        return "the level of the character is below the RequiredLevel of the objective";
    case QuestError::wrongFaction:
        return "the RequiredFaction of the objective does not permit the faction of the character";
    case QuestError::unsupportedGate:
        return "the objective has a gate whose rule the backend does not have (RequiredWorldTags, RequiredGameModeId, a progression)";
    case QuestError::onCooldown:
        return "the character completed this repeatable objective in the current cycle of its schedule";
    case QuestError::missingReturnItems:
        return "a return task of the objective does not have its items in the bag now";
    }
    return "?";
}

/** One indexed task: the instance and the task index. */
namespace {

/**
 * Stage QG5b: @return True when the ObjectiveFlow of an NPA passes for a character (static read A2 of stage QG5b: the evaluator 0x318A4F0 and
 * the clause matcher 0x3189DA0 of ObjectivesComponentRequestBus). The clauses are ordered overrides: from the last clause to the first, the
 * first clause that matches decides (a `u` passes, an `l` fails, 0x318A5B1 to 0x318A6A6); no match fails, and a flow with no clause passes.
 * A clause matches when its objective is complete (IsObjectiveComplete 0x3A48A30: the completion achievement of the row), else: OBJECTIVE
 * and OBJECTIVE:a when the character has an instance of it (HasObjective 0x3A46DB0), OBJECTIVE:c never, OBJECTIVE:r when the instance is ready
 * for completion (0x3A48D50: the root is done), OBJECTIVE/TASK and OBJECTIVE/TASK:a when the task element is Active, Complete, or
 * CompleteActive (0x3A49520, 0x3A49640: states 1, 2, 3), OBJECTIVE/TASK:c when it is Complete or CompleteActive (2, 3). An objective that
 * the data does not have matches nothing (0x3189DCA).
 * @param completed `completed(objective)`: the completion achievement of the objective is set for the character.
 * @param instance_of `instance_of(crc)`: the active instance of the objective of the character, or null.
 */
template <class Completed, class InstanceOf>
bool flow_passes(const gamedata::QuestData& data, const std::vector<gamedata::QuestFlowClause>& clauses, Completed&& completed,
                 InstanceOf&& instance_of) {
    if (clauses.empty()) {
        return true;
    }
    for (auto clause = clauses.rbegin(); clause != clauses.rend(); ++clause) {
        const QuestObjective* objective = data.objective(clause->objective);
        if (objective == nullptr) {
            continue;
        }
        bool match = completed(*objective);
        if (!match) {
            const QuestInstance* quest = instance_of(objective->crc);
            if (quest != nullptr && clause->task.empty()) {
                match = clause->modifier == 'a'
                        || (clause->modifier == 'r' && objective->root < quest->tasks.size() && (quest->tasks[objective->root].state == quest_state::kComplete
                                                                                            || quest->tasks[objective->root].state == quest_state::kCompleteActive));
            } else if (quest != nullptr) {
                const std::string wanted = lower_text(clause->task);
                for (std::size_t index = 0; index < objective->nodes.size() && index < quest->tasks.size(); ++index) {
                    if (objective->nodes[index].task == gamedata::kNoTask || lower_text(data.task(objective->nodes[index].task).id) != wanted) {
                        continue;
                    }
                    const std::uint8_t state = quest->tasks[index].state;
                    const bool complete = state == quest_state::kComplete || state == quest_state::kCompleteActive;
                    match = clause->modifier == 'c' ? complete : complete || state == quest_state::kActive;
                    break;
                }
            }
        }
        if (match) {
            return clause->unlock;
        }
    }
    return false;
}

/**
 * The value of an achievement conditional (client 0x31898D0, 0x3A8C640): ids with !, &&, ||, and parentheses. `value(id)` gives the value of
 * one id (no value: the id is malformed, an NPA with a malformed expression). One reader for the quest plans and for the persons (stage
 * 2.5c). Code review of stage 2.5c, finding PR5C-R7: an expression that the reader cannot read to its end (a missing operand, a missing
 * closing parenthesis, text after the end) has no value, also under a negation: the caller takes it as false (a malformed requirement does
 * not pass).
 */
template <class IdValue>
std::optional<bool> evaluate_conditional(const std::string& text, IdValue&& value) {
    struct Reader {
        const std::string& text;
        std::size_t position{0};
        IdValue& value;
        bool error{false};

        void blank() {
            while (position < text.size() && (text[position] == ' ' || text[position] == '\t')) {
                ++position;
            }
        }
        bool take(const char* token) {
            blank();
            const std::size_t length = std::char_traits<char>::length(token);
            if (text.compare(position, length, token) == 0) {
                position += length;
                return true;
            }
            return false;
        }
        bool any() {
            bool result = all();
            while (take("||")) {
                const bool right = all();
                result = result || right;
            }
            return result;
        }
        bool all() {
            bool result = unary();
            while (take("&&")) {
                const bool right = unary();
                result = result && right;
            }
            return result;
        }
        bool unary() {
            if (take("!")) {
                return !unary();
            }
            if (take("(")) {
                const bool result = any();
                if (!take(")")) {
                    error = true;
                }
                return result;
            }
            blank();
            const std::size_t start = position;
            while (position < text.size() && text[position] != ' ' && text[position] != '\t' && text[position] != '&'
                   && text[position] != '|' && text[position] != '(' && text[position] != ')' && text[position] != '!') {
                ++position;
            }
            if (position == start) {
                error = true; // A missing operand.
                return false;
            }
            const std::optional<bool> one = value(text.substr(start, position - start));
            if (!one) {
                error = true;
                return false;
            }
            return *one;
        }
    };
    Reader reader{text, 0, value};
    const bool result = reader.any();
    reader.blank();
    if (reader.error || reader.position != text.size()) {
        return std::nullopt;
    }
    return result;
}

/**
 * Stage QG2a: @return The ids of an achievement conditional, and of the non-persisted achievements that it names (their conditionals, to
 * the depth of the reader). The reader of evaluate_conditional visits each operand.
 */
void conditional_ids(const gamedata::QuestData& data, const std::string& text, int depth, std::set<std::string>& ids) {
    if (text.empty() || depth > kNpaDepth) {
        return;
    }
    (void)evaluate_conditional(text, [&](const std::string& id) -> std::optional<bool> {
        if (ids.insert(lower_text(id)).second) {
            if (const gamedata::QuestNpa* npa = data.npa(id); npa != nullptr && !npa->conditional.empty()) {
                conditional_ids(data, npa->conditional, depth + 1, ids);
            }
        }
        return false;
    });
}

/**
 * Stage QG2a, decision B of the orchestrator (2026-10-28; the user can change it: TASKS.md "Waiting for the user"): the PushObjectiveOnReqMet
 * rows that the push rule does not grant: the old start of the game (01_SoleSurvivor2: its condition is true for each new character, and the
 * start of a new character is the Windsward beach, decision D1 of the user) and the two seasonal starts behind the bit debug_1 (research
 * quest-gaps.md section 8).
 */
constexpr std::array<const char*, 3> kPushExcluded{"01_SoleSurvivor2", "Quest_03189", "Quest_03190"};

} // namespace

struct Subscriber {
    std::uint64_t instance{0};
    std::uint8_t index{0};
    bool operator==(const Subscriber& other) const {
        return instance == other.instance && index == other.index;
    }
};

/**
 * The index of the active tasks of one character by the key of their event (stage 1.8c; code review of stage 1.8a, finding QR8A-R7). A key
 * is a kind and a value: "k:" a vitals category (a kill), "e:" a game event, "g:" an item of a gather, "c:" a recipe (a craft; "c:*" any
 * recipe), "r:" a recipe of a refine, "s:" a screen CRC, "i:" a tag of TaskInteract, "t:" a tag of a quest object, "kd:" and "gd:" a source
 * of a quest drop, "cd" a task with a quest drop of a chest (stage QG5a), "items" the possession and equip tasks, "place" the tasks of a
 * place, "ach" TaskAchievement, "die" TaskDie, "level" TaskHaveLevel of the level of the character (stage QG2a).
 */
struct Quests::Subscriptions {
    std::unordered_map<std::string, std::vector<Subscriber>> byKey;
    std::unordered_map<std::uint64_t, std::vector<std::string>> keysOf;
    std::size_t total{0};
    /** The place of each active instance in the list of the entity (code review of stage 1.8c, finding QR8C-R11: no walk to find it). */
    std::unordered_map<std::uint64_t, std::size_t> positions;

    void remove(std::uint64_t instance) {
        const auto found = keysOf.find(instance);
        if (found == keysOf.end()) {
            return;
        }
        for (const std::string& key : found->second) {
            std::vector<Subscriber>& list = byKey[key];
            const auto before = list.size();
            list.erase(std::remove_if(list.begin(), list.end(), [&](const Subscriber& entry) { return entry.instance == instance; }), list.end());
            total -= before - list.size();
            if (list.empty()) {
                byKey.erase(key);
            }
        }
        keysOf.erase(found);
    }
    void add(const std::string& key, Subscriber entry) {
        std::vector<Subscriber>& list = byKey[key];
        if (std::find(list.begin(), list.end(), entry) != list.end()) {
            return;
        }
        list.push_back(entry);
        ++total;
        std::vector<std::string>& keys = keysOf[entry.instance];
        if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
            keys.push_back(key);
        }
    }
    [[nodiscard]] const std::vector<Subscriber>* find(const std::string& key) const {
        const auto found = byKey.find(key);
        return found != byKey.end() ? &found->second : nullptr;
    }
};

/**
 * Stage QG2a: the index of the push rows of PushObjectiveOnReqMet (decision B: 18 rows of the build) by what their availability depends on:
 * the achievement bits of RequiredAchievementId (with its non-persisted achievements) and of the completion achievement, and RequiredLevel.
 * A change of a character visits only the rows of its changed bits and of its new levels (MP-8).
 */
struct Quests::PushIndex {
    /** The push rows (indexes of QuestData::objectives), in data order. */
    std::vector<std::uint32_t> rows;
    /** The rows of each watched bit. */
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> byBit;
    /** The watched bits, sorted. */
    std::vector<std::uint32_t> bits;
    /** RequiredLevel (above 1) and the row, sorted by the level. */
    std::vector<std::pair<std::uint32_t, std::uint32_t>> byLevel;
    /** The objectives of the data that the index has (a new load of the quest data builds the index again). */
    const QuestObjective* source{nullptr};
    std::size_t count{0};
};

/**
 * One change of the quests of one character: copies of the quests that the change touches (copy on write: the other quests stay in the
 * entity), the changed achievement bits, and the rows and the item actions that the change writes. Nothing changes in the world before
 * Quests::commit.
 */
struct Quests::Plan {
    Plan(Quests& owner, const Entity& entity, std::uint64_t now)
        : owner(owner), data(*owner.m_data), ownership(owner.m_world.ownership()), counts(owner.m_counts), entity(entity),
          tracked(entity.quests.tracked), nowNs(now) {}

    Quests& owner;
    const gamedata::QuestData& data;
    const Ownership& ownership;
    QuestCounts& counts;
    const Entity& entity;
    /** The copies of the quests that the plan touched, the new instances, the ended instances. */
    std::map<std::uint64_t, QuestInstance> copies;
    std::vector<std::uint64_t> added;
    std::set<std::uint64_t> removed;
    std::set<std::uint64_t> changed;
    std::vector<std::uint64_t> tracked;
    bool trackedChanged{false};
    /** The bits that the plan changed (true: set). */
    std::map<std::uint32_t, bool> bits;
    /** The achievement actions in their order: the id and true for an unlock, false for a lock. */
    std::vector<std::pair<std::string, bool>> actions;
    std::vector<state::QuestUndoRow> undo;
    struct TimerStart {
        std::uint64_t instance;
        std::uint8_t index;
        std::uint64_t startNs;
        double seconds;
    };
    std::vector<TimerStart> timers;
    /**
     * The item actions of the plan (TaskGiveItem, TaskGiveAndTakeItem, TaskRemoveItem, the returns of an end) in the order of the tasks (code
     * review of stage 1.8c, finding QR8C-R2: a remove after a give of the same plan takes the given item), and their sum by item.
     */
    std::vector<QuestItemAction> itemActions;
    std::map<std::string, std::int64_t> itemDelta;
    /** The award rows that the plan writes (the key: objective CRC and task index) and the objectives whose rows go. */
    std::map<std::pair<std::uint32_t, std::uint8_t>, std::uint32_t> awards;
    std::set<std::string> awardDeletes;
    /** The TaskAchievement tasks that the plan activated (the waits that the index of the character does not have yet). */
    std::vector<Subscriber> waits;
    /** The instances that failed in the plan: they end after the step (end_instance). */
    std::vector<std::uint64_t> failed;
    std::vector<std::uint64_t> resetOnDeath;
    std::vector<std::string> notes;
    /** Stage 1.8b: the whispers of the game events of the plan (5070 after the commit), and the instances that a hand-in completed. */
    std::vector<std::uint32_t> whispers;
    std::vector<std::uint64_t> completed;
    /** Stage 1.8b: the experience and the coin (the units of the wallet) of the plan (a hand-in, the game event of a task). */
    std::uint64_t experience{0};
    std::uint64_t coin{0};
    /** Stage 1.8d: the territory standing of the plan (the territory and the points; decision D3 option B). */
    std::vector<std::pair<std::uint16_t, std::uint64_t>> standing;
    /** Stage 1.8e: the points of the Azoth balance of the plan (AzothReward) and the cap of the balance. */
    std::uint64_t azoth{0};
    std::uint64_t azothCap{0};
    /** Stage QG5b: the categorical progression experience of the plan (a trade skill id of the tables and the experience). */
    std::vector<std::pair<std::string, std::uint64_t>> progressions;
    /** Stage 1.8d: the repeat rows that the plan writes (a completion of a repeatable objective with a schedule) and removes (a reset). */
    std::vector<state::QuestRepeatRow> repeatWrites;
    std::vector<std::string> repeatDeletes;
    std::uint64_t nowNs;
    std::uint64_t steps{0};
    bool overflow{false};

    /** @return The quest of an instance as the plan sees it (its copy, or the value of the entity), or null. */
    const QuestInstance* peek(std::uint64_t instance) const {
        if (removed.count(instance) != 0) {
            return nullptr;
        }
        const auto copy = copies.find(instance);
        if (copy != copies.end()) {
            return &copy->second;
        }
        return owner.instance_of(entity, instance);
    }
    /** @return The copy of a quest that the plan changes (made at the first change), or null. */
    QuestInstance* edit(std::uint64_t instance) {
        if (removed.count(instance) != 0) {
            return nullptr;
        }
        const auto copy = copies.find(instance);
        if (copy != copies.end()) {
            return &copy->second;
        }
        const QuestInstance* quest = owner.instance_of(entity, instance);
        return quest != nullptr ? &copies.emplace(instance, *quest).first->second : nullptr;
    }
    /** @return The quests of the plan in the order of the entity, then the new ones (for a reader of all quests). */
    std::vector<const QuestInstance*> all() const {
        std::vector<const QuestInstance*> list;
        for (const QuestInstance& quest : entity.quests.active) {
            ++counts.questVisits;
            if (const QuestInstance* seen = peek(quest.instance)) {
                list.push_back(seen);
            }
        }
        for (const std::uint64_t instance : added) {
            if (const QuestInstance* seen = peek(instance)) {
                list.push_back(seen);
            }
        }
        return list;
    }

    const QuestObjective* objective_of(const QuestInstance& quest) const {
        return data.objective_by_crc(quest.objectiveCrc);
    }
    const QuestTask* task_of(const QuestInstance& quest, std::uint8_t index) const {
        const QuestObjective* objective = objective_of(quest);
        if (objective == nullptr || index >= objective->nodes.size() || objective->nodes[index].task == gamedata::kNoTask) {
            return nullptr;
        }
        return &data.task(objective->nodes[index].task);
    }

    bool has_bit(std::uint32_t bit) const {
        const auto found = bits.find(bit);
        if (found != bits.end()) {
            return found->second;
        }
        return std::binary_search(entity.achievements.begin(), entity.achievements.end(), bit);
    }

    void action(const std::string& id, bool unlock) {
        actions.emplace_back(id, unlock);
        if (const std::optional<std::uint32_t> bit = ownership.achievement_bit(id)) {
            bits[*bit] = unlock;
        }
    }

    // The conditional of TaskAchievement and of a non-persisted achievement (client 0x31898D0, 0x3A8C640): ids with !, &&, ||, and
    // parentheses. An id is true when the character has its bit; a non-persisted achievement is its own conditional; an id that the data
    // does not know is false (research U7: it must not become a stored bit).
    std::optional<bool> id_value(const std::string& id, int depth) {
        if (id.empty()) {
            return false;
        }
        if (const std::optional<std::uint32_t> bit = ownership.achievement_bit(id)) {
            return has_bit(*bit);
        }
        if (const gamedata::QuestNpa* npa = data.npa(id)) {
            // Stage QG5b (static read A3: the three gates of 0x318A4F0 with AND): the conditional (if any), the ObjectiveFlow (if any) with
            // the quests of the plan, the status (if any; no rule in the backend: false).
            if (depth >= kNpaDepth || !npa->status.empty()) {
                return false;
            }
            if (!npa->conditional.empty()) {
                const std::optional<bool> gate =
                    evaluate_conditional(npa->conditional, [this, depth](const std::string& inner) { return id_value(inner, depth + 1); });
                if (!gate || !*gate) {
                    return gate;
                }
            }
            return npa->flow.empty() || flow_passes(data, npa->flowClauses, [this](const QuestObjective& objective) { return objective_done(objective); },
                                                    [this](std::uint32_t crc) { return instance_of_objective(crc); });
        }
        return false;
    }

    /** Stage QG5b: @return True when the plan has the completion achievement of an objective (IsObjectiveComplete 0x3A48A30). */
    bool objective_done(const QuestObjective& objective) const {
        const std::optional<std::uint32_t> bit = objective.achievement.empty() ? std::nullopt : ownership.achievement_bit(objective.achievement);
        return bit && has_bit(*bit);
    }
    /** Stage QG5b: @return The instance of an objective as the plan sees it, or null. */
    const QuestInstance* instance_of_objective(std::uint32_t crc) const {
        for (const QuestInstance& quest : entity.quests.active) {
            if (quest.objectiveCrc == crc) {
                if (const QuestInstance* seen = peek(quest.instance)) {
                    return seen;
                }
            }
        }
        for (const std::uint64_t instance : added) {
            const QuestInstance* seen = peek(instance);
            if (seen != nullptr && seen->objectiveCrc == crc) {
                return seen;
            }
        }
        return nullptr;
    }

    bool evaluate(const std::string& conditional) {
        if (conditional.empty()) {
            return false;
        }
        return evaluate_conditional(conditional, [this](const std::string& id) { return id_value(id, 0); }).value_or(false);
    }

    bool step() {
        ++steps;
        ++counts.steps;
        if (steps > kStepLimit) {
            overflow = true;
        }
        return !overflow;
    }

    // ----- The items and the places of the character (the predicates of stage 1.8c) -----

    /** @return The count of an item id in the bag and the paperdoll, with the item actions of the plan. */
    std::uint64_t item_count(const std::string& itemId) const {
        const std::int64_t base = static_cast<std::int64_t>(Inventory::count(entity, itemId));
        const auto delta = itemDelta.find(lower_text(itemId));
        return static_cast<std::uint64_t>(std::max<std::int64_t>(0, base + (delta != itemDelta.end() ? delta->second : 0)));
    }
    /** @return True when the character is in a point of interest of the tags (the territory of the task limits them). */
    bool in_place(const QuestTask& task) const {
        const gamedata::PoiData* pois = owner.pois();
        if (pois == nullptr || entity.positionEstimated) {
            return false;
        }
        return pois->inside(task.poiTags, task.territory, entity.position.x, entity.position.y);
    }
    /** @return False when the task has the location rule and the character is not in its place (0x825DAD0, 0x825E860). */
    bool location_ok(const QuestTask& task) const {
        return !task.enforceLocation || task.poiTags.empty() || in_place(task);
    }
    /** @return The value of a predicate task now (its progress), or no value for a task that is not a predicate. */
    std::optional<std::uint32_t> predicate(const QuestTask& task, const gamedata::QuestNode& node) const {
        // EnforceLocationRequirement (0x825DAD0, 0x825E860): a possession or an equip task counts only in its place (code review of stage
        // 1.8c, finding QR8C-R6; the index has these tasks under "place" too).
        if (gamedata::is_possession(task.type)) {
            return location_ok(task) ? static_cast<std::uint32_t>(std::min<std::uint64_t>(item_count(task.itemName), node.target)) : 0U;
        }
        if (task.type == task_type::kEquipItem) {
            return location_ok(task) && Inventory::equipped(entity, task.itemName, task.minGearScore) ? node.target : 0U;
        }
        if (task.type == task_type::kGoToPoi || task.type == task_type::kGoToPoiOnTerritory) {
            return in_place(task) ? node.target : 0U;
        }
        if (task.type == task_type::kExitPoi) {
            // Outside each point of interest of the tags, also at the activation (0x825EBF0). No data, or no shape of a tag of the task:
            // no knowledge of the place (code review of stage 1.8c, finding QR8C-R7: an unknown shape is not an exit).
            return owner.pois() != nullptr && owner.pois()->knows(task.poiTags) && !entity.positionEstimated && !in_place(task) ? node.target
                                                                                                                                 : 0U;
        }
        if (task.type == task_type::kAchievement) {
            return const_cast<Plan*>(this)->evaluate(task.itemName) ? node.target : 0U;
        }
        if (task.type == task_type::kHaveLevel) {
            // Stage QG2a (research quest-gaps.md section 8): with a blank ItemName the level of the character (activation 0x3B04F40); the
            // callback 0x3B073A0 compares the level index with TargetQty - 1 (unsigned): satisfied at the level of the screen TargetQty or
            // more (a TargetQty of 0 never). With an ItemName the points or the rank of a categorical progression (a faction): no owner in
            // the backend (roadmap 4.5), the task waits.
            if (!trimmed_text(task.itemName).empty()) {
                return std::nullopt;
            }
            const std::int64_t target = task.targetQty.value_or(0);
            return target >= 1 && static_cast<std::int64_t>(entity.progression.level) >= target ? node.target : 0U;
        }
        if (task.type == task_type::kTriggerArea) {
            // Stage 1.8b: the accepted position of the character in a volume of the tag of the task (the client sends no message for these
            // volumes: live check T1). Enter and Stay Inside: inside one volume of the tag; Leave: outside each one. No trigger data, no
            // volume of the tag, or a camera estimate: no knowledge of the place, the task waits (the rule of QR8C-R7).
            const gamedata::TriggerData* triggers = owner.triggers();
            if (triggers == nullptr || task.targetTag.empty() || entity.positionEstimated || !triggers->knows(task.targetTag)) {
                return 0U;
            }
            const bool inside = triggers->inside(task.targetTag, entity.position.x, entity.position.y, entity.position.z, &counts.triggerVisits);
            return (task.areaRule == gamedata::AreaRule::leave ? !inside : inside) ? node.target : 0U;
        }
        return std::nullopt;
    }

    /**
     * Stage 1.8b: a TaskTriggerGameEvent applies its game event (KillEnemyType; research quest-engine-3.md Q3): its whisper (the client plays
     * it and shows its subtitles: message 5070 after the commit; the AchievementId of the whisper), its achievement, and its experience and
     * coin. An item of the event goes into the bag. @return False when the bag has no place for the item of the event (the task waits).
     */
    bool apply_event(const QuestTask& task) {
        const std::string eventId = task.targets.empty() ? std::string{} : task.targets.front();
        if (eventId.empty()) {
            notes.push_back("the task " + task.id + " names no game event");
            return true;
        }
        const gamedata::QuestGameEvent* event = data.game_event(eventId);
        if (event != nullptr && !event->item.empty() && event->itemQty > 0) {
            const auto quantity = static_cast<std::uint32_t>(event->itemQty);
            if (!fits(GrantItem{event->item, quantity, std::nullopt, {}})) {
                // The task waits: it is in the index of the items (code review of stage 1.8b, finding QR8B-R11), and a change of the items
                // tries the event again (Quests::refresh).
                notes.push_back("the bag has no place for the item " + event->item + " of the game event " + eventId + ": the task " + task.id
                                + " waits");
                return false;
            }
            itemActions.push_back(QuestItemAction{true, GrantItem{event->item, quantity, std::nullopt, {}}});
            itemDelta[lower_text(event->item)] += quantity;
        }
        std::string whisperText = "no whisper";
        if (const gamedata::QuestWhisper* whisper = data.whisper_of_event(eventId)) {
            whispers.push_back(whisper->crc);
            whisperText = "the whisper " + whisper->id;
            if (!whisper->achievement.empty()) {
                action(whisper->achievement, true);
            }
        }
        if (event != nullptr) {
            if (!event->achievement.empty()) {
                action(event->achievement, true);
            }
            experience += event->experience;
            coin += event->coin;
            if (!event->unpaid.empty()) {
                notes.push_back("the game event " + eventId + " has a reward that the backend cannot pay yet (" + event->unpaid.front() + "): not paid");
            } else if (!event->progression.empty() && event->progressionAmount > 0) {
                // Stage QG5b: only the hand-in pays a categorical progression (the success event of an objective).
                notes.push_back("the game event " + eventId + " has the categorical progression reward " + event->progression
                                + " (only a hand-in pays it): not paid");
            }
        }
        ++counts.gameEventsApplied;
        notes.push_back("the task " + task.id + " applied the game event " + eventId + " (" + whisperText + ")");
        return true;
    }

    // ----- The rules of the tasks -----

    /** Activates a task and runs its rule. */
    void activate(QuestInstance& quest, std::uint8_t index) {
        const QuestObjective* objective = objective_of(quest);
        if (objective == nullptr || index >= objective->nodes.size() || !step()) {
            return;
        }
        const gamedata::QuestNode& node = objective->nodes[index];
        if (node.task == gamedata::kNoTask) {
            return;
        }
        QuestTaskState& state = quest.tasks[index];
        if (done(state.state) || state.state == quest_state::kActive || state.state == quest_state::kFailed) {
            return;
        }
        state.state = quest_state::kActive;
        changed.insert(quest.instance);
        const QuestTask& task = data.task(node.task);
        switch (task.type) {
        case task_type::kConsecutiveContainer: {
            for (const std::uint8_t child : node.children) {
                if (!done(quest.tasks[child].state)) {
                    activate(quest, child);
                    return;
                }
            }
            complete(quest, index);
            return;
        }
        case task_type::kSimpleContainer:
        case task_type::kPartialContainer: {
            if (node.children.empty()) {
                complete(quest, index);
                return;
            }
            for (const std::uint8_t child : node.children) {
                if (quest.tasks[index].state != quest_state::kActive) {
                    return; // A child completed the container.
                }
                activate(quest, child);
            }
            return;
        }
        case task_type::kUnlockAchievement:
            action(task.itemName, true);
            if (!task.stayActive) {
                undo.push_back(state::QuestUndoRow{quest.instance, index, "lock", task.itemName});
            }
            complete(quest, index);
            return;
        case task_type::kLockAchievement:
            action(task.itemName, false);
            if (!task.stayActive) {
                undo.push_back(state::QuestUndoRow{quest.instance, index, "unlock", task.itemName});
            }
            complete(quest, index);
            return;
        case task_type::kTemporaryUnlockAchievement:
            action(task.itemName, true);
            undo.push_back(state::QuestUndoRow{quest.instance, index, "lock", task.itemName});
            complete(quest, index);
            return;
        case task_type::kAchievement:
            waits.push_back(Subscriber{quest.instance, index});
            break; // The predicate below.
        case task_type::kTimer: {
            state.startNs = nowNs;
            timers.push_back(TimerStart{quest.instance, index, nowNs, task.duration.value_or(0.0)});
            ++counts.timersStarted;
            if (task.failOnCondition) {
                // A failure timer is satisfied while it runs (client 0x3B2BA40: its effective StayActive; its activation 0x3B2D250 sets the
                // satisfied active form) and fails the objective at its end (code review of stage 1.8a, finding QR8A-R2).
                complete(quest, index);
            }
            return;
        }
        case task_type::kDie:
            // TaskDie is a condition, not a count of deaths (research quest-engine.md Q1): satisfied while the character lives (184 of 185
            // nodes are in a simple container, which needs each child: a decision of an agent). A death fails or resets the objective.
            complete(quest, index);
            return;
        case task_type::kGiveItem:
        case task_type::kGiveAndTakeItem: {
            // The give at the activation (0x825EEA8, 0x825F020); AwardOnlyOnce: only the first time (0x825EEF0: not after a reset).
            const auto key = std::make_pair(quest.objectiveCrc, index);
            const std::uint32_t quantity = node.target;
            const std::uint32_t paid = award_of(key);
            if (task.awardOnce && paid >= quantity) {
                notes.push_back("the task " + task.id + " gave its items before (AwardOnlyOnce): no item now");
                complete(quest, index);
                return;
            }
            if (!fits(GrantItem{task.itemName, quantity, std::nullopt, {}})) {
                // No place in the bag: the task waits (indexed under "items": a change of the items tries again; a decision of an agent).
                notes.push_back("the bag has no place for " + std::to_string(quantity) + " " + task.itemName + " of the task " + task.id +
                                ": the task waits");
                return;
            }
            itemActions.push_back(QuestItemAction{true, GrantItem{task.itemName, quantity, std::nullopt, {}}});
            itemDelta[lower_text(task.itemName)] += quantity;
            if (task.awardOnce) {
                awards[key] = paid + quantity;
            }
            complete(quest, index);
            return;
        }
        case task_type::kRemoveItem: {
            // The removal at the activation from the inventory, not the global storage (0x8260370). A character with fewer items loses what
            // it has (a decision of an agent: research quest-engine.md Q1 "insufficient-items result needs a rule").
            const auto have = static_cast<std::uint32_t>(std::min<std::uint64_t>(item_count(task.itemName), node.target));
            if (have > 0) {
                itemActions.push_back(QuestItemAction{false, GrantItem{task.itemName, have, std::nullopt, {}}});
                itemDelta[lower_text(task.itemName)] -= have;
            }
            complete(quest, index);
            return;
        }
        case task_type::kObjective:
            grant_child(quest, index, task);
            return;
        case task_type::kTriggerGameEvent:
            // Stage 1.8b: the event at the activation, then complete (client 0x8260AE0, 0x8260B10: "a higher count does not emit more
            // events"; research quest-progress.md).
            if (apply_event(task)) {
                complete(quest, index);
            }
            return;
        default:
            break;
        }
        // A predicate: its value at the activation (a possession, an equip, a place, an achievement: 0x825EB30, 0x825EBF0, 0x825F310).
        if (const std::optional<std::uint32_t> value = predicate(task, node)) {
            set_progress(quest, index, *value);
            return;
        }
        // A counter: it starts with its progress (0, or the progress of an earlier activation of a rewound branch).
        if (is_counter(task.type) && state.progress >= node.target) {
            complete(quest, index);
        }
        // Each other type waits: its supplier is a later part (1.8b: persons, conversations, cutscenes, trigger areas, whispers).
    }

    std::uint32_t award_of(const std::pair<std::uint32_t, std::uint8_t>& key) const {
        const auto planned = awards.find(key);
        if (planned != awards.end()) {
            return planned->second;
        }
        const auto stored = entity.quests.awards.find(key);
        return stored != entity.quests.awards.end() ? stored->second : 0U;
    }

    /** @return True when the bag has a place for the item actions of the plan and one more give. */
    bool fits(const GrantItem& more) {
        std::vector<QuestItemAction> all = itemActions;
        all.push_back(QuestItemAction{true, more});
        const Inventory::Plan trial = owner.m_world.inventory().plan_quest_items(entity.characterId, all, nullptr);
        return trial.ok();
    }

    /** TaskObjective (0x82619E0): a new instance of the child objective; the task completes when the child completes. */
    void grant_child(QuestInstance& quest, std::uint8_t index, const QuestTask& task) {
        const QuestObjective* child = data.objective(task.itemName);
        if (child == nullptr) {
            notes.push_back("the task " + task.id + " names the objective " + task.itemName + ", which the quest data does not have: it waits");
            return;
        }
        for (const QuestInstance* other : all()) {
            if (other->objectiveCrc != child->crc) {
                continue;
            }
            if (other->parent != 0 && peek(other->parent) == nullptr) {
                // The child of a parent that ended in this plan (a reset of the parent): it is the child of the new parent now (code review
                // of stage 1.8c, finding QR8C-R3), so that its completion completes the task of the new parent. The row of the child goes
                // into the transaction of the reset (code review of stage 2.5c, finding PR5C-R4: a new world entry keeps the new parent).
                edit(other->instance)->parent = quest.instance;
                changed.insert(other->instance);
                notes.push_back("the child objective " + child->id + " of the task " + task.id + " is the child of the new instance " +
                                hex(quest.instance) + " now");
                return;
            }
            notes.push_back("the child objective " + child->id + " of the task " + task.id + " is active: no second instance");
            return;
        }
        const std::uint64_t parent = quest.instance;
        const std::uint64_t instance = owner.next_instance();
        add(*child, instance, "objective", quest.originTerritory, parent);
        (void)index;
    }

    /**
     * @return True when the root of a quest is satisfied: the quest waits for its hand-in, and its StayActive tasks do not regress (stage 1.8d,
     *         live F of the beach chain). The data puts the hand-in of 06_crewmates at Aelstrom in the watchtower camp, outside the shipwreck
     *         place of its StayActive task 6: with a regression no character could hand it in. The client function (0x3ADDEC0) has no check
     *         of the objective; a decision of an agent from the data (case QD11).
     */
    bool holds(const QuestInstance& quest) const {
        const QuestObjective* objective = objective_of(quest);
        return objective != nullptr && objective->root < quest.tasks.size() && done(quest.tasks[objective->root].state);
    }

    /** Sets the value of a predicate task: its progress, its completion, or its regression (a CompleteActive task below its target). */
    void set_progress(QuestInstance& quest, std::uint8_t index, std::uint32_t value) {
        const QuestObjective* objective = objective_of(quest);
        if (objective == nullptr) {
            return;
        }
        const gamedata::QuestNode& node = objective->nodes[index];
        QuestTaskState& state = quest.tasks[index];
        value = std::min(value, node.target);
        if (state.state == quest_state::kActive) {
            if (value != state.progress) {
                state.progress = value;
                changed.insert(quest.instance);
                ++counts.progressChanges;
            }
            if (value >= node.target) {
                complete(quest, index);
            }
            return;
        }
        if (state.state == quest_state::kCompleteActive && value < node.target) {
            if (holds(quest)) {
                return;
            }
            // Stage 1.8e: a TaskTriggerArea of the rule Enter is an entry: it stays satisfied when the character leaves the volume (the data
            // has the rule Stay Inside, 3 rows, for a task that needs the character inside; live check M1 of stage 1.8e: the monolith of
            // 06_demonstorm2 stands outside each volume of its StayActive Enter task TriggerVol_06_BeachCave). Stay Inside regresses.
            if (const QuestTask* task = task_of(quest, index);
                task != nullptr && task->type == task_type::kTriggerArea && task->areaRule == gamedata::AreaRule::enter) {
                return;
            }
            // The regression of a StayActive task (client 0x3ADDEC0: a CompleteActive task whose progress falls below its target returns
            // to Active and notifies its parent; code review of stage 1.8a, finding QR8A-R3).
            state.state = quest_state::kActive;
            state.progress = value;
            changed.insert(quest.instance);
            ++counts.progressChanges;
            regressed(quest, index);
        }
    }

    /** Adds to the progress of an active counter task. */
    void add_progress(QuestInstance& quest, std::uint8_t index, std::uint32_t amount) {
        const QuestObjective* objective = objective_of(quest);
        QuestTaskState& state = quest.tasks[index];
        if (objective == nullptr || state.state != quest_state::kActive || amount == 0) {
            return;
        }
        const gamedata::QuestNode& node = objective->nodes[index];
        state.progress = static_cast<std::uint32_t>(std::min<std::uint64_t>(static_cast<std::uint64_t>(state.progress) + amount, node.target));
        changed.insert(quest.instance);
        ++counts.progressChanges;
        if (state.progress >= node.target) {
            complete(quest, index);
        }
    }

    /** @return The number of satisfied direct children of a node (progress at or above the target). */
    static std::uint32_t satisfied(const QuestInstance& quest, const QuestObjective& objective, std::uint8_t index) {
        std::uint32_t count = 0;
        for (const std::uint8_t child : objective.nodes[index].children) {
            count += quest.tasks[child].progress >= objective.nodes[child].target ? 1U : 0U;
        }
        return count;
    }

    /** Ends the active tasks under a node (the other children of a partial container that completed, a rewound branch). */
    void deactivate(QuestInstance& quest, std::uint8_t index) {
        const QuestObjective* objective = objective_of(quest);
        if (objective == nullptr || !step()) {
            return;
        }
        QuestTaskState& state = quest.tasks[index];
        if (!running(state.state)) {
            return; // The base deactivation of the client writes Inactive unless the task is Complete or Failed (0x3AD2FE0).
        }
        state.state = quest_state::kInactive;
        state.startNs = 0;
        changed.insert(quest.instance);
        for (const std::uint8_t child : objective->nodes[index].children) {
            deactivate(quest, child);
        }
    }

    /**
     * A child of a container is no longer satisfied (the regression of a StayActive task): the count of the container, the rewind of a
     * consecutive container (client 0x3AC0990: an earlier child that is Active again deactivates the later children), and the regression of
     * a CompleteActive container.
     */
    void regressed(QuestInstance& quest, std::uint8_t index) {
        const QuestObjective* objective = objective_of(quest);
        if (objective == nullptr || !step()) {
            return;
        }
        const gamedata::QuestNode& node = objective->nodes[index];
        if (!node.parent) {
            return;
        }
        const std::uint8_t parentIndex = *node.parent;
        const gamedata::QuestNode& parent = objective->nodes[parentIndex];
        QuestTaskState& state = quest.tasks[parentIndex];
        if (!running(state.state)) {
            return; // A Complete container is terminal: it does not count again.
        }
        state.progress = satisfied(quest, *objective, parentIndex);
        changed.insert(quest.instance);
        if (data.task(parent.task).type == task_type::kConsecutiveContainer) {
            const auto self = std::find(parent.children.begin(), parent.children.end(), index);
            for (auto later = self == parent.children.end() ? self : self + 1; later != parent.children.end(); ++later) {
                deactivate(quest, *later);
            }
        }
        if (state.state == quest_state::kCompleteActive && state.progress < parent.target) {
            state.state = quest_state::kActive;
            regressed(quest, parentIndex);
        }
    }

    /** Completes a task and runs the rule of its container. */
    void complete(QuestInstance& quest, std::uint8_t index) {
        const QuestObjective* objective = objective_of(quest);
        if (objective == nullptr || !step()) {
            return;
        }
        const gamedata::QuestNode& node = objective->nodes[index];
        QuestTaskState& state = quest.tasks[index];
        if (done(state.state)) {
            return;
        }
        const QuestTask& task = data.task(node.task);
        // A satisfied task: the progress reaches the target (a container: the count of its satisfied children), and the state is
        // CompleteActive when the node has an effective StayActive (research quest-display-18a.md Q3).
        state.state = node.stayActive ? quest_state::kCompleteActive : quest_state::kComplete;
        state.progress = gamedata::is_container(task.type) ? std::max(node.target, satisfied(quest, *objective, index)) : node.target;
        if (!(task.type == task_type::kTimer && task.failOnCondition)) {
            state.startNs = 0; // A failure timer keeps its start: it runs while it is satisfied (finding QR8A-R2).
        }
        changed.insert(quest.instance);
        if (!node.parent) {
            notes.push_back("the root of the objective " + quest.objective + " (instance " + hex(quest.instance) +
                            ") is complete: the quest waits for its hand-in");
            child_completed(quest);
            return;
        }
        const std::uint8_t parentIndex = *node.parent;
        if (!running(quest.tasks[parentIndex].state)) {
            return;
        }
        const gamedata::QuestNode& parent = objective->nodes[parentIndex];
        // The count of the container (client 0x3AD6C60, 0x3AD6D20: the satisfied direct children while it is Active or CompleteActive).
        quest.tasks[parentIndex].progress = satisfied(quest, *objective, parentIndex);
        if (quest.tasks[parentIndex].state == quest_state::kCompleteActive) {
            return; // A satisfied container again (a regressed child came back): nothing more.
        }
        switch (data.task(parent.task).type) {
        case task_type::kConsecutiveContainer: {
            const auto self = std::find(parent.children.begin(), parent.children.end(), index);
            for (auto next = self == parent.children.end() ? self : self + 1; next != parent.children.end(); ++next) {
                if (!done(quest.tasks[*next].state)) {
                    activate(quest, *next);
                    return;
                }
            }
            complete(quest, parentIndex);
            return;
        }
        case task_type::kSimpleContainer:
            if (std::all_of(parent.children.begin(), parent.children.end(), [&](std::uint8_t child) { return done(quest.tasks[child].state); })) {
                complete(quest, parentIndex);
            }
            return;
        case task_type::kPartialContainer:
            // The client ends the unfinished children with no StayActive (0x3AD6D20); the server state says the same.
            for (const std::uint8_t child : parent.children) {
                if (child != index && !objective->nodes[child].stayActive) {
                    deactivate(quest, child);
                }
            }
            complete(quest, parentIndex);
            return;
        default:
            return;
        }
    }

    /** The root of a child objective (TaskObjective) is complete: the task of its parent completes (0x82619E0). */
    void child_completed(const QuestInstance& child) {
        if (child.parent == 0) {
            return;
        }
        QuestInstance* parent = edit(child.parent);
        const QuestObjective* objective = parent != nullptr ? objective_of(*parent) : nullptr;
        for (std::size_t index = 0; objective != nullptr && index < parent->tasks.size(); ++index) {
            const QuestTask* task = task_of(*parent, static_cast<std::uint8_t>(index));
            if (task != nullptr && task->type == task_type::kObjective && parent->tasks[index].state == quest_state::kActive
                && lower_text(task->itemName) == lower_text(child.objective)) {
                complete(*parent, static_cast<std::uint8_t>(index));
                return;
            }
        }
    }

    /**
     * The TaskAchievement tasks of the character (Active or CompleteActive) get the value of their conditional, to a fixed point (an action
     * can change a later one). Only the indexed waits and the waits of this plan are visited (finding QR8A-R7); each visit is counted.
     */
    void settle(const Subscriptions* index) {
        for (int round = 0; round < kSettleRounds && !overflow; ++round) {
            bool progress = false;
            std::vector<Subscriber> list;
            if (const std::vector<Subscriber>* indexed = index != nullptr ? index->find("ach") : nullptr) {
                list = *indexed;
            }
            for (const Subscriber& wait : waits) {
                if (std::find(list.begin(), list.end(), wait) == list.end()) {
                    list.push_back(wait);
                }
            }
            for (const Subscriber& wait : list) {
                ++counts.waitsVisited;
                const QuestInstance* seen = peek(wait.instance);
                const QuestTask* task = seen != nullptr ? task_of(*seen, wait.index) : nullptr;
                if (task == nullptr || task->type != task_type::kAchievement || !running(seen->tasks[wait.index].state)) {
                    continue;
                }
                const bool value = evaluate(task->itemName);
                const bool satisfied = seen->tasks[wait.index].state == quest_state::kCompleteActive;
                if (value == satisfied && !(value && seen->tasks[wait.index].state == quest_state::kActive)) {
                    continue;
                }
                if (!value && holds(*seen)) {
                    continue; // A ready quest holds its satisfied wait (stage 1.8d): no regression, and no round for it.
                }
                QuestInstance* quest = edit(wait.instance);
                const QuestObjective* objective = objective_of(*quest);
                set_progress(*quest, wait.index, value ? objective->nodes[wait.index].target : 0U);
                progress = true;
            }
            if (!progress) {
                return;
            }
        }
        overflow = true;
    }

    /** Adds a new instance of an objective and activates its root. */
    QuestInstance& add(const QuestObjective& objective, std::uint64_t instance, const std::string& source, std::uint16_t territory,
                       std::uint64_t parent = 0) {
        QuestInstance quest;
        quest.instance = instance;
        quest.objective = objective.id;
        quest.objectiveCrc = objective.crc;
        quest.acceptNs = nowNs;
        quest.originTerritory = territory;
        quest.parent = parent;
        quest.source = source;
        quest.tasks.resize(objective.nodes.size());
        copies.emplace(instance, std::move(quest));
        added.push_back(instance);
        changed.insert(instance);
        activate(copies[instance], objective.root);
        return copies[instance];
    }
};

Quests::Quests(World& world, state::Store& store, const gamedata::QuestData* data) : m_world(world), m_store(store), m_data(data) {}

Quests::~Quests() = default;

const gamedata::PoiData* Quests::pois() const {
    return m_pois != nullptr && m_pois->loaded() ? m_pois : nullptr;
}

const gamedata::TriggerData* Quests::triggers() const {
    return m_triggers != nullptr && m_triggers->loaded() ? m_triggers : nullptr;
}

std::uint64_t Quests::now_ns() const {
    return m_world.clock().wall_time_ns(m_world.clock().now());
}

std::uint64_t Quests::next_instance() {
    if (m_nextInstance == 0) {
        m_nextInstance = std::max(kFirstInstance, m_store.largest_quest_instance() + 1);
    }
    return m_nextInstance++;
}

void Quests::schedule(const std::string& characterId, std::uint64_t instance, std::uint8_t index, std::uint64_t startNs, double seconds) {
    // The end on the world clock: the start on the synced wall clock plus the Duration (fractional seconds kept as nanoseconds).
    const auto durationNs = static_cast<std::int64_t>(std::llround(std::max(0.0, seconds) * 1e9));
    const std::int64_t left = static_cast<std::int64_t>(startNs) + durationNs - static_cast<std::int64_t>(now_ns());
    const auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::nanoseconds(std::max<std::int64_t>(0, left)));
    m_world.after(delay, [characterId, instance, index, startNs](World& world) { world.quests().timer_due(characterId, instance, index, startNs); });
}

void Quests::schedule_repeat(const std::string& characterId, std::uint32_t objectiveCrc, std::uint64_t resetNs) {
    // One deadline of the world clock for each repeat (MP-8: the work follows the due entries, not each quest of each player in each tick).
    const std::int64_t left = static_cast<std::int64_t>(resetNs) - static_cast<std::int64_t>(now_ns());
    const auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::nanoseconds(std::max<std::int64_t>(0, left)));
    ++m_counts.repeatsScheduled;
    m_world.after(delay, [characterId, objectiveCrc, resetNs](World& world) { world.quests().repeat_due(characterId, objectiveCrc, resetNs); });
}

void Quests::repeat_due(const std::string& characterId, std::uint32_t objectiveCrc, std::uint64_t resetNs) {
    Entity* entity = m_world.find_character(characterId);
    if (entity == nullptr || data() == nullptr || !entity->quests.loaded) {
        return; // The character left: its next entry schedules the reset again from its row.
    }
    const auto found = entity->quests.repeats.find(objectiveCrc);
    const QuestObjective* objective = m_data->objective_by_crc(objectiveCrc);
    if (found == entity->quests.repeats.end() || found->second.resetNs != resetNs || objective == nullptr) {
        return; // An old deadline: a later completion has its own.
    }
    if (now_ns() < resetNs) {
        schedule_repeat(characterId, objectiveCrc, resetNs); // The world clock moved back (a development command): wait again.
        return;
    }
    ++m_counts.repeatsDue;
    // The reset generation: one plan removes the completion achievement of the objective and the repeat row; the person offers it again.
    Plan plan(*this, *entity, now_ns());
    if (!objective->achievement.empty()) {
        plan.action(objective->achievement, false);
    }
    // The row keeps the generation (the number of completions) with no deadline.
    state::QuestRepeatRow row = found->second;
    row.resetNs = 0;
    plan.repeatWrites.push_back(row);
    const auto subscriptions = m_subscriptions.find(entity->id);
    plan.settle(subscriptions != m_subscriptions.end() ? subscriptions->second.get() : nullptr);
    const QuestError error = commit(*entity, plan, {});
    if (error == QuestError::storeFailed) {
        m_world.after(kTimerRetry, [characterId, objectiveCrc, resetNs](World& world) { world.quests().repeat_due(characterId, objectiveCrc, resetNs); });
    } else if (error == QuestError::none) {
        ++m_counts.repeatResets;
        // The offer of the person of an open conversation follows the bits (Persons::refresh_offer at the next request).
    }
    log::write("quests: the repeat of %s of character %s (completion %u) reset at its cycle: %s", objective->id.c_str(), characterId.c_str(),
               static_cast<unsigned>(found == entity->quests.repeats.end() ? 0 : found->second.completions), quest_error_text(error));
}

bool Quests::repeated(std::uint64_t result) const {
    return m_results.count(result) != 0;
}

void Quests::remember(std::uint64_t result) {
    if (!m_results.insert(result).second) {
        return;
    }
    m_resultOrder.push_back(result);
    if (m_resultOrder.size() > kResultWindow) {
        m_results.erase(m_resultOrder.front());
        m_resultOrder.pop_front();
    }
}

const QuestInstance* Quests::instance_of(const Entity& entity, std::uint64_t instance) {
    ++m_counts.instanceLookups;
    const auto found = m_subscriptions.find(entity.id);
    if (found != m_subscriptions.end() && found->second) {
        const auto position = found->second->positions.find(instance);
        if (position != found->second->positions.end() && position->second < entity.quests.active.size()
            && entity.quests.active[position->second].instance == instance) {
            return &entity.quests.active[position->second];
        }
        if (position == found->second->positions.end() && found->second->positions.size() == entity.quests.active.size()) {
            return nullptr; // The index has each active instance: no such instance.
        }
    }
    // No index yet (a character that is not in the world, a plan before the first index): one walk, counted.
    for (const QuestInstance& quest : entity.quests.active) {
        ++m_counts.questVisits;
        if (quest.instance == instance) {
            return &quest;
        }
    }
    return nullptr;
}

void Quests::index(const Entity& entity, const std::set<std::uint64_t>& instances, bool all) {
    if (data() == nullptr || !entity.quests.loaded) {
        return;
    }
    std::unique_ptr<Subscriptions>& slot = m_subscriptions[entity.id];
    if (!slot || all) {
        slot = std::make_unique<Subscriptions>();
    }
    Subscriptions& subscriptions = *slot;
    for (const std::uint64_t instance : instances) {
        subscriptions.remove(instance);
    }
    // The instances to index: all (a new index: one walk, counted), or only the changed ones by their place (finding QR8C-R11).
    std::vector<const QuestInstance*> list;
    if (all) {
        subscriptions.positions.clear();
        for (std::size_t position = 0; position < entity.quests.active.size(); ++position) {
            ++m_counts.questVisits;
            subscriptions.positions[entity.quests.active[position].instance] = position;
            list.push_back(&entity.quests.active[position]);
        }
    } else {
        for (const std::uint64_t instance : instances) {
            if (const QuestInstance* quest = instance_of(entity, instance)) {
                list.push_back(quest);
            }
        }
    }
    for (const QuestInstance* entry : list) {
        const QuestInstance& quest = *entry;
        const QuestObjective* objective = m_data->objective_by_crc(quest.objectiveCrc);
        for (std::size_t index = 0; objective != nullptr && index < quest.tasks.size(); ++index) {
            const gamedata::QuestNode& node = objective->nodes[index];
            const std::uint8_t state = quest.tasks[index].state;
            if (node.task == gamedata::kNoTask || !running(state)) {
                continue;
            }
            const QuestTask& task = m_data->task(node.task);
            const Subscriber entry{quest.instance, static_cast<std::uint8_t>(index)};
            const bool open = state == quest_state::kActive;
            if (!task.interactTag.empty() && open) {
                subscriptions.add("t:" + task.interactTag, entry);
            }
            // An Active give and an Active TaskTriggerGameEvent wait for a place in the bag (code review of stage 1.8b, finding QR8B-R11).
            if (gamedata::is_possession(task.type) || task.type == task_type::kEquipItem
                || (open && (task.type == task_type::kGiveItem || task.type == task_type::kGiveAndTakeItem
                             || task.type == task_type::kTriggerGameEvent))) {
                subscriptions.add("items", entry);
            }
            if (is_place(task.type)
                || ((gamedata::is_possession(task.type) || task.type == task_type::kEquipItem) && task.enforceLocation && !task.poiTags.empty())) {
                subscriptions.add("place", entry); // Finding QR8C-R6: a possession or an equip with the location rule looks at each move too.
            }
            if (task.type == task_type::kAchievement) {
                subscriptions.add("ach", entry);
            }
            if (task.type == task_type::kDie) {
                subscriptions.add("die", entry);
            }
            // Stage QG2a: TaskHaveLevel of the level of the character (a blank ItemName) looks again at a change of the level.
            if (task.type == task_type::kHaveLevel && trimmed_text(task.itemName).empty()) {
                subscriptions.add("level", entry);
            }
            if (!open) {
                continue; // The counters and the drops need an Active task.
            }
            if (gamedata::is_possession(task.type) && task.minGearScore == 0 && task.dropChance > 0.0) {
                const char* prefix = task.type == task_type::kGatherItems || task.type == task_type::kGatherAndReturnItems ? "gd:" : "kd:";
                for (const std::string& source : task.dropSources) {
                    subscriptions.add(prefix + source, entry);
                }
            }
            // Stage QG5a: a possession task with ChestDropProbability looks at each successful open of a chest of its character.
            if (gamedata::is_possession(task.type) && task.minGearScore == 0 && task.chestChance > 0.0) {
                subscriptions.add("cd", entry);
            }
            switch (task.type) {
            case task_type::kKillContribution:
                for (const std::string& target : task.targets) {
                    subscriptions.add("k:" + target, entry);
                }
                break;
            case task_type::kGameEvent:
                for (const std::string& target : task.targets) {
                    subscriptions.add("e:" + target, entry);
                }
                break;
            case task_type::kRequireGatherItems:
                subscriptions.add("g:" + lower_text(task.itemName), entry);
                break;
            case task_type::kCraftRecipe:
                subscriptions.add(task.anyRecipe ? std::string("c:*") : "c:" + task.recipe, entry);
                break;
            case task_type::kCraftMultipleRecipes:
                for (const std::string& recipe : task.dropSources) {
                    subscriptions.add("c:" + recipe, entry);
                }
                break;
            case task_type::kRefineRecipe:
                subscriptions.add("r:" + task.recipe, entry);
                break;
            case task_type::kOpenUiScreen:
                subscriptions.add("s:" + std::to_string(task.screenCrc), entry);
                break;
            case task_type::kInteract:
                subscriptions.add("i:" + task.interactTag, entry);
                break;
            // Stage 1.8b: the cutscene and the cinematic of the task (the CRC-32 of the name), the person and its topic or state.
            case task_type::kCutsceneComplete:
                subscriptions.add("cut:" + std::to_string(task.nameCrc), entry);
                break;
            case task_type::kCinematicStart:
                subscriptions.add("cin0:" + std::to_string(task.nameCrc), entry);
                break;
            case task_type::kCinematicComplete:
                subscriptions.add("cin1:" + std::to_string(task.nameCrc), entry);
                break;
            case task_type::kConversationTopic:
                subscriptions.add("ct:" + person_key(task.itemName, task.screen), entry);
                break;
            case task_type::kConversationState:
                subscriptions.add("cs:" + person_key(task.itemName, task.screen), entry);
                break;
            default:
                break;
            }
        }
    }
}

std::size_t Quests::subscriber_count(const Entity& entity) const {
    const auto found = m_subscriptions.find(entity.id);
    return found != m_subscriptions.end() && found->second ? found->second->total : 0;
}

bool Quests::has_place_tasks(const Entity& entity) const {
    const auto found = m_subscriptions.find(entity.id);
    return found != m_subscriptions.end() && found->second && found->second->find("place") != nullptr;
}

QuestError Quests::commit(Entity& entity, Plan& plan, const std::vector<state::QuestGrantRow>& grants) {
    ++m_counts.plans;
    if (plan.overflow) {
        log::write("quests: the task rules of character %s did not end after %llu steps: no change", entity.characterId.c_str(),
                   static_cast<unsigned long long>(plan.steps));
        return QuestError::noFixedPoint;
    }
    // The net achievement changes: the last action of each id.
    std::vector<std::string> unlocks;
    std::vector<std::string> locks;
    {
        std::map<std::string, std::pair<std::string, bool>> last;
        std::vector<std::string> order;
        for (const auto& [id, unlock] : plan.actions) {
            const std::string key = gamedata::lower_text(id);
            if (last.count(key) == 0) {
                order.push_back(key);
            }
            last[key] = {id, unlock};
        }
        for (const std::string& key : order) {
            (last[key].second ? unlocks : locks).push_back(last[key].first);
        }
    }
    // The item actions (stage 1.8c) and the coin (stage 1.8b): one plan of the inventory, written in this transaction, finished after it.
    std::optional<Inventory::Plan> items;
    std::optional<std::uint64_t> coin;
    if (plan.coin > 0) {
        coin = std::min(entity.wallet.coin + plan.coin, kWalletLimit);
    }
    if (!plan.itemActions.empty() || coin) {
        items = m_world.inventory().plan_quest_items(entity.characterId, plan.itemActions, nullptr);
        if (!items->ok()) {
            log::write("quests: the items of the quest change of character %s do not fit (%s): no change", entity.characterId.c_str(),
                       item_error_text(items->error));
            return items->error == ItemError::bagFull ? QuestError::bagFull : QuestError::storeFailed;
        }
    }
    // The territory standing (stage 1.8d): one plan of the skill system (the owner of the categorical progressions).
    std::optional<Skills::Plan> standing;
    if (!plan.standing.empty() || plan.azoth > 0 || !plan.progressions.empty()) {
        standing = m_world.skills().plan_none(entity.characterId);
        // Stage 1.8e: AzothReward goes into the same plan (the Azoth balance is a categorical progression too); a gain stops at the cap.
        if (plan.azoth > 0) {
            if (const ProgressError error = m_world.skills().add_balance(*standing, kAzothCurrency, static_cast<std::int64_t>(plan.azoth), plan.azothCap);
                error != ProgressError::none) {
                log::write("quests: the Azoth %llu for character %s cannot be given (%s): no change", static_cast<unsigned long long>(plan.azoth),
                           entity.characterId.c_str(), progress_error_text(error));
                return QuestError::cannotPay;
            }
        }
        for (const auto& [territory, amount] : plan.standing) {
            if (const ProgressError error = m_world.skills().add_standing(*standing, territory, amount); error != ProgressError::none) {
                log::write("quests: the standing %llu of the territory %u for character %s cannot be given (%s): no change",
                           static_cast<unsigned long long>(amount), static_cast<unsigned>(territory), entity.characterId.c_str(),
                           progress_error_text(error));
                return QuestError::cannotPay;
            }
        }
        // Stage QG5b: the categorical progression reward (the AzothStaff rank) in the same plan: the experience rule of the trade skills
        // (Skills::add_experience: AutoRankUp, the cap of the rank table; at the cap the experience is 0).
        for (const auto& [id, amount] : plan.progressions) {
            if (const ProgressError error = m_world.skills().add_experience(*standing, id, amount); error != ProgressError::none) {
                log::write("quests: the experience %llu of %s for character %s cannot be given (%s): no change",
                           static_cast<unsigned long long>(amount), id.c_str(), entity.characterId.c_str(), progress_error_text(error));
                return QuestError::cannotPay;
            }
        }
    }
    // The experience (stage 1.8b): one plan of the progress system (the pattern of the build presets). Stage QG1: with the experience of the
    // standing ranks of the plan (XpReward times TerritoryStandingXpModifier, client 0x31FD3B0): the standing plan comes first.
    // Stage QG1: the experience of the quest with the GainXP card of the territory of the character (client 0x3CF04A9; one product for the
    // experience of the event and of the row: an agent rule), and the ranks with their own card product (add_standing).
    const std::uint64_t experience =
        m_world.skills().card_experience(entity.characterId, plan.experience) + (standing ? standing->rankExperience : 0);
    std::optional<Progress::Plan> sheet;
    if (experience > 0) {
        sheet = m_world.progress().plan_experience(entity.characterId, std::min(experience, kExperienceLimit));
        if (!sheet->ok()) {
            log::write("quests: the experience of the quest change of character %s cannot be given (%s): no change", entity.characterId.c_str(),
                       progress_error_text(sheet->error));
            return QuestError::cannotPay;
        }
    }
    state::Store::Batch batch(m_store);
    bool ok = batch.open();
    for (const std::uint64_t instance : plan.changed) {
        const QuestInstance* quest = plan.peek(instance);
        const QuestObjective* objective = quest != nullptr ? plan.objective_of(*quest) : nullptr;
        if (!ok || objective == nullptr) {
            continue;
        }
        state::QuestRow row;
        row.instance = quest->instance;
        row.objective = quest->objective;
        row.objectiveCrc = quest->objectiveCrc;
        row.acceptNs = quest->acceptNs;
        row.originTerritory = quest->originTerritory;
        row.parent = quest->parent;
        row.source = quest->source;
        for (std::size_t index = 0; index < quest->tasks.size(); ++index) {
            const gamedata::QuestNode& node = objective->nodes[index];
            if (node.task == gamedata::kNoTask) {
                continue;
            }
            const QuestTaskState& task = quest->tasks[index];
            row.tasks.push_back(state::QuestTaskRow{static_cast<std::uint32_t>(index), m_data->task(node.task).id, m_data->task(node.task).crc,
                                                    task.progress, task.state, task.startNs});
        }
        ok = m_store.write_quest(entity.characterId, row);
    }
    for (const std::uint64_t instance : plan.removed) {
        ok = ok && m_store.delete_quest(entity.characterId, instance);
    }
    if (ok && plan.trackedChanged) {
        ok = m_store.write_quest_pins(entity.characterId, plan.tracked);
    }
    if (ok && !unlocks.empty()) {
        ok = m_store.grant_achievements(entity.characterId, unlocks, nullptr);
    }
    if (ok && !locks.empty()) {
        ok = m_store.remove_achievements(entity.characterId, locks);
    }
    for (const state::QuestUndoRow& row : plan.undo) {
        ok = ok && m_store.write_quest_undo(entity.characterId, row);
    }
    for (const std::string& objective : plan.awardDeletes) {
        ok = ok && m_store.delete_quest_awards(entity.characterId, objective);
    }
    for (const auto& [key, amount] : plan.awards) {
        const QuestObjective* objective = m_data->objective_by_crc(key.first);
        ok = ok && objective != nullptr && m_store.write_quest_award(entity.characterId, state::QuestAwardRow{objective->id, key.second, amount});
    }
    for (const state::QuestGrantRow& grant : grants) {
        ok = ok && m_store.write_quest_grant(entity.characterId, grant);
    }
    if (ok && items) {
        ok = m_world.inventory().write(*items, coin);
    }
    if (ok && sheet) {
        ok = m_world.progress().write(*sheet);
    }
    if (ok && standing) {
        ok = m_world.skills().write(*standing);
    }
    for (const state::QuestRepeatRow& row : plan.repeatWrites) {
        ok = ok && m_store.write_quest_repeat(entity.characterId, row);
    }
    for (const std::string& objective : plan.repeatDeletes) {
        ok = ok && m_store.delete_quest_repeat(entity.characterId, objective);
    }
    ok = ok && batch.commit();
    if (!ok) {
        ++m_counts.storeFailures;
        log::write("quests: the database did not take the quest change of character %s: no change", entity.characterId.c_str());
        return QuestError::storeFailed;
    }
    // The rows are in the database: now the entity, the bits, the items, the marks, the timers, and the index. A plan that adds or removes
    // no instance replaces its copies at their places (code review of stage 1.8c, finding QR8C-R11: no walk over the other quests).
    const auto slot = m_subscriptions.find(entity.id);
    Subscriptions* subscriptions = slot != m_subscriptions.end() ? slot->second.get() : nullptr;
    bool replaced = plan.added.empty() && plan.removed.empty() && subscriptions != nullptr
                    && subscriptions->positions.size() == entity.quests.active.size();
    for (auto copy = plan.copies.begin(); replaced && copy != plan.copies.end(); ++copy) {
        const auto position = subscriptions->positions.find(copy->first);
        replaced = position != subscriptions->positions.end() && position->second < entity.quests.active.size()
                   && entity.quests.active[position->second].instance == copy->first;
    }
    if (replaced) {
        for (auto& [instance, quest] : plan.copies) {
            entity.quests.active[subscriptions->positions.at(instance)] = std::move(quest);
        }
    } else {
        std::vector<QuestInstance> active;
        for (QuestInstance& quest : entity.quests.active) {
            ++m_counts.questVisits;
            if (plan.removed.count(quest.instance) != 0) {
                continue;
            }
            const auto copy = plan.copies.find(quest.instance);
            active.push_back(copy != plan.copies.end() ? std::move(copy->second) : std::move(quest));
        }
        for (const std::uint64_t instance : plan.added) {
            if (plan.removed.count(instance) == 0) {
                active.push_back(std::move(plan.copies[instance]));
            }
        }
        entity.quests.active = std::move(active);
        if (subscriptions != nullptr) {
            subscriptions->positions.clear();
            for (std::size_t position = 0; position < entity.quests.active.size(); ++position) {
                subscriptions->positions[entity.quests.active[position].instance] = position;
            }
        }
    }
    entity.quests.tracked = plan.tracked;
    for (const std::string& objective : plan.awardDeletes) {
        const QuestObjective* row = m_data->objective(objective);
        for (auto award = entity.quests.awards.begin(); row != nullptr && award != entity.quests.awards.end();) {
            award = award->first.first == row->crc ? entity.quests.awards.erase(award) : std::next(award);
        }
    }
    for (const auto& [key, amount] : plan.awards) {
        entity.quests.awards[key] = amount;
    }
    entity.quests.loaded = true;
    entity.quests.data = m_data;
    // The completed notice (stage 1.8b): the instances of the hand-ins of this world entry, the newest last.
    for (const std::uint64_t instance : plan.completed) {
        entity.quests.completed.push_back(instance);
    }
    if (entity.quests.completed.size() > kCompletedNotices) {
        entity.quests.completed.erase(entity.quests.completed.begin(),
                                      entity.quests.completed.end() - static_cast<std::ptrdiff_t>(kCompletedNotices));
    }
    // The level and the experience first (the finish of the items refreshes the gear with them; the order of the build presets).
    if (sheet) {
        (void)m_world.progress().finish(*sheet, "the experience of a quest");
    }
    if (standing) {
        (void)m_world.skills().finish(*standing, "the territory standing and the Azoth of a quest");
    }
    // The repeats (stage 1.8d): the rows of the entity, and a deadline of the world clock for each new one.
    for (const std::string& objective : plan.repeatDeletes) {
        if (const QuestObjective* row = m_data->objective(objective)) {
            entity.quests.repeats.erase(row->crc);
        }
    }
    for (const state::QuestRepeatRow& row : plan.repeatWrites) {
        if (const QuestObjective* objective = m_data->objective(row.objective)) {
            entity.quests.repeats[objective->crc] = row;
            if (row.resetNs != 0) {
                schedule_repeat(entity.characterId, objective->crc, row.resetNs);
            }
        }
    }
    m_world.ownership().quest_achievements(entity, unlocks, locks);
    if (!unlocks.empty() || !locks.empty()) {
        m_pushDirty.insert(entity.id); // Stage QG2a: the bits of a quest change (a completion, an action, an undo) are a change of the push.
    }
    m_world.touch(entity.id, ComponentKey::objectives);
    // The index before the items (code review of stage 1.8c, finding QR8C-R14): the item change of this plan marks the possession tasks
    // that the plan activated (a give of the same plan for a new HaveItems task).
    std::set<std::uint64_t> touched = plan.changed;
    touched.insert(plan.removed.begin(), plan.removed.end());
    index(entity, touched, false);
    if (items) {
        (void)m_world.inventory().finish(*items, "the item actions of a quest");
        if (coin) {
            m_world.inventory().coin_written(entity.characterId, *coin);
        }
    }
    // The whispers of the game events of the plan (stage 1.8b): the session of the owner sends 5070 (live check W1: the subtitles).
    for (const std::uint32_t whisper : plan.whispers) {
        ++m_counts.whispers;
        m_world.whisper(entity.owner, whisper);
    }
    for (const Plan::TimerStart& timer : plan.timers) {
        schedule(entity.characterId, timer.instance, timer.index, timer.startNs, timer.seconds);
    }
    for (const std::string& note : plan.notes) {
        log::write("quests: character %s: %s", entity.characterId.c_str(), note.c_str());
    }
    return QuestError::none;
}

QuestResult Quests::grant(Entity& entity, const std::string& objectiveId, const std::string& source, std::uint16_t territory) {
    QuestResult result;
    if (data() == nullptr) {
        result.error = QuestError::noData;
        return result;
    }
    const QuestObjective* objective = m_data->objective(objectiveId);
    if (objective == nullptr) {
        result.error = QuestError::unknownObjective;
        return result;
    }
    for (const QuestInstance& quest : entity.quests.active) {
        if (quest.objectiveCrc == objective->crc) {
            result.error = QuestError::alreadyActive;
            result.instance = quest.instance;
            return result;
        }
    }
    Plan plan(*this, entity, now_ns());
    const std::uint64_t instance = next_instance();
    plan.add(*objective, instance, source, territory);
    // The automatic pin (a server rule: the client has no automatic pin for these types, research quests.md 3.4): the flag of the type of
    // the objective in the last 2709 of the client; with no flag, the default options of the client.
    const std::optional<bool> flag = objective->typeIndex ? m_world.auto_pin(entity, *objective->typeIndex) : std::nullopt;
    const bool pin = flag.value_or(!objective->typeIndex || *objective->typeIndex != kCommunityGoalType);
    if (pin && plan.tracked.size() < kMaxTracked) {
        plan.tracked.push_back(instance);
        plan.trackedChanged = true;
    }
    const auto found = m_subscriptions.find(entity.id);
    plan.settle(found != m_subscriptions.end() ? found->second.get() : nullptr);
    result.error = commit(entity, plan, {});
    if (result.ok()) {
        result.instance = instance;
        ++m_counts.grants;
        const QuestInstance* quest = nullptr;
        for (const QuestInstance& candidate : entity.quests.active) {
            if (candidate.instance == instance) {
                quest = &candidate;
            }
        }
        std::string active;
        for (std::size_t index = 0; quest != nullptr && index < quest->tasks.size(); ++index) {
            if (quest->tasks[index].state == quest_state::kActive) {
                active += (active.empty() ? "" : " ") + std::to_string(index);
            }
        }
        log::write("quests: character %s got the objective %s (instance %016llx, %s), active tasks [%s]%s", entity.characterId.c_str(),
                   objective->id.c_str(), static_cast<unsigned long long>(instance), source.c_str(), active.c_str(), pin ? ", pinned" : "");
    }
    return result;
}

void Quests::entered(Entity& entity) {
    if (data() == nullptr) {
        return; // No quest data: no quest, and no change of the states (rule A5a).
    }
    // The first instance id of this run, before any change of a character can remove a row (stage 1.8d, a finding of the live run: a lazy
    // start after a hand-in gave a new instance the id of the instance of the completed notice of the same world entry).
    if (m_nextInstance == 0) {
        m_nextInstance = std::max(kFirstInstance, m_store.largest_quest_instance() + 1);
    }
    QuestLog log;
    std::size_t skipped = 0;
    for (const state::QuestRow& row : m_store.list_quests(entity.characterId)) {
        const QuestObjective* objective = m_data->objective(row.objective);
        if (objective == nullptr || objective->crc != row.objectiveCrc) {
            ++skipped; // The data has no such objective now: the rows stay, and the client gets no element.
            continue;
        }
        QuestInstance quest;
        quest.instance = row.instance;
        quest.objective = objective->id;
        quest.objectiveCrc = objective->crc;
        quest.acceptNs = row.acceptNs;
        quest.originTerritory = static_cast<std::uint16_t>(row.originTerritory);
        quest.parent = row.parent;
        quest.source = row.source;
        quest.tasks.resize(objective->nodes.size());
        for (const state::QuestTaskRow& task : row.tasks) {
            if (task.index >= objective->nodes.size() || objective->nodes[task.index].task == gamedata::kNoTask
                || m_data->task(objective->nodes[task.index].task).crc != task.crc) {
                ++skipped;
                continue;
            }
            quest.tasks[task.index] = QuestTaskState{static_cast<std::uint8_t>(task.state), task.progress, task.startNs};
        }
        log.active.push_back(std::move(quest));
    }
    for (const std::uint64_t instance : m_store.list_quest_pins(entity.characterId)) {
        if (log.tracked.size() < kMaxTracked
            && std::any_of(log.active.begin(), log.active.end(), [&](const QuestInstance& quest) { return quest.instance == instance; })) {
            log.tracked.push_back(instance);
        }
    }
    for (const state::QuestAwardRow& row : m_store.list_quest_awards(entity.characterId)) {
        if (const QuestObjective* objective = m_data->objective(row.objective); objective != nullptr && row.index <= 255) {
            log.awards[{objective->crc, static_cast<std::uint8_t>(row.index)}] = row.amount;
        }
    }
    // Stage 1.8d: the repeats of the completed repeatable objectives (their reset on the world clock).
    for (const state::QuestRepeatRow& row : m_store.list_quest_repeats(entity.characterId)) {
        if (const QuestObjective* objective = m_data->objective(row.objective)) {
            log.repeats[objective->crc] = row;
        }
    }
    log.loaded = true;
    log.data = m_data;
    entity.quests = std::move(log);
    if (skipped != 0) {
        log::write("quests: character %s has %zu saved quest row(s) that the quest data does not have: not loaded (the rows stay)",
                   entity.characterId.c_str(), skipped);
    }
    // The timers that run (Active, and a failure timer CompleteActive: finding QR8A-R2): their end on the world clock from the saved start
    // (the time while the character was away counts: the client shows start + Duration against the synced wall clock; decision D4).
    for (const QuestInstance& quest : entity.quests.active) {
        const QuestObjective* objective = m_data->objective_by_crc(quest.objectiveCrc);
        for (std::size_t index = 0; objective != nullptr && index < quest.tasks.size(); ++index) {
            const gamedata::QuestNode& node = objective->nodes[index];
            if (node.task != gamedata::kNoTask && running(quest.tasks[index].state) && quest.tasks[index].startNs != 0
                && m_data->task(node.task).type == task_type::kTimer) {
                schedule(entity.characterId, quest.instance, static_cast<std::uint8_t>(index), quest.tasks[index].startNs,
                         m_data->task(node.task).duration.value_or(0.0));
            }
        }
    }
    index(entity, {}, true);
    // Stage 1.8d: the deadline of each repeat (a reset that is past runs at once: the time while the character was away counts).
    for (const auto& [crc, row] : entity.quests.repeats) {
        if (row.resetNs != 0) {
            schedule_repeat(entity.characterId, crc, row.resetNs);
        }
    }
    // The seasons readiness state: the HUD pin needs it (live check L1 of stage 1.8a).
    m_world.touch(entity.id, ComponentKey::seasons);
    if (!entity.quests.active.empty()) {
        m_world.touch(entity.id, ComponentKey::objectives);
    }
    run_pending(entity);
    // The items, the place, and the bits can have changed while the character was away (a template, the DLC, a command).
    refresh(entity, true, true, true, true, "entry");
    // Stage QG2a: the push of PushObjectiveOnReqMet when the saved state of the character is ready (a project rule: the schedule of the
    // original server is not known).
    push_changed(entity, true, "entry");
    log::write("quests: character %s has %zu active quest(s), %zu pinned, %zu indexed task(s)", entity.characterId.c_str(),
               entity.quests.active.size(), entity.quests.tracked.size(), subscriber_count(entity));
}

void Quests::left(const Entity& entity) {
    m_subscriptions.erase(entity.id);
    m_itemsDirty.erase(entity.id);
    m_placesDirty.erase(entity.id);
    m_levelsDirty.erase(entity.id);
    m_pushDirty.erase(entity.id);
    m_pushWatch.erase(entity.id);
    m_equips.erase(entity.id);
}

void Quests::run_pending(Entity& entity) {
    for (state::QuestGrantRow grant : m_store.list_quest_grants(entity.characterId)) {
        if (grant.state != "pending") {
            continue;
        }
        const QuestObjective* objective = m_data->objective(grant.objective);
        if (objective == nullptr) {
            log::write("quests: the pending grant %s %s of character %s names the objective %s, which the quest data does not have: it waits",
                       grant.source.c_str(), grant.key.c_str(), entity.characterId.c_str(), grant.objective.c_str());
            continue;
        }
        const auto active = std::find_if(entity.quests.active.begin(), entity.quests.active.end(),
                                         [&](const QuestInstance& quest) { return quest.objectiveCrc == objective->crc; });
        if (active != entity.quests.active.end()) {
            grant.state = "granted";
            grant.instance = active->instance;
            (void)m_store.write_quest_grant(entity.characterId, grant);
            continue;
        }
        Plan plan(*this, entity, now_ns());
        const std::uint64_t instance = next_instance();
        const std::uint16_t territory = grant.source == "territory" ? static_cast<std::uint16_t>(std::strtoul(grant.key.c_str(), nullptr, 10)) : 0;
        plan.add(*objective, instance, grant.source, territory);
        const std::optional<bool> flag = objective->typeIndex ? m_world.auto_pin(entity, *objective->typeIndex) : std::nullopt;
        if (flag.value_or(!objective->typeIndex || *objective->typeIndex != kCommunityGoalType) && plan.tracked.size() < kMaxTracked) {
            plan.tracked.push_back(instance);
            plan.trackedChanged = true;
        }
        const auto found = m_subscriptions.find(entity.id);
        plan.settle(found != m_subscriptions.end() ? found->second.get() : nullptr);
        grant.state = "granted";
        grant.instance = instance;
        const QuestError error = commit(entity, plan, {grant});
        if (error != QuestError::none) {
            log::write("quests: the pending grant %s %s of character %s: %s; the next entry tries again", grant.source.c_str(), grant.key.c_str(),
                       entity.characterId.c_str(), quest_error_text(error));
            continue;
        }
        ++m_counts.grants;
        log::write("quests: character %s got the objective %s (instance %016llx) of the one-time grant %s %s", entity.characterId.c_str(),
                   objective->id.c_str(), static_cast<unsigned long long>(instance), grant.source.c_str(), grant.key.c_str());
    }
}

bool Quests::conditional(const Entity& entity, const std::string& text) const {
    if (text.empty()) {
        return false;
    }
    const Ownership& ownership = m_world.ownership();
    std::function<std::optional<bool>(const std::string&, int)> value = [&](const std::string& id, int depth) -> std::optional<bool> {
        if (id.empty()) {
            return false;
        }
        if (const std::optional<std::uint32_t> bit = ownership.achievement_bit(id)) {
            return std::binary_search(entity.achievements.begin(), entity.achievements.end(), *bit);
        }
        if (m_data != nullptr && m_data->loaded()) {
            if (const gamedata::QuestNpa* npa = m_data->npa(id)) {
                // Stage QG5b: the three gates of the client (0x318A4F0): the conditional, the ObjectiveFlow with the quests of the character,
                // the status (no rule: false).
                if (depth >= kNpaDepth || !npa->status.empty()) {
                    return false;
                }
                if (!npa->conditional.empty()) {
                    const std::optional<bool> gate =
                        evaluate_conditional(npa->conditional, [&](const std::string& inner) { return value(inner, depth + 1); });
                    if (!gate || !*gate) {
                        return gate;
                    }
                }
                return npa->flow.empty()
                       || flow_passes(
                           *m_data, npa->flowClauses,
                           [&](const QuestObjective& objective) {
                               const std::optional<std::uint32_t> bit =
                                   objective.achievement.empty() ? std::nullopt : ownership.achievement_bit(objective.achievement);
                               return bit && std::binary_search(entity.achievements.begin(), entity.achievements.end(), *bit);
                           },
                           [&](std::uint32_t crc) { return active_of(entity, crc); });
            }
        }
        return false;
    };
    // A malformed expression does not pass (finding PR5C-R7).
    return evaluate_conditional(text, [&](const std::string& id) { return value(id, 0); }).value_or(false);
}

std::optional<std::uint8_t> Quests::task_state(const Entity& entity, std::string_view objective, std::string_view task) const {
    if (m_data == nullptr || !m_data->loaded()) {
        return std::nullopt;
    }
    const std::string wantedObjective = lower_text(objective);
    const std::string wantedTask = lower_text(task);
    // The quests of the character (MP-8: the active instances of one character, each tree 255 nodes at most).
    for (const QuestInstance& quest : entity.quests.active) {
        if (!wantedObjective.empty() && lower_text(quest.objective) != wantedObjective) {
            continue;
        }
        if (wantedTask.empty()) {
            return quest_state::kActive;
        }
        const QuestObjective* row = m_data->objective(quest.objective);
        if (row == nullptr) {
            continue;
        }
        for (std::size_t index = 0; index < row->nodes.size() && index < quest.tasks.size(); ++index) {
            if (row->nodes[index].task != gamedata::kNoTask && lower_text(m_data->task(row->nodes[index].task).id) == wantedTask) {
                return quest.tasks[index].state;
            }
        }
    }
    return std::nullopt;
}

bool Quests::objective_completed(const Entity& entity, std::string_view objective) const {
    const QuestObjective* row = m_data != nullptr && m_data->loaded() ? m_data->objective(objective) : nullptr;
    return row != nullptr && !row->achievement.empty() && conditional(entity, row->achievement);
}

void Quests::achievements_changed(Entity& entity) {
    if (data() == nullptr || !entity.quests.loaded) {
        return;
    }
    // Stage QG2a: a change of the bits is a change of the push rows (the next tick looks at the rows of the changed bits).
    m_pushDirty.insert(entity.id);
    if (entity.quests.active.empty()) {
        return;
    }
    ++m_counts.events;
    refresh(entity, false, false, true, false, "achievement change");
}

void Quests::refresh(Entity& entity, bool items, bool places, bool achievements, bool levels, const char* cause) {
    if (data() == nullptr || !entity.quests.loaded) {
        return;
    }
    const auto found = m_subscriptions.find(entity.id);
    const Subscriptions* subscriptions = found != m_subscriptions.end() ? found->second.get() : nullptr;
    if (subscriptions == nullptr) {
        return;
    }
    Plan plan(*this, entity, now_ns());
    const auto look = [&](const char* key) {
        const std::vector<Subscriber>* list = subscriptions->find(key);
        if (list == nullptr) {
            return;
        }
        const std::vector<Subscriber> copy = *list;
        for (const Subscriber& entry : copy) {
            ++m_counts.subscribersVisited;
            const QuestInstance* seen = plan.peek(entry.instance);
            const QuestTask* task = seen != nullptr ? plan.task_of(*seen, entry.index) : nullptr;
            if (task == nullptr || !running(seen->tasks[entry.index].state)) {
                continue;
            }
            if (task->type == task_type::kGiveItem || task->type == task_type::kGiveAndTakeItem) {
                // A give that waited for a place in the bag tries again when the bag has the place now.
                if (seen->tasks[entry.index].state == quest_state::kActive
                    && plan.fits(GrantItem{task->itemName, plan.objective_of(*seen)->nodes[entry.index].target, std::nullopt, {}})) {
                    QuestInstance* quest = plan.edit(entry.instance);
                    quest->tasks[entry.index].state = quest_state::kNew;
                    plan.activate(*quest, entry.index);
                }
                continue;
            }
            if (task->type == task_type::kTriggerGameEvent) {
                // A game event whose item waited for a place in the bag (code review of stage 1.8b, finding QR8B-R11): the activation
                // again, with all its effects in this plan, when the item fits now.
                const gamedata::QuestGameEvent* event = task->targets.empty() ? nullptr : m_data->game_event(task->targets.front());
                if (seen->tasks[entry.index].state == quest_state::kActive && event != nullptr && event->itemQty > 0
                    && plan.fits(GrantItem{event->item, static_cast<std::uint32_t>(event->itemQty), std::nullopt, {}})) {
                    QuestInstance* quest = plan.edit(entry.instance);
                    quest->tasks[entry.index].state = quest_state::kNew;
                    plan.activate(*quest, entry.index);
                }
                continue;
            }
            const QuestObjective* objective = plan.objective_of(*seen);
            const std::optional<std::uint32_t> value = plan.predicate(*task, objective->nodes[entry.index]);
            if (!value) {
                continue;
            }
            const QuestTaskState& state = seen->tasks[entry.index];
            const bool satisfied = *value >= objective->nodes[entry.index].target;
            if (state.state == quest_state::kActive ? *value == state.progress && !satisfied : satisfied) {
                continue; // No change: no copy of the quest.
            }
            plan.set_progress(*plan.edit(entry.instance), entry.index, *value);
        }
    };
    if (items) {
        look("items");
    }
    if (places) {
        look("place");
    }
    if (levels) {
        look("level");
    }
    // Code review of stage QG5b, finding QG5BR-R2: a task change of this refresh (an item, a place, a level) can change an ObjectiveFlow of an
    // NPA (the task states), thus the indexed achievement waits of the character are settled again too (the fixed point of the plan).
    plan.settle(achievements || !plan.actions.empty() || !plan.changed.empty() ? subscriptions : nullptr);
    if (plan.changed.empty() && plan.actions.empty()) {
        return;
    }
    const QuestError error = commit(entity, plan, {});
    if (error != QuestError::none) {
        log::write("quests: the %s of character %s did not change its quests: %s", cause, entity.characterId.c_str(), quest_error_text(error));
    }
}

void Quests::timer_due(const std::string& characterId, std::uint64_t instance, std::uint8_t index, std::uint64_t startNs) {
    Entity* entity = m_world.find_character(characterId);
    if (entity == nullptr || data() == nullptr) {
        return; // The character left: its next entry starts the timer again from the saved start.
    }
    Plan plan(*this, *entity, now_ns());
    const QuestInstance* seen = plan.peek(instance);
    if (seen == nullptr || index >= seen->tasks.size() || !running(seen->tasks[index].state) || seen->tasks[index].startNs != startNs) {
        return; // An old timer: the task ended, or a later activation has its own timer.
    }
    QuestInstance* quest = plan.edit(instance);
    const QuestObjective* objective = plan.objective_of(*quest);
    const QuestTask& task = m_data->task(objective->nodes[index].task);
    if (task.failOnCondition) {
        // FailOnCondition: the end of the timer fails the task and its objective (client 0x3B2FFA0 with 0x3B2E970: the opposite progress
        // transition, then Failed; the objective goes with no completed entry: the banner "Objective Failed", live L1 of stage 1.8a).
        quest->tasks[index].state = quest_state::kFailed;
        quest->tasks[index].progress = 0;
        quest->tasks[index].startNs = 0;
        plan.changed.insert(instance);
        end_instance(plan, instance, true, "its failure timer ended");
        ++m_counts.failures;
    } else {
        plan.complete(*quest, index);
    }
    const auto found = m_subscriptions.find(entity->id);
    plan.settle(found != m_subscriptions.end() ? found->second.get() : nullptr);
    const QuestError error = commit(*entity, plan, {});
    if (error == QuestError::none) {
        ++m_counts.timersDone;
    } else if (error == QuestError::storeFailed) {
        // The task and its rows stay as they were; the same timer (its instance, index, and start) tries again (finding QR8A-R5).
        ++m_counts.timerRetries;
        m_world.after(kTimerRetry, [characterId, instance, index, startNs](World& world) { world.quests().timer_due(characterId, instance, index, startNs); });
    }
    log::write("quests: the timer %s of character %s (instance %016llx, task %u) ended after %.3f s: %s", task.id.c_str(), characterId.c_str(),
               static_cast<unsigned long long>(instance), static_cast<unsigned>(index),
               static_cast<double>(now_ns() - startNs) / 1e9,
               error == QuestError::none ? (task.failOnCondition ? "failed" : "complete")
                                         : (error == QuestError::storeFailed ? "the store did not take it, a retry follows" : quest_error_text(error)));
}

bool Quests::client_tracked(Entity& entity, const std::vector<std::uint64_t>& instances) {
    if (data() == nullptr || !entity.quests.loaded) {
        return false;
    }
    std::vector<std::uint64_t> tracked;
    for (const std::uint64_t instance : instances) {
        const bool known = std::any_of(entity.quests.active.begin(), entity.quests.active.end(),
                                       [&](const QuestInstance& quest) { return quest.instance == instance; });
        if (!known || std::find(tracked.begin(), tracked.end(), instance) != tracked.end() || tracked.size() >= kMaxTracked) {
            log::write("quests: the pinned list of the client of character %s has the instance %016llx, which is not an active quest of the "
                       "character (or is there two times): no change",
                       entity.characterId.c_str(), static_cast<unsigned long long>(instance));
            return false;
        }
        tracked.push_back(instance);
    }
    if (tracked == entity.quests.tracked) {
        return true;
    }
    ++m_counts.trackedUpdates;
    if (!m_store.write_quest_pins(entity.characterId, tracked)) {
        return false;
    }
    // The list of the client is the truth: no new state goes back to it (research quests.md 6.2).
    entity.quests.tracked = std::move(tracked);
    return true;
}

TemplateResult Quests::apply_template(const std::string& characterId, const std::string& templateId) {
    TemplateResult result;
    if (data() == nullptr) {
        result.error = QuestError::noData;
        return result;
    }
    const gamedata::QuestBackstory* backstory = m_data->backstory(templateId);
    if (backstory == nullptr || backstory->archetype) {
        result.error = QuestError::unknownTemplate;
        return result;
    }
    if (!m_store.find_character(characterId)) {
        result.error = QuestError::unknownCharacter;
        return result;
    }
    for (const std::string& id : backstory->achievements) {
        (m_world.ownership().achievement_bit(id) ? result.achievements : result.achievementsWithNoBit).push_back(id);
    }
    std::vector<const QuestObjective*> objectives;
    for (const std::string& id : backstory->objectives) {
        if (const QuestObjective* objective = m_data->objective(id)) {
            objectives.push_back(objective);
            result.objectives.push_back(objective->id);
        } else {
            result.objectivesWithNoRow.push_back(id);
        }
    }
    std::vector<std::string> rows = result.achievements;
    rows.insert(rows.end(), result.achievementsWithNoBit.begin(), result.achievementsWithNoBit.end());
    Entity* entity = m_world.find_character(characterId);
    result.inWorld = entity != nullptr;
    if (entity == nullptr) {
        // The rows only: the achievements, and the objectives as pending grants for the next world entry.
        state::Store::Batch batch(m_store);
        bool ok = batch.open() && (rows.empty() || m_store.grant_achievements(characterId, rows, nullptr));
        for (const QuestObjective* objective : objectives) {
            ok = ok && m_store.write_quest_grant(characterId, state::QuestGrantRow{"backstory", backstory->id + "/" + objective->id, objective->id,
                                                                               "pending", 0});
        }
        if (!(ok && batch.commit())) {
            result.error = QuestError::storeFailed;
        }
        return result;
    }
    Plan plan(*this, *entity, now_ns());
    for (const std::string& id : rows) {
        plan.action(id, true);
    }
    std::vector<state::QuestGrantRow> grants;
    for (const QuestObjective* objective : objectives) {
        std::uint64_t instance = 0;
        for (const QuestInstance* quest : plan.all()) {
            if (quest->objectiveCrc == objective->crc) {
                instance = quest->instance;
            }
        }
        if (instance == 0) {
            instance = next_instance();
            plan.add(*objective, instance, "backstory", 0);
            if (plan.tracked.size() < kMaxTracked) {
                plan.tracked.push_back(instance);
                plan.trackedChanged = true;
            }
        }
        grants.push_back(state::QuestGrantRow{"backstory", backstory->id + "/" + objective->id, objective->id, "granted", instance});
    }
    const auto found = m_subscriptions.find(entity->id);
    plan.settle(found != m_subscriptions.end() ? found->second.get() : nullptr);
    result.error = commit(*entity, plan, grants);
    if (result.ok()) {
        log::write("quests: character %s got the backstory template %s: %zu achievement(s), %zu objective(s), %zu objective(s) with no row",
                   characterId.c_str(), backstory->id.c_str(), rows.size(), result.objectives.size(), result.objectivesWithNoRow.size());
    }
    return result;
}

std::optional<QuestView> Quests::view(const std::string& characterId) {
    if (!m_store.find_character(characterId)) {
        return std::nullopt;
    }
    QuestView view;
    view.grants = m_store.list_quest_grants(characterId);
    view.awards = m_store.list_quest_awards(characterId);
    const Entity* entity = m_world.find_character(characterId);
    view.inWorld = entity != nullptr && entity->quests.loaded;
    if (!view.inWorld) {
        view.quests = m_store.list_quests(characterId);
        view.tracked = m_store.list_quest_pins(characterId);
        return view;
    }
    for (const QuestInstance& quest : entity->quests.active) {
        state::QuestRow row;
        row.instance = quest.instance;
        row.objective = quest.objective;
        row.objectiveCrc = quest.objectiveCrc;
        row.acceptNs = quest.acceptNs;
        row.originTerritory = quest.originTerritory;
        row.parent = quest.parent;
        row.source = quest.source;
        const QuestObjective* objective = m_data != nullptr ? m_data->objective_by_crc(quest.objectiveCrc) : nullptr;
        for (std::size_t index = 0; objective != nullptr && index < quest.tasks.size(); ++index) {
            if (objective->nodes[index].task == gamedata::kNoTask) {
                continue;
            }
            const QuestTask& task = m_data->task(objective->nodes[index].task);
            row.tasks.push_back(state::QuestTaskRow{static_cast<std::uint32_t>(index), task.id, task.crc, quest.tasks[index].progress,
                                                    quest.tasks[index].state, quest.tasks[index].startNs});
        }
        view.quests.push_back(std::move(row));
    }
    view.tracked = entity->quests.tracked;
    return view;
}

state::NewCharacterRows Quests::new_character_rows(const gamedata::QuestData* data, const std::string& archetype,
                                                   std::optional<state::WorldPosition>& position) {
    state::NewCharacterRows rows;
    if (data == nullptr || !data->loaded()) {
        return rows;
    }
    // The archetype achievement: the AchievementUnlockOverride of the backstory row of the archetype (research quest-engine-3.md Q1).
    if (const gamedata::QuestBackstory* backstory = data->backstory(archetype); backstory != nullptr && backstory->archetype) {
        rows.achievements = backstory->achievements;
    }
    // The start objective of the start territory (TerritoryDefinition.ObjectiveID), one time, at the first world entry.
    if (const QuestObjective* start = data->territory_objective(kStartTerritory)) {
        rows.grants.push_back(state::QuestGrantRow{"territory", std::to_string(kStartTerritory), start->id, "pending", 0});
        position = kBeachStart;
    }
    return rows;
}

// ----- Stage 1.8c: the events of the other systems -----

void Quests::event(Entity& entity, const QuestEvent& event) {
    if (data() == nullptr || !entity.quests.loaded || entity.kind != EntityKind::player) {
        return;
    }
    ++m_counts.worldEvents;
    const std::uint64_t result = event.result != 0 ? result_key(event.result, entity.id, event.kind) : 0;
    if (result != 0 && repeated(result)) {
        ++m_counts.repeatedResults;
        return;
    }
    const auto found = m_subscriptions.find(entity.id);
    const Subscriptions* subscriptions = found != m_subscriptions.end() ? found->second.get() : nullptr;
    if (subscriptions == nullptr) {
        return;
    }
    const std::vector<std::tuple<std::uint64_t, std::uint8_t, std::uint32_t>> matched = matched_tasks(entity, event, *subscriptions);
    if (matched.empty()) {
        if (result != 0) {
            remember(result);
        }
        return;
    }
    Plan plan(*this, entity, now_ns());
    for (const auto& [instance, index, amount] : matched) {
        if (QuestInstance* quest = plan.edit(instance)) {
            plan.add_progress(*quest, index, amount);
        }
    }
    plan.settle(subscriptions);
    const QuestError error = commit(entity, plan, {});
    if (error == QuestError::none) {
        if (result != 0) {
            remember(result); // Code review of stage 1.8c, finding QR8C-R1: a result counts as handled only after its commit.
        }
        return;
    }
    log::write("quests: an event of character %s did not change its quests: %s", entity.characterId.c_str(), quest_error_text(error));
    if (error == QuestError::storeFailed && event.attempt < kEventRetries) {
        // The source action committed: its quest event tries again after 2 s on the world clock (the rule of a refused timer write,
        // finding QR8A-R5). A delivery of the same result before the retry counts one time.
        ++m_counts.eventRetries;
        QuestEvent again = event;
        ++again.attempt;
        if (event.attempt == 0 && !event.restricted) {
            for (const auto& [instance, index, amount] : matched) {
                again.matched.emplace_back(instance, index);
            }
        }
        m_world.after(kEventRetry, [characterId = entity.characterId, again](World& world) {
            if (Entity* character = world.find_character(characterId)) {
                world.quests().event(*character, again);
            }
        });
    }
}

std::vector<std::tuple<std::uint64_t, std::uint8_t, std::uint32_t>> Quests::matched_tasks(const Entity& entity, const QuestEvent& event,
                                                                                         const Subscriptions& subscriptions) {
    // The keys of the event, and the amount of each one.
    std::vector<std::pair<std::string, std::uint32_t>> keys;
    for (std::size_t position = 0; position < event.keys.size(); ++position) {
        const std::string key = lower_text(event.keys[position]);
        const std::uint32_t amount = position < event.amounts.size() ? event.amounts[position] : 1U;
        switch (event.kind) {
        case QuestEventKind::kill:
            keys.emplace_back("k:" + key, 1U);
            break;
        case QuestEventKind::gameEvent:
            keys.emplace_back("e:" + key, amount);
            break;
        case QuestEventKind::gather:
            keys.emplace_back("g:" + key, amount);
            break;
        case QuestEventKind::craft:
            keys.emplace_back("c:" + key, amount);
            keys.emplace_back("c:*", amount);
            break;
        case QuestEventKind::refine:
            // Stage 1.8d (live check of the beach chain): a recipe with IsRefining is a craft of its recipe too. TaskCraftRecipe of
            // 06_boarfeast names Procedural_HealthRecoveryT1 (IsRefining 1, the Light Ration of the camp): the main story counts it (the
            // game data; a decision of an agent: the server rule of the original game is not in the client). TaskRefineRecipe counts only
            // a refining recipe.
            keys.emplace_back("r:" + key, amount);
            keys.emplace_back("c:" + key, amount);
            keys.emplace_back("c:*", amount);
            break;
        case QuestEventKind::interact:
            keys.emplace_back("i:" + key, 1U);
            break;
        case QuestEventKind::topic:
            keys.emplace_back("ct:" + key, 1U);
            break;
        case QuestEventKind::state:
            keys.emplace_back("cs:" + key, 1U);
            break;
        case QuestEventKind::screen:
        case QuestEventKind::cutscene:
        case QuestEventKind::cinematicStart:
        case QuestEventKind::cinematicEnd:
            break;
        }
    }
    if (event.kind == QuestEventKind::screen) {
        keys.emplace_back("s:" + std::to_string(event.screenCrc), 1U);
    } else if (event.kind == QuestEventKind::cutscene) {
        keys.emplace_back("cut:" + std::to_string(event.screenCrc), 1U);
    } else if (event.kind == QuestEventKind::cinematicStart || event.kind == QuestEventKind::cinematicEnd) {
        keys.emplace_back((event.kind == QuestEventKind::cinematicStart ? "cin0:" : "cin1:") + std::to_string(event.screenCrc), 1U);
    }
    // The matching tasks: each subscriber of each key once (a kill of a victim with the categories "Corrupted" and "Pirate" counts one
    // time for a task that names both). A retry (code review of stage 2.5c, finding PR5C-R5) looks only at the tasks of the first delivery.
    std::vector<std::pair<Subscriber, std::uint32_t>> matched;
    const auto amount_of = [&](const Subscriber& entry) {
        std::uint32_t amount = 1;
        for (const auto& [key, keyAmount] : keys) {
            const std::vector<Subscriber>* list = subscriptions.find(key);
            if (list != nullptr && std::find(list->begin(), list->end(), entry) != list->end()) {
                amount = keyAmount;
                break;
            }
        }
        return amount;
    };
    std::vector<std::pair<std::string, std::uint32_t>> lookups = keys;
    if (event.attempt > 0 || event.restricted) {
        lookups.clear();
        for (const auto& [instance, index] : event.matched) {
            const Subscriber entry{instance, index};
            ++m_counts.subscribersVisited;
            const QuestInstance* quest = instance_of(entity, instance);
            const QuestObjective* objective = quest != nullptr ? m_data->objective_by_crc(quest->objectiveCrc) : nullptr;
            if (objective == nullptr || index >= quest->tasks.size() || quest->tasks[index].state != quest_state::kActive) {
                continue; // The quest of the first delivery ended (a reset gives a new instance): no credit for the replacement.
            }
            matched.emplace_back(entry, amount_of(entry));
        }
    }
    for (const auto& [key, amount] : lookups) {
        const std::vector<Subscriber>* list = subscriptions.find(key);
        for (std::size_t position = 0; list != nullptr && position < list->size(); ++position) {
            const Subscriber entry = (*list)[position];
            ++m_counts.subscribersVisited;
            if (std::any_of(matched.begin(), matched.end(), [&](const auto& seen) { return seen.first == entry; })) {
                continue;
            }
            const QuestInstance* quest = instance_of(entity, entry.instance);
            const QuestObjective* objective = quest != nullptr ? m_data->objective_by_crc(quest->objectiveCrc) : nullptr;
            if (objective == nullptr || entry.index >= quest->tasks.size() || quest->tasks[entry.index].state != quest_state::kActive) {
                continue;
            }
            const QuestTask& task = m_data->task(objective->nodes[entry.index].task);
            if (event.kind == QuestEventKind::kill && !task.weapon.empty() && task.weapon != lower_text(event.weapon)) {
                continue; // KillEnemyWeaponID (0x825E6B0): the item of the kill.
            }
            // The smallest gear score of a crafted item (0x825DE70). Code review of stage 1.8d, finding QR8D-R4: a refine that reaches a craft
            // task (the rule of stage 1.8d above) has the same check.
            if ((event.kind == QuestEventKind::craft || (event.kind == QuestEventKind::refine && task.type != task_type::kRefineRecipe))
                && task.minGearScore != 0 && event.gearScore < task.minGearScore) {
                continue;
            }
            if (task.type == task_type::kInteract && task.onInteractStart != event.started) {
                continue; // OnInteractStart: the start or the end of the interaction (0x825E470).
            }
            if (task.enforceLocation && !task.poiTags.empty()) {
                const gamedata::PoiData* data = pois();
                if (data == nullptr || entity.positionEstimated
                    || !data->inside(task.poiTags, task.territory, entity.position.x, entity.position.y)) {
                    continue; // EnforceLocationRequirement (0x825DAD0): only in the place of the task.
                }
            }
            matched.emplace_back(entry, amount);
        }
    }
    std::vector<std::tuple<std::uint64_t, std::uint8_t, std::uint32_t>> found;
    found.reserve(matched.size());
    for (const auto& [entry, amount] : matched) {
        found.emplace_back(entry.instance, entry.index, amount);
    }
    return found;
}

QuestError Quests::object_destroyed(Entity& entity, const std::vector<QuestDrop>& drops, const std::vector<QuestEvent>& events) {
    if (data() == nullptr || !entity.quests.loaded || entity.kind != EntityKind::player) {
        return QuestError::none; // No quest of the character: the destruction has no row of the quest system.
    }
    const auto found = m_subscriptions.find(entity.id);
    Subscriptions* subscriptions = found != m_subscriptions.end() ? found->second.get() : nullptr;
    std::vector<std::tuple<std::uint64_t, std::uint8_t, std::uint32_t>> matched;
    for (const QuestEvent& event : events) {
        ++m_counts.worldEvents;
        if (subscriptions != nullptr) {
            for (const auto& task : matched_tasks(entity, event, *subscriptions)) {
                matched.push_back(task);
            }
        }
    }
    if (drops.empty() && matched.empty()) {
        return QuestError::none;
    }
    // One plan: the quest drops (a give of the items, with the cause ItemGathered of the plan of the quest items, whole or not at all; the
    // award rows of AwardOnlyOnce), then the progress of the tasks of the events.
    Plan plan(*this, entity, now_ns());
    for (const QuestDrop& drop : drops) {
        plan.itemActions.push_back(QuestItemAction{true, GrantItem{drop.itemId, drop.quantity, std::nullopt, {}}});
        plan.itemDelta[lower_text(drop.itemId)] += drop.quantity;
        if (drop.counted) {
            const auto key = std::make_pair(drop.objectiveCrc, drop.index);
            const auto planned = plan.awards.find(key);
            const auto stored = entity.quests.awards.find(key);
            const std::uint32_t before = planned != plan.awards.end() ? planned->second : stored != entity.quests.awards.end() ? stored->second : 0U;
            plan.awards[key] = before + drop.quantity;
        }
    }
    for (const auto& [instance, index, amount] : matched) {
        if (QuestInstance* quest = plan.edit(instance)) {
            plan.add_progress(*quest, index, amount);
        }
    }
    plan.settle(subscriptions);
    const QuestError error = commit(entity, plan, {});
    if (error != QuestError::none) {
        log::write("quests: the destruction of a quest object for character %s did not change its quests: %s", entity.characterId.c_str(),
                   quest_error_text(error));
        return error;
    }
    for (const QuestDrop& drop : drops) {
        ++m_counts.drops;
        m_misses.erase({entity.characterId, drop.instance, drop.index});
        log::write("quests: character %s got the quest drop %u %s (objective %s, task %u)", entity.characterId.c_str(), drop.quantity,
                   drop.itemId.c_str(), drop.objective.c_str(), static_cast<unsigned>(drop.index));
    }
    return QuestError::none;
}

void Quests::killed(const Entity& creature, const std::vector<Entity*>& credited, const std::vector<std::string>& categories,
                    const std::string& killEvent, std::uint64_t result) {
    for (Entity* character : credited) {
        if (character == nullptr) {
            continue;
        }
        QuestEvent kill;
        kill.kind = QuestEventKind::kill;
        kill.keys = categories;
        kill.result = result;
        event(*character, kill);
        if (!killEvent.empty()) {
            QuestEvent applied;
            applied.kind = QuestEventKind::gameEvent;
            applied.keys = {killEvent};
            applied.result = result;
            event(*character, applied);
        }
    }
    (void)creature;
}

std::vector<QuestDrop> Quests::plan_drops(Entity& entity, bool gather, const std::vector<std::string>& sources) {
    std::vector<QuestDrop> drops;
    if (data() == nullptr || !entity.quests.loaded) {
        return drops;
    }
    const auto found = m_subscriptions.find(entity.id);
    const Subscriptions* subscriptions = found != m_subscriptions.end() ? found->second.get() : nullptr;
    if (subscriptions == nullptr) {
        return drops;
    }
    std::vector<Subscriber> seen;
    for (const std::string& source : sources) {
        const std::vector<Subscriber>* list = subscriptions->find((gather ? "gd:" : "kd:") + lower_text(source));
        for (std::size_t position = 0; list != nullptr && position < list->size(); ++position) {
            const Subscriber entry = (*list)[position];
            ++m_counts.subscribersVisited;
            if (std::find(seen.begin(), seen.end(), entry) != seen.end()) {
                continue;
            }
            seen.push_back(entry);
            const QuestInstance* quest = instance_of(entity, entry.instance);
            const QuestObjective* objective = quest != nullptr ? m_data->objective_by_crc(quest->objectiveCrc) : nullptr;
            if (objective == nullptr) {
                continue;
            }
            const QuestTask& task = m_data->task(objective->nodes[entry.index].task);
            if (std::optional<QuestDrop> drop = roll_drop(entity, entry.instance, entry.index, task.dropChance)) {
                drops.push_back(std::move(*drop));
            }
        }
    }
    return drops;
}

std::vector<QuestDrop> Quests::plan_chest_drops(Entity& entity, const state::WorldPosition& chest) {
    std::vector<QuestDrop> drops;
    if (data() == nullptr || !entity.quests.loaded) {
        return drops;
    }
    const auto found = m_subscriptions.find(entity.id);
    const std::vector<Subscriber>* list = found != m_subscriptions.end() && found->second ? found->second->find("cd") : nullptr;
    for (std::size_t position = 0; list != nullptr && position < list->size(); ++position) {
        const Subscriber entry = (*list)[position];
        ++m_counts.subscribersVisited;
        const QuestInstance* quest = instance_of(entity, entry.instance);
        const QuestObjective* objective = quest != nullptr ? m_data->objective_by_crc(quest->objectiveCrc) : nullptr;
        if (objective == nullptr) {
            continue;
        }
        const QuestTask& task = m_data->task(objective->nodes[entry.index].task);
        // The place of the chest: a task with a POITag takes only the chests in a point of interest of its tags (and of its TerritoryID);
        // a task with no POITag takes each chest (docs/systems/quests.md, the quest chest drops).
        if (!task.poiTags.empty() && (pois() == nullptr || !pois()->inside(task.poiTags, task.territory, chest.x, chest.y))) {
            continue;
        }
        if (std::optional<QuestDrop> drop = roll_drop(entity, entry.instance, entry.index, task.chestChance, true)) {
            drops.push_back(std::move(*drop));
        }
    }
    return drops;
}

std::optional<QuestDrop> Quests::roll_drop(Entity& entity, std::uint64_t instance, std::uint8_t index, double chance, bool chest) {
    const QuestInstance* quest = instance_of(entity, instance);
    const QuestObjective* objective = quest != nullptr ? m_data->objective_by_crc(quest->objectiveCrc) : nullptr;
    if (objective == nullptr || index >= quest->tasks.size() || quest->tasks[index].state != quest_state::kActive) {
        return std::nullopt;
    }
    const QuestTask& task = m_data->task(objective->nodes[index].task);
    const std::uint32_t target = objective->nodes[index].target;
    // PauseAndClearLootOnTaskComplete: no drop while the character has the target count (0x825F3B0).
    if (task.pauseLoot && Inventory::count(entity, task.itemName) >= target) {
        return std::nullopt;
    }
    const auto key = std::make_pair(quest->objectiveCrc, index);
    const auto award = entity.quests.awards.find(key);
    if (task.awardOnce && award != entity.quests.awards.end() && award->second >= target) {
        return std::nullopt; // AwardOnlyOnce: the task gave its target count of items (0x825F8F0).
    }
    if (task.enforceLocation && !task.poiTags.empty()
        && (pois() == nullptr || entity.positionEstimated || !pois()->inside(task.poiTags, task.territory, entity.position.x, entity.position.y))) {
        return std::nullopt;
    }
    // The roll: the chance in percent; GuranteedDropAttempts: the drop after that many misses is sure (0x825F630). Stage QG5a: the chest
    // chance takes the roll of the client (the integer part of a number of 0 to 100 at or below the chance: 40 is 41 of 100; 0x3B0B2F3 to
    // 0x3B0B336); the other chances a uniform number below the chance.
    std::uint32_t& misses = m_misses[{entity.characterId, instance, index}];
    const bool sure = task.guaranteedAttempts != 0 && misses >= task.guaranteedAttempts;
    const double roll = static_cast<double>(m_world.random_unit()) * 100.0;
    if (!sure && !(chest ? std::floor(roll) <= chance : roll < chance)) {
        ++misses;
        ++m_counts.dropMisses;
        return std::nullopt;
    }
    // One item for each kill, gather, or chest (a decision of an agent: the data has no quantity of a drop).
    return QuestDrop{task.itemName, 1, quest->objectiveCrc, quest->objective, instance, index, task.awardOnce};
}

bool Quests::write_drops(const Entity& entity, const std::vector<QuestDrop>& drops) {
    for (const QuestDrop& drop : drops) {
        if (!drop.counted) {
            continue;
        }
        const auto award = entity.quests.awards.find({drop.objectiveCrc, drop.index});
        const std::uint32_t amount = (award != entity.quests.awards.end() ? award->second : 0U) + drop.quantity;
        if (!m_store.write_quest_award(entity.characterId, state::QuestAwardRow{drop.objective, drop.index, amount})) {
            return false;
        }
    }
    return true;
}

void Quests::drops_done(Entity& entity, const std::vector<QuestDrop>& drops) {
    for (const QuestDrop& drop : drops) {
        ++m_counts.drops;
        m_misses.erase({entity.characterId, drop.instance, drop.index});
        if (drop.counted) {
            entity.quests.awards[{drop.objectiveCrc, drop.index}] += drop.quantity;
        }
        log::write("quests: character %s got the quest drop %u %s (objective %s, task %u)", entity.characterId.c_str(), drop.quantity,
                   drop.itemId.c_str(), drop.objective.c_str(), static_cast<unsigned>(drop.index));
    }
}

bool Quests::quest_object_open(const Entity& entity, const std::string& interactTag) const {
    if (interactTag.empty()) {
        return false;
    }
    const auto found = m_subscriptions.find(entity.id);
    return found != m_subscriptions.end() && found->second && found->second->find("t:" + lower_text(interactTag)) != nullptr;
}

void Quests::items_touched(const Entity& entity) {
    const auto found = m_subscriptions.find(entity.id);
    if (found != m_subscriptions.end() && found->second && found->second->find("items") != nullptr) {
        m_itemsDirty.insert(entity.id);
    }
}

void Quests::moved(const Entity& entity) {
    const auto found = m_subscriptions.find(entity.id);
    if (found != m_subscriptions.end() && found->second && found->second->find("place") != nullptr) {
        m_placesDirty.insert(entity.id);
    }
}

void Quests::item_equipped(const Entity& entity, const std::string& itemId, std::uint32_t gearScore) {
    const gamedata::ItemCatalog* catalog = m_world.options().itemCatalog;
    if (data() == nullptr || !entity.quests.loaded || catalog == nullptr || catalog->artifact_objective(itemId) == nullptr) {
        return;
    }
    m_equips[entity.id].emplace_back(itemId, gearScore);
}

void Quests::equip_grant(Entity& entity, const std::string& itemId, std::uint32_t gearScore) {
    const gamedata::ItemCatalog* catalog = m_world.options().itemCatalog;
    const gamedata::ArtifactObjective* artifact = catalog != nullptr ? catalog->artifact_objective(itemId) : nullptr;
    if (artifact == nullptr || data() == nullptr || !entity.quests.loaded) {
        return;
    }
    ++m_counts.equipChecks;
    const QuestObjective* objective = m_data->objective(artifact->objectiveId);
    if (objective == nullptr) {
        log::write("quests: the equip of %s by character %s grants nothing: the quest data has no objective %s", itemId.c_str(),
                   entity.characterId.c_str(), artifact->objectiveId.c_str());
        return;
    }
    // The item level of the gear score of the instance (research quest-engine.md Q7: GetLevelRequirement 0x5C4C960 with the equip-cap
    // lookup 0x4403360 for an item with an empty RequiredLevel; the artifacts of gear score 700 need the level 65). The client equips no
    // item above the level of its player; the backend grants the objective only at that level.
    const gamedata::ProgressionTables* tables = m_world.options().progressionTables;
    const std::uint32_t level = tables != nullptr && tables->loaded() ? tables->item_level(gearScore) : 0;
    if (level == 0 || entity.progression.level < level) {
        log::write("quests: the equip of %s (gear score %u) by character %s grants no %s: %s", itemId.c_str(), static_cast<unsigned>(gearScore),
                   entity.characterId.c_str(), objective->id.c_str(),
                   level == 0 ? "the progression tables have no item level (equipGearScores)"
                              : ("the level " + std::to_string(entity.progression.level) + " is below the item level "
                                 + (level == gamedata::kNoItemLevel ? std::string("(no level admits the gear score)") : std::to_string(level)))
                                    .c_str());
        return;
    }
    // The normal availability: no active instance (no second instance), the completion achievement, the gates of the row.
    if (const QuestError error = available(entity, *objective); error != QuestError::none) {
        log::write("quests: the equip of %s by character %s grants no %s: %s", itemId.c_str(), entity.characterId.c_str(), objective->id.c_str(),
                   quest_error_text(error));
        return;
    }
    const QuestResult result = grant(entity, objective->id, "equip", objective->exclusiveTerritory.value_or(0));
    if (result.ok()) {
        ++m_counts.equipGrants;
        log::write("quests: character %s got the objective %s by the equip of %s (OnEquipObjectiveId)", entity.characterId.c_str(),
                   objective->id.c_str(), itemId.c_str());
    } else {
        log::write("quests: the equip grant of %s to character %s: %s", objective->id.c_str(), entity.characterId.c_str(),
                   quest_error_text(result.error));
    }
}

void Quests::level_changed(const Entity& entity) {
    if (data() != nullptr && entity.quests.loaded) {
        m_levelsDirty.insert(entity.id);
    }
}

bool Quests::pushed(const QuestObjective& objective) {
    return objective.pushOnRequirement && std::none_of(kPushExcluded.begin(), kPushExcluded.end(), [&](const char* id) {
               return lower_text(objective.id) == lower_text(id);
           });
}

const Quests::PushIndex& Quests::push_index() {
    const auto& objectives = m_data->objectives();
    if (m_push && m_push->source == objectives.data() && m_push->count == objectives.size()) {
        return *m_push;
    }
    m_push = std::make_unique<PushIndex>();
    PushIndex& index = *m_push;
    index.source = objectives.data();
    index.count = objectives.size();
    std::set<std::uint32_t> bits;
    for (std::uint32_t at = 0; at < objectives.size(); ++at) {
        const QuestObjective& objective = objectives[at];
        if (!pushed(objective)) {
            continue;
        }
        index.rows.push_back(at);
        // The dependencies of the availability (Quests::available): the ids of RequiredAchievementId (with its non-persisted achievements)
        // and the completion achievement, and RequiredLevel.
        std::set<std::string> ids;
        conditional_ids(*m_data, objective.required, 0, ids);
        if (!objective.achievement.empty()) {
            conditional_ids(*m_data, objective.achievement, 0, ids);
        }
        for (const std::string& id : ids) {
            if (const std::optional<std::uint32_t> bit = m_world.ownership().achievement_bit(id)) {
                std::vector<std::uint32_t>& rows = index.byBit[*bit];
                if (std::find(rows.begin(), rows.end(), at) == rows.end()) {
                    rows.push_back(at);
                }
                bits.insert(*bit);
            }
        }
        if (objective.requiredLevel > 1) {
            index.byLevel.emplace_back(objective.requiredLevel, at);
        }
    }
    index.bits.assign(bits.begin(), bits.end());
    std::sort(index.byLevel.begin(), index.byLevel.end());
    log::write("quests: the push of PushObjectiveOnReqMet has %zu row(s) with %zu watched achievement(s) (decision B: %zu row(s) left out)",
               index.rows.size(), index.bits.size(), kPushExcluded.size());
    return index;
}

void Quests::push_changed(Entity& entity, bool all, const char* cause) {
    if (data() == nullptr || !entity.quests.loaded) {
        return;
    }
    const PushIndex& index = push_index();
    if (index.rows.empty()) {
        return;
    }
    ++m_counts.pushChecks;
    // The watched bits of the character now, against its last check (MP-8: the watched bits and the rows of the changed ones, not each row).
    PushWatch now;
    now.level = entity.progression.level;
    for (const std::uint32_t bit : index.bits) {
        if (std::binary_search(entity.achievements.begin(), entity.achievements.end(), bit)) {
            now.bits.push_back(bit);
        }
    }
    PushWatch& before = m_pushWatch[entity.id];
    std::set<std::uint32_t> candidates;
    if (all) {
        candidates.insert(index.rows.begin(), index.rows.end());
    } else {
        std::vector<std::uint32_t> changed;
        std::set_symmetric_difference(before.bits.begin(), before.bits.end(), now.bits.begin(), now.bits.end(), std::back_inserter(changed));
        for (const std::uint32_t bit : changed) {
            if (const auto rows = index.byBit.find(bit); rows != index.byBit.end()) {
                candidates.insert(rows->second.begin(), rows->second.end());
            }
        }
        if (now.level > before.level) {
            for (auto entry = std::upper_bound(index.byLevel.begin(), index.byLevel.end(), std::make_pair(before.level, std::uint32_t{0xFFFFFFFFU}));
                 entry != index.byLevel.end() && entry->first <= now.level; ++entry) {
                candidates.insert(entry->second);
            }
        }
    }
    before = std::move(now);
    // The grant: the normal availability (Quests::available: no active instance, the completion achievement, RequiredLevel,
    // RequiredAchievementId with the `_pushedonce` bits, the faction, the gates), and the ExclusiveTerritory of a push row (eligibility
    // 0x3A4C20F, 0x3A4C218: the character in that territory). The grant runs the first tasks of the tree: their actions set the bits
    // (`_pushedonce`) in its plan. Retrigger (PushObjectiveOnReqRetrigger) has no other rule here: a grant of one time (a project rule).
    for (const std::uint32_t at : candidates) {
        ++m_counts.pushVisits;
        const QuestObjective& objective = m_data->objectives()[at];
        if (available(entity, objective) != QuestError::none) {
            continue;
        }
        const std::uint16_t exclusive = objective.exclusiveTerritory.value_or(0);
        if (exclusive != 0 && m_world.territory_of(entity) != exclusive) {
            continue;
        }
        const QuestResult result = grant(entity, objective.id, "push", exclusive);
        if (result.ok()) {
            ++m_counts.pushGrants;
            log::write("quests: character %s got the objective %s by its push (PushObjectiveOnReqMet; %s)", entity.characterId.c_str(),
                       objective.id.c_str(), cause);
        } else {
            log::write("quests: the push of %s to character %s: %s", objective.id.c_str(), entity.characterId.c_str(), quest_error_text(result.error));
        }
    }
}

void Quests::place_reported(Entity& entity, std::uint16_t territory, bool entered) {
    const gamedata::PoiData* data = pois();
    const gamedata::Poi* poi = data != nullptr ? data->poi(territory) : nullptr;
    const bool inside = poi != nullptr && poi->contains(entity.position.x, entity.position.y);
    log::write("quests: the client of character %s reports that it %s the place %u (%s); the shape of the data says %s", entity.characterId.c_str(),
               entered ? "entered" : "left", static_cast<unsigned>(territory),
               poi != nullptr ? (poi->tags.empty() ? "no tag" : poi->tags.front().c_str()) : "no point of interest of the data",
               poi == nullptr ? "nothing" : (inside ? "inside" : "outside"));
    // A cue only: the membership of a task is the shape of the data with the accepted position (the next tick looks again).
    moved(entity);
}

void Quests::died(Entity& entity) {
    if (data() == nullptr || !entity.quests.loaded) {
        return;
    }
    const auto found = m_subscriptions.find(entity.id);
    const std::vector<Subscriber>* list = found != m_subscriptions.end() && found->second ? found->second->find("die") : nullptr;
    if (list == nullptr) {
        return;
    }
    Plan plan(*this, entity, now_ns());
    std::set<std::uint64_t> ending;
    std::vector<std::uint64_t> resets;
    for (const Subscriber& entry : *list) {
        ++m_counts.subscribersVisited;
        const QuestInstance* quest = plan.peek(entry.instance);
        const QuestTask* task = quest != nullptr ? plan.task_of(*quest, entry.index) : nullptr;
        if (task == nullptr || !running(quest->tasks[entry.index].state) || ending.count(entry.instance) != 0) {
            continue;
        }
        // TaskDie (0x825E5F0, 0x825E628): FailOnCondition fails the objective, ResetOnCondition resets it.
        if (task->failOnCondition) {
            ending.insert(entry.instance);
            end_instance(plan, entry.instance, true, "its character died (TaskDie)");
            ++m_counts.failures;
        } else if (task->resetOnCondition) {
            ending.insert(entry.instance);
            resets.push_back(entry.instance);
        }
    }
    for (const std::uint64_t instance : resets) {
        const QuestInstance* quest = plan.peek(instance);
        const QuestObjective* objective = quest != nullptr ? plan.objective_of(*quest) : nullptr;
        if (objective == nullptr) {
            continue;
        }
        const std::uint16_t territory = quest->originTerritory;
        const std::uint64_t parent = quest->parent;
        const auto position = std::find(plan.tracked.begin(), plan.tracked.end(), instance);
        const bool pinned = position != plan.tracked.end();
        end_instance(plan, instance, true, "its character died (TaskDie with ResetOnCondition)");
        const std::uint64_t next = next_instance();
        plan.add(*objective, next, "reset", territory, parent);
        if (pinned && plan.tracked.size() < kMaxTracked) {
            plan.tracked.push_back(next);
            plan.trackedChanged = true;
        }
        ++m_counts.resets;
    }
    if (plan.changed.empty() && plan.removed.empty()) {
        return;
    }
    plan.settle(found->second.get());
    const QuestError error = commit(entity, plan, {});
    if (error != QuestError::none) {
        log::write("quests: the death of character %s did not change its quests: %s", entity.characterId.c_str(), quest_error_text(error));
    }
}

void Quests::tick() {
    if (m_itemsDirty.empty() && m_placesDirty.empty() && m_levelsDirty.empty() && m_pushDirty.empty() && m_equips.empty()) {
        return;
    }
    // Stage QG4: the grants of the equipped artifacts first (a build preset gives the completion achievement of its artifacts in its
    // transaction, before this tick: no grant then).
    std::map<EntityId, std::vector<std::pair<std::string, std::uint32_t>>> equips;
    equips.swap(m_equips);
    for (const auto& [id, list] : equips) {
        Entity* entity = m_world.find(id);
        for (const auto& [itemId, gearScore] : list) {
            if (entity != nullptr && entity->kind == EntityKind::player) {
                equip_grant(*entity, itemId, gearScore);
            }
        }
    }
    std::set<EntityId> items;
    std::set<EntityId> places;
    std::set<EntityId> levels;
    std::set<EntityId> pushes;
    items.swap(m_itemsDirty);
    places.swap(m_placesDirty);
    levels.swap(m_levelsDirty);
    pushes.swap(m_pushDirty);
    std::set<EntityId> all = items;
    all.insert(places.begin(), places.end());
    all.insert(levels.begin(), levels.end());
    for (const EntityId id : all) {
        Entity* entity = m_world.find(id);
        if (entity == nullptr || entity->kind != EntityKind::player) {
            continue;
        }
        const bool item = items.count(id) != 0;
        const bool place = places.count(id) != 0;
        const bool level = levels.count(id) != 0;
        m_counts.itemLooks += item ? 1 : 0;
        m_counts.placeLooks += place ? 1 : 0;
        m_counts.levelLooks += level ? 1 : 0;
        refresh(*entity, item, place, false, level,
                item && place ? "change of the items and the place" : item ? "change of the items" : place ? "move" : "change of the level");
    }
    // Stage QG2a: the push after a change of the bits or of the level (a project rule: the schedule of the original server is not known).
    pushes.insert(levels.begin(), levels.end());
    for (const EntityId id : pushes) {
        Entity* entity = m_world.find(id);
        if (entity != nullptr && entity->kind == EntityKind::player) {
            push_changed(*entity, false, levels.count(id) != 0 ? "a change of the level" : "a change of the achievements");
        }
    }
}

void Quests::end_instance(Plan& plan, std::uint64_t instance, bool keepAwards, const char* cause, bool completed) {
    const QuestInstance* quest = plan.peek(instance);
    const QuestObjective* objective = quest != nullptr ? plan.objective_of(*quest) : nullptr;
    if (objective == nullptr) {
        return;
    }
    // The undo of the achievement actions (0x8260730, 0x8260880, 0x8260590: an unlock or a lock with no StayActive reverts, a temporary
    // unlock locks): the undo rows of the instance, and the rows that this plan adds.
    std::vector<state::QuestUndoRow> rows;
    for (const state::QuestUndoRow& row : m_store.list_quest_undo(plan.entity.characterId)) {
        if (row.instance == instance) {
            rows.push_back(row);
        }
    }
    for (const state::QuestUndoRow& row : plan.undo) {
        if (row.instance == instance) {
            rows.push_back(row);
        }
    }
    for (const state::QuestUndoRow& row : rows) {
        // The completion (stage 1.8b) reverts only a temporary unlock (client 0x8260590: "restore on completion, reset, or abandon"); an
        // unlock or a lock reverts at a reset or an abandon only (0x8260730, 0x8260880; research quest-progress.md). Live check P1: Aelstrom
        // 01 (the lock of task 13 of 06_getoffbeach) must stay for the offer of the next quest after the hand-in.
        if (completed) {
            const QuestTask* task = row.index <= 255 ? plan.task_of(*quest, static_cast<std::uint8_t>(row.index)) : nullptr;
            if (task == nullptr || task->type != task_type::kTemporaryUnlockAchievement) {
                continue;
            }
        }
        plan.action(row.achievement, row.action == "unlock");
    }
    plan.undo.erase(std::remove_if(plan.undo.begin(), plan.undo.end(), [&](const state::QuestUndoRow& row) { return row.instance == instance; }),
                    plan.undo.end());
    // The returns: a HaveAndReturn or GatherAndReturn task takes its target count (0x825F730, 0x825FB70: "removed when the objective is
    // completed, reset, or abandoned"); a GiveAndTake task takes its items back only when they are quest items (0x825F020). Only a task that
    // was active (a task that never started has no items of the quest: a decision of an agent).
    for (std::size_t index = 0; index < quest->tasks.size(); ++index) {
        const QuestTask* task = plan.task_of(*quest, static_cast<std::uint8_t>(index));
        if (task == nullptr || quest->tasks[index].state == quest_state::kNew) {
            continue;
        }
        const std::uint32_t target = objective->nodes[index].target;
        std::uint32_t take = 0;
        if (gamedata::is_return(task->type)) {
            take = static_cast<std::uint32_t>(std::min<std::uint64_t>(plan.item_count(task->itemName), target));
        } else if (task->type == task_type::kGiveAndTakeItem && done(quest->tasks[index].state)
                   && m_world.inventory().quest_item(task->itemName)) {
            take = static_cast<std::uint32_t>(std::min<std::uint64_t>(plan.item_count(task->itemName), target));
        }
        if (take > 0) {
            plan.itemActions.push_back(QuestItemAction{false, GrantItem{task->itemName, take, std::nullopt, {}}});
            plan.itemDelta[lower_text(task->itemName)] -= take;
        }
    }
    if (!keepAwards) {
        plan.awardDeletes.insert(objective->id);
    }
    const auto pin = std::find(plan.tracked.begin(), plan.tracked.end(), instance);
    if (pin != plan.tracked.end()) {
        plan.tracked.erase(pin);
        plan.trackedChanged = true;
    }
    plan.notes.push_back("the objective " + objective->id + " (instance " + hex(instance) + ") ends: " + cause);
    plan.removed.insert(instance);
    plan.changed.erase(instance);
    if (completed) {
        plan.completed.push_back(instance);
    }
}

QuestResult Quests::abandon(Entity& entity, std::uint64_t instance) {
    QuestResult result;
    if (data() == nullptr) {
        result.error = QuestError::noData;
        return result;
    }
    Plan plan(*this, entity, now_ns());
    const QuestInstance* quest = plan.peek(instance);
    const QuestObjective* objective = quest != nullptr ? plan.objective_of(*quest) : nullptr;
    if (objective == nullptr) {
        result.error = QuestError::unknownInstance;
        return result;
    }
    if (!objective->canAbandon) {
        result.error = QuestError::notAbandonable;
        return result;
    }
    end_instance(plan, instance, false, "abandoned");
    // The undo can lock a bit that a wait of a different quest has (code review of stage 1.8c, finding QR8C-R5): the indexed waits get the
    // value of now in this plan (the bit change of a plan sends no second change event).
    const auto found = m_subscriptions.find(entity.id);
    plan.settle(found != m_subscriptions.end() ? found->second.get() : nullptr);
    result.error = commit(entity, plan, {});
    if (result.ok()) {
        ++m_counts.abandons;
        result.instance = instance;
    }
    return result;
}

QuestResult Quests::reset(Entity& entity, std::uint64_t instance) {
    QuestResult result;
    if (data() == nullptr) {
        result.error = QuestError::noData;
        return result;
    }
    Plan plan(*this, entity, now_ns());
    const QuestInstance* quest = plan.peek(instance);
    const QuestObjective* objective = quest != nullptr ? plan.objective_of(*quest) : nullptr;
    if (objective == nullptr) {
        result.error = QuestError::unknownInstance;
        return result;
    }
    const std::uint16_t territory = quest->originTerritory;
    const std::string source = quest->source;
    const std::uint64_t parent = quest->parent;
    const auto pin = std::find(plan.tracked.begin(), plan.tracked.end(), instance);
    const std::ptrdiff_t position = pin != plan.tracked.end() ? pin - plan.tracked.begin() : -1;
    end_instance(plan, instance, true, "reset");
    // A new instance (state 0 and state 4 do not reset a task of the client: live finding of stage 1.8a), with the pin at the place of the
    // old one and the parent of the old one (code review of stage 1.8c, finding QR8C-R3: the completion of a reset child completes the task
    // of its parent). The award rows stay (AwardOnlyOnce: no item again after a reset, 0x825EEF0).
    const std::uint64_t next = next_instance();
    plan.add(*objective, next, source.empty() ? "reset" : source, territory, parent);
    if (position >= 0 && plan.tracked.size() < kMaxTracked) {
        plan.tracked.insert(plan.tracked.begin() + std::min<std::ptrdiff_t>(position, static_cast<std::ptrdiff_t>(plan.tracked.size())), next);
        plan.trackedChanged = true;
    }
    const auto found = m_subscriptions.find(entity.id);
    plan.settle(found != m_subscriptions.end() ? found->second.get() : nullptr);
    result.error = commit(entity, plan, {});
    if (result.ok()) {
        ++m_counts.resets;
        result.instance = next;
    }
    return result;
}

// ----- Stage 1.8b: the offers, the hand-in, the cutscenes, the cinematics, and the effects of a topic -----

QuestError Quests::available(const Entity& entity, const QuestObjective& objective) const {
    if (data() == nullptr || !entity.quests.loaded) {
        return QuestError::noData;
    }
    if (active_of(entity, objective.crc) != nullptr) {
        return QuestError::alreadyActive;
    }
    // The checks of the client at a person (0x37AE910) and its eligibility (0x3A4BFE0), in their order; the client hides an offer that fails
    // one of them (no locked offer: research quests-18d rules Q2). The active list of one character (MP-8).
    if (!objective.achievement.empty()) {
        // Another active objective with the same completion achievement (HasObjectiveWithCompletionAchievement 0x3A47310: the variants).
        const std::string achievement = lower_text(objective.achievement);
        for (const QuestInstance& quest : entity.quests.active) {
            const QuestObjective* other = m_data->objective_by_crc(quest.objectiveCrc);
            if (other != nullptr && !other->achievement.empty() && lower_text(other->achievement) == achievement) {
                return QuestError::alreadyActive;
            }
        }
        // The completion achievement (IsObjectiveComplete 0x3A48A30). A repeatable objective with a schedule of the data is on its cooldown
        // until the reset of its cycle takes the achievement away (stage 1.8d); a repeat with no schedule stays completed (decision D4 of
        // the user is open: the interval of such a repeat).
        if (conditional(entity, objective.achievement)) {
            const auto repeat = entity.quests.repeats.find(objective.crc);
            return repeat != entity.quests.repeats.end() && repeat->second.resetNs != 0 ? QuestError::onCooldown : QuestError::completedBefore;
        }
    }
    // Finding QR8D-R3: a repeat of the current cycle (its row has a deadline) is on its cooldown also with no completion achievement.
    if (objective.repeatable) {
        const auto repeat = entity.quests.repeats.find(objective.crc);
        if (repeat != entity.quests.repeats.end() && repeat->second.resetNs != 0) {
            return QuestError::onCooldown;
        }
    }
    // The gates whose service rule the backend does not have: RequiredGameModeId (row +0x34: the query 0x2CE2150 is IsInGameMode of the
    // GameModeParticipant component, 0x32BA3B0; the open world has no active game mode, so a row with a mode is false), the world tags (the
    // release status of the service, 0x3A48F40 and 0x3A8DB90), a categorical progression (no row has it). A decision of an agent: an unknown
    // rule is false, as an unknown achievement is (research U7). Stage QG2a (research quest-gaps-qg2b.md section 1): QuestArcTag (row +0x20)
    // is no gate: the eligibility 0x3A4BFE0 does not read it; it groups the QuestArc meta-achievements (0x38A6E20) only.
    if (!objective.gameMode.empty() || !objective.worldTags.empty() || !objective.requiredProgression.empty()) {
        return QuestError::unsupportedGate;
    }
    // RequiredLevel: the level index plus 1 (the level of the screen) is not below it (0x3A4C13E).
    if (objective.requiredLevel != 0 && entity.progression.level < objective.requiredLevel) {
        return QuestError::levelTooLow;
    }
    // RequiredAchievementId: the client offers only the variant whose conditional is true (the eight variants of 06_boarfeast name one
    // archetype achievement each; research quest-engine-3.md Q5). An empty cell has no rule.
    if (!objective.required.empty() && !conditional(entity, objective.required)) {
        return QuestError::notAvailable;
    }
    // RequiredFaction: bit 0 of the mask bypasses the test, else the bit of the faction of the character (0x3A4C166). The backend has no
    // faction of a character yet (roadmap 4.5): each character is unaligned (faction 0).
    if (!faction_allowed(objective.factionMask, kNoFaction)) {
        return QuestError::wrongFaction;
    }
    return QuestError::none;
}

QuestResult Quests::accept(Entity& entity, const QuestObjective& objective, std::uint16_t territory) {
    QuestResult result;
    result.error = available(entity, objective);
    if (!result.ok()) {
        return result;
    }
    result = grant(entity, objective.id, "offer", territory);
    if (result.ok()) {
        ++m_counts.accepts;
    }
    return result;
}

const QuestInstance* Quests::active_of(const Entity& entity, std::uint32_t objectiveCrc) const {
    // The active quests of one character (MP-8).
    for (const QuestInstance& quest : entity.quests.active) {
        if (quest.objectiveCrc == objectiveCrc) {
            return &quest;
        }
    }
    return nullptr;
}

bool Quests::ready(const QuestInstance& quest) const {
    const QuestObjective* objective = m_data != nullptr ? m_data->objective_by_crc(quest.objectiveCrc) : nullptr;
    return objective != nullptr && objective->root < quest.tasks.size() && done(quest.tasks[objective->root].state);
}

QuestError Quests::reward_join(const Entity& entity, const QuestObjective& objective, std::uint16_t originTerritory, RewardJoin& join,
                               std::string& detail) const {
    // Stage QG1: the standing goes to the territory of the character at the hand-in (World::territory_of). The grant of a game event of the
    // client takes the territory of its context, and with no context territory the detected territory of the player (0x31F6D2B to
    // 0x31F6D63, GetDetectedTerritoryId); the writer of the context of an objective completion is not known (static read of stage QG1,
    // Q1a), and the mission data has its own flag GrantRewardsToOriginTerritory (0x39C609E). Outside each territory: the origin territory.
    return reward_join_of(entity.characterId, nullptr, objective, standing_territory(m_world.territory_of(entity), originTerritory), join, detail);
}

std::uint16_t Quests::standing_territory(std::uint16_t handIn, std::uint16_t originTerritory) const {
    // A territory with no standing row (Great Cleave 3, Shattered Mountain 7, First Light 9 of the data) is not a territory of a standing:
    // then the origin territory of the instance [agent: the client grant would give nothing; a completed quest keeps its reward].
    const gamedata::ProgressionTables* tables = m_world.options().progressionTables;
    const bool hasRow = handIn != 0 && tables != nullptr && tables->loaded() && tables->standing(handIn) != nullptr;
    return hasRow ? handIn : originTerritory;
}

QuestError Quests::reward_join(const SkillSet& skills, const QuestObjective& objective, std::uint16_t originTerritory, RewardJoin& join,
                               std::string& detail) const {
    return reward_join_of(std::string{}, &skills, objective, originTerritory, join, detail);
}

QuestError Quests::reward_join_of(const std::string& characterId, const SkillSet* skills, const QuestObjective& objective,
                                  std::uint16_t originTerritory, RewardJoin& join, std::string& detail) const {
    join = RewardJoin{};
    // The rewards (research quest-engine.md Q6): the success event (UniversalExpAmount, CurrencyReward with CoinRewardChance, ItemReward with
    // ItemRewardQty and GearScoreRange, AchievementId, TerritoryStanding), the row (ExperienceReward, ItemRewardName with ItemRewardQty,
    // AchievementId, ObjectiveReward). A reward with no owner in the backend refuses the hand-in: a completed quest must not lose a reward (the
    // brief of stage 1.8b; stage 1.8d: the faction rewards (roadmap 4.5), the other currencies and tracks, an entitlement, a status effect, and
    // ProgressionReward stay refused).
    const auto refuse = [&](QuestError error, std::string text) {
        detail = std::move(text);
        return error;
    };
    const gamedata::QuestGameEvent* event = objective.successEvent.empty() ? nullptr : m_data->game_event(objective.successEvent);
    if (!objective.successEvent.empty() && event == nullptr) {
        return refuse(QuestError::cannotPay, "the quest data has no game event " + objective.successEvent);
    }
    if (event != nullptr && !event->unpaid.empty()) {
        return refuse(QuestError::cannotPay, "the reward " + event->unpaid.front() + " of " + event->id + " has no owner in the backend");
    }
    if (!objective.progressionReward.empty()) {
        return refuse(QuestError::cannotPay, "the reward ProgressionReward " + objective.progressionReward + " has no owner in the backend");
    }
    // Code review of stage 1.8b, finding QR8B-R9: a next objective (ObjectiveReward) that the quest data does not have refuses the hand-in.
    join.next = objective.objectiveReward.empty() ? nullptr : m_data->objective(objective.objectiveReward);
    if (!objective.objectiveReward.empty() && join.next == nullptr) {
        return refuse(QuestError::cannotPay, "the quest data has no next objective " + objective.objectiveReward + " (ObjectiveReward)");
    }
    const auto add_item = [&](const std::string& itemId, std::int64_t quantity,
                              std::optional<std::pair<std::uint16_t, std::uint16_t>> range) -> std::optional<QuestError> {
        if (itemId.empty() || quantity == 0) {
            return std::nullopt;
        }
        if (quantity < 0) {
            return refuse(QuestError::cannotPay, "a negative reward quantity of " + itemId + " (a removal: no rule)");
        }
        const gamedata::ItemCatalog* catalog = m_world.options().itemCatalog;
        const gamedata::ItemDefinition* definition = catalog != nullptr ? catalog->find(itemId) : nullptr;
        if (definition == nullptr) {
            return refuse(QuestError::cannotPay, "the item catalog has no reward item " + itemId);
        }
        // Stage 1.8d: a loot container (Resource+LootContainer: the Meat Pouch of 06_skinningknife) is a reward item of the bag; its open (1733)
        // rolls its loot table (ItemContainers). Stage QG2a: or pays its salvage event (the coin purses). A container that cannot open would
        // stay closed: refused, with the cause (ItemContainers::open_refusal).
        if (lower_text(definition->itemClass).find("lootcontainer") != std::string::npos && !m_world.item_containers().openable(definition->id)) {
            const std::string cause = m_world.item_containers().open_refusal(definition->id);
            return refuse(QuestError::cannotPay, "the reward item " + itemId + " is a loot container that cannot open: "
                                                     + (cause.empty() ? std::string("no open path") : cause));
        }
        join.items.push_back(RewardItem{definition->id, static_cast<std::uint32_t>(std::min<std::int64_t>(quantity, 0xFFFFFFFF)), range});
        return std::nullopt;
    };
    if (event != nullptr) {
        if (const std::optional<QuestError> error = add_item(event->item, event->itemQty, event->gearScoreRange)) {
            return *error;
        }
        join.eventExperience = event->experience;
        join.coin = event->coin;
        join.coinChance = event->coinChance;
        if (!event->achievement.empty()) {
            join.eventAchievements.push_back(event->achievement);
        }
        // Stage QG1: TerritoryStanding goes to the territory of the hand-in (the territory of the character, else the origin territory of the
        // instance: Quests::reward_join; it replaces decision D3 option B of stage 1.8d). With no territory it has no owner: refused. A rank-up
        // is paid (Skills::add_standing: the ranks, the tokens, the experience of the ranks).
        if (event->standing > 0) {
            if (originTerritory == 0) {
                return refuse(QuestError::cannotPay, "the standing " + std::to_string(event->standing) + " of " + event->id
                                                         + " has no territory (the character is outside each territory, and the instance has "
                                                           "no origin territory)");
            }
            Skills::Plan trial;
            if (skills != nullptr) {
                trial.skills = *skills; // Stage 1.8e: the standing of a history of the support manifest.
            } else {
                trial = m_world.skills().plan_none(characterId);
            }
            if (const ProgressError error = m_world.skills().add_standing(trial, originTerritory, event->standing); error != ProgressError::none) {
                return refuse(QuestError::cannotPay, "the standing " + std::to_string(event->standing) + " of the territory "
                                                         + std::to_string(originTerritory) + ": " + progress_error_text(error));
            }
            join.standing = event->standing;
            join.standingTerritory = originTerritory;
        }
        // Stage 1.8e: AzothReward goes into the Azoth balance (stage 3.7c). The cap of the balance is the cap of the crafting data (2,500); the
        // part above the cap is lost (AllowOverflow 0). With no cap in the crafting data the reward has no owner.
        if (event->azoth > 0) {
            const gamedata::CraftingData* crafting = m_world.options().craftingData;
            const gamedata::CraftCurrency* currency = crafting != nullptr && crafting->loaded() ? crafting->currency(kAzothCurrency) : nullptr;
            if (currency == nullptr || currency->cap == 0) {
                return refuse(QuestError::cannotPay, "the reward AzothReward " + std::to_string(event->azoth) + " of " + event->id
                                                         + " has no cap of the balance " + kAzothCurrency + " (the crafting data)");
            }
            const SkillSet* set = skills;
            const Entity* character = set == nullptr ? m_world.find_character(characterId) : nullptr;
            const std::uint64_t have = set != nullptr ? Skills::balance(*set, kAzothCurrency)
                                       : character != nullptr ? Skills::balance(character->skills, kAzothCurrency)
                                                              : 0;
            join.azoth = event->azoth;
            join.azothCap = currency->cap;
            join.azothGain = have >= currency->cap ? 0 : std::min(event->azoth, currency->cap - have);
        }
        // Stage QG5b: CategoricalProgressionReward goes to its progression when the skill system owns it: a trade skill of the progression
        // tables (AzothStaff of 10 main story events: 1 experience, and each rank of its table needs 1). A faction, an event, or a riding
        // track that the tables do not have as a trade skill stays refused (their owners are later stages).
        if (!event->progression.empty() && event->progressionAmount > 0) {
            const gamedata::ProgressionTables* tables = m_world.options().progressionTables;
            const gamedata::ProgressionRow* row = tables != nullptr && tables->loaded() ? tables->progression(event->progression) : nullptr;
            if (row == nullptr || row->kind != gamedata::ProgressionKind::tradeSkill) {
                return refuse(QuestError::cannotPay, "the reward CategoricalProgressionReward " + std::to_string(event->progressionAmount) + " of "
                                                         + event->id + " names the progression " + event->progression
                                                         + ", which is not a trade skill of the progression tables (no owner in the backend)");
            }
            join.progression = row->id;
            join.progressionAmount = event->progressionAmount;
        }
    }
    if (const std::optional<QuestError> error = add_item(objective.itemReward, objective.itemRewardQty, std::nullopt)) {
        return *error;
    }
    join.rowExperience = objective.experienceReward;
    join.achievement = objective.achievement;
    if (objective.repeatable) {
        join.schedule = m_data->schedule_kind(objective.schedule); // Finding QR8D-R3: also with no completion achievement.
    }
    return QuestError::none;
}

HandInResult Quests::hand_in(Entity& entity, std::uint64_t instance) {
    HandInResult result;
    result.instance = instance;
    const auto refuse = [&](QuestError error, std::string detail) {
        result.error = error;
        result.detail = std::move(detail);
        ++m_counts.handInRefusals;
        log::write("quests: the hand-in of the instance %016llx of character %s is refused: %s (%s); the quest stays as it is",
                   static_cast<unsigned long long>(instance), entity.characterId.c_str(), quest_error_text(error), result.detail.c_str());
        return result;
    };
    if (data() == nullptr || !entity.quests.loaded) {
        return refuse(QuestError::noData, "no quest data");
    }
    Plan plan(*this, entity, now_ns());
    const QuestInstance* quest = plan.peek(instance);
    const QuestObjective* objective = quest != nullptr ? plan.objective_of(*quest) : nullptr;
    if (objective == nullptr) {
        return refuse(QuestError::unknownInstance, "no active quest of this instance (a second hand-in)");
    }
    if (!ready(*quest)) {
        return refuse(QuestError::notReady, "the root task of " + objective->id + " is not complete");
    }
    // Code review of stage 1.8b, finding QR8B-R2 (research quest-engine.md Q3: the required items at the turn-in): each satisfied return task
    // (HaveAndReturnItems, GatherAndReturnItems) needs its target count in the bag now; the completion takes it. A shortage refuses the hand-in.
    // Code review of stage 1.8d, finding QR8D-R1: the return tasks of one item take their counts one after the other, so the bag needs the
    // sum of their targets (a reservation of each take against the rest of the stock), not the target of each task alone.
    struct ReturnNeed {
        std::string item;
        std::uint64_t count{0};
        std::string tasks;
    };
    std::map<std::string, ReturnNeed> returns;
    for (std::size_t index = 0; index < quest->tasks.size(); ++index) {
        const QuestTask* task = plan.task_of(*quest, static_cast<std::uint8_t>(index));
        if (task != nullptr && gamedata::is_return(task->type) && done(quest->tasks[index].state)) {
            ReturnNeed& need = returns[lower_text(task->itemName)];
            need.item = task->itemName;
            need.count += objective->nodes[index].target;
            need.tasks += (need.tasks.empty() ? "" : ", ") + task->id;
        }
    }
    for (const auto& [key, need] : returns) {
        const std::uint64_t have = plan.item_count(need.item);
        if (have < need.count) {
            return refuse(QuestError::missingReturnItems, "the return tasks " + need.tasks + " need " + std::to_string(need.count) + " "
                                                              + need.item + " together, and the bag has " + std::to_string(have));
        }
    }
    // The rewards: the one reward join of the data (Quests::reward_join), then the rolls of this hand-in (the coin chance, the gear score of
    // a range, the perks of the items) in its plan.
    RewardJoin join;
    std::string detail;
    if (const QuestError error = reward_join(entity, *objective, quest->originTerritory, join, detail); error != QuestError::none) {
        return refuse(error, detail);
    }
    const QuestObjective* next = join.next;
    std::vector<LootDrop> drops;
    for (const RewardItem& item : join.items) {
        // GearScoreRange of the event (client 0x31E43A0): the gear score of the item, one value or a range of the world generator (a
        // decision of an agent: the three rows of the data have one value).
        std::optional<std::uint32_t> gearScore;
        if (item.gearScoreRange) {
            const auto [low, high] = *item.gearScoreRange;
            const float unit = m_world.random_unit();
            gearScore = low + std::min<std::uint32_t>(static_cast<std::uint32_t>(unit * static_cast<float>(high - low + 1)), high - low);
        }
        drops.push_back(LootDrop{item.itemId, item.quantity, gearScore});
        result.items.push_back(item.itemId + " x" + std::to_string(item.quantity));
    }
    // The items with the rules of a new instance of stage 1.3b: the gear score of the item table (GearScoreOverride), its fixed perks and
    // its perk places with the level of the character (Rewards::grant_items: the roll of world/perk_roll.h, with ForceRarity of the layout).
    // The perks go into the instance (finding QR8B-R1).
    for (const GrantItem& item : m_world.rewards().grant_items(drops, entity.progression.level)) {
        plan.itemActions.push_back(QuestItemAction{true, item});
        plan.itemDelta[lower_text(item.itemId)] += item.quantity;
    }
    plan.experience += join.eventExperience;
    if (join.coin > 0 && (join.coinChance >= 1.0 || static_cast<double>(m_world.random_unit()) < join.coinChance)) {
        plan.coin += join.coin;
    }
    for (const std::string& achievement : join.eventAchievements) {
        plan.action(achievement, true);
        result.achievements.push_back(achievement);
    }
    if (join.standing > 0) {
        plan.standing.emplace_back(join.standingTerritory, join.standing);
        result.standing = join.standing;
        result.standingTerritory = join.standingTerritory;
    }
    if (join.azoth > 0) {
        plan.azoth += join.azoth;
        plan.azothCap = join.azothCap;
        result.azoth = join.azothGain;
    }
    if (!join.progression.empty()) {
        plan.progressions.emplace_back(join.progression, join.progressionAmount);
        result.progression = join.progression;
        result.progressionAmount = join.progressionAmount;
    }
    plan.experience += objective->experienceReward;
    result.experience = plan.experience;
    result.coin = plan.coin;
    // The completion: the instance goes (with the undo of its temporary unlocks and the return of its items) into the completed notice, and
    // the completion achievement of the row is set.
    end_instance(plan, instance, true, "handed in", true);
    if (!objective->achievement.empty()) {
        plan.action(objective->achievement, true);
        result.achievements.push_back(objective->achievement);
    }
    // Stage 1.8d: the repeat (research quests-18d rules Q4): a repeatable objective with a schedule of the data keeps its completion achievement
    // to the start of the next cycle of the schedule (the OnCooldown answer of the person, 0x37AE910); then the reset removes it. A repeat with
    // no schedule, or with a seasonal window, is not repeated (decision D4 of the user is open).
    // Code review of stage 1.8d, finding QR8D-R3: the repeat row and its deadline do not need a completion achievement (196 scheduled repeat
    // rows of the data have none): the row is the cooldown of the cycle, and the availability reads it.
    if (objective->repeatable) {
        const std::uint64_t reset = next_cycle(m_data->schedule_kind(objective->schedule), plan.nowNs);
        if (reset != 0) {
            state::QuestRepeatRow row;
            row.objective = objective->id;
            const auto before = entity.quests.repeats.find(objective->crc);
            row.completions = (before != entity.quests.repeats.end() ? before->second.completions : 0) + 1;
            row.completedNs = plan.nowNs;
            row.resetNs = reset;
            plan.repeatWrites.push_back(row);
            result.resetNs = reset;
        }
    }
    if (next != nullptr && active_of(entity, next->crc) == nullptr) {
        // ObjectiveReward: the next objective (research quest-engine.md Q6). No row of the beach chain has it.
        plan.add(*next, next_instance(), "reward", quest->originTerritory);
        result.next = next->id;
    }
    // Decision D5 option A (orchestrator, 2026-10-18): a full bag refuses the hand-in; nothing changes, and the quest stays ready.
    if (!plan.itemActions.empty()) {
        const Inventory::Plan trial = m_world.inventory().plan_quest_items(entity.characterId, plan.itemActions, nullptr);
        if (!trial.ok()) {
            return refuse(trial.error == ItemError::bagFull ? QuestError::bagFull : QuestError::cannotPay,
                          std::string("the reward items: ") + item_error_text(trial.error));
        }
    }
    const auto found = m_subscriptions.find(entity.id);
    plan.settle(found != m_subscriptions.end() ? found->second.get() : nullptr);
    const QuestError error = commit(entity, plan, {});
    if (error != QuestError::none) {
        return refuse(error, "the transaction of the hand-in");
    }
    ++m_counts.handIns;
    std::string items;
    for (const std::string& item : result.items) {
        items += (items.empty() ? "" : ", ") + item;
    }
    std::string progression;
    if (!result.progression.empty()) {
        result.progressionRank = entity.skills.rank(gamedata::progression_id(result.progression));
        progression = ", " + std::to_string(result.progressionAmount) + " " + result.progression + " experience (rank "
                      + std::to_string(result.progressionRank) + ")";
    }
    log::write("quests: character %s handed in %s (instance %016llx): %llu experience, %llu coin units, items [%s], standing %llu of the "
               "territory %u, Azoth %llu%s%s%s",
               entity.characterId.c_str(), objective->id.c_str(), static_cast<unsigned long long>(instance),
               static_cast<unsigned long long>(result.experience), static_cast<unsigned long long>(result.coin), items.c_str(),
               static_cast<unsigned long long>(result.standing), static_cast<unsigned>(result.standingTerritory),
               static_cast<unsigned long long>(result.azoth), progression.c_str(), result.next.empty() ? "" : ", next objective ",
               result.next.c_str());
    return result;
}

bool Quests::cutscene_requested(Entity& entity, std::uint32_t cutsceneCrc) {
    const auto found = m_subscriptions.find(entity.id);
    const std::vector<Subscriber>* list =
        found != m_subscriptions.end() && found->second ? found->second->find("cut:" + std::to_string(cutsceneCrc)) : nullptr;
    std::vector<std::pair<std::uint64_t, std::uint8_t>> tasks;
    for (std::size_t position = 0; list != nullptr && position < list->size(); ++position) {
        ++m_counts.subscribersVisited;
        const Subscriber entry = (*list)[position];
        const QuestInstance* quest = instance_of(entity, entry.instance);
        if (quest != nullptr && quest->tasks[entry.index].state == quest_state::kActive) {
            tasks.emplace_back(entry.instance, entry.index);
        }
    }
    // A second 6703 of the pending cutscene (code review of stage 1.8b, finding QR8B-R8): the grant again, with its tasks and its start.
    // A pending grant whose tasks all ended (a reset) is not kept: the new request gets a new grant for the tasks of now.
    const bool pendingLives = std::any_of(entity.quests.cutsceneTasks.begin(), entity.quests.cutsceneTasks.end(), [&](const auto& task) {
        return std::find(tasks.begin(), tasks.end(), task) != tasks.end();
    });
    if (cutsceneCrc != 0 && entity.quests.pendingCutscene == cutsceneCrc && pendingLives) {
        ++m_counts.cutsceneGrants;
        log::write("quests: character %s asks again for its pending cutscene %08x: the same grant (6707)%s", entity.characterId.c_str(),
                   static_cast<unsigned>(cutsceneCrc), entity.quests.cutsceneStarted ? ", it started" : "");
        return true;
    }
    if (tasks.empty()) {
        ++m_counts.cutsceneRefusals;
        log::write("quests: the client of character %s asks for the cutscene %08x, which no active task of the character waits for: no grant",
                   entity.characterId.c_str(), static_cast<unsigned>(cutsceneCrc));
        return false;
    }
    // The pending cutscene of the character (research quest-engine-3.md Q2: the server grants 6707 when a task waits for it; live check C1).
    entity.quests.pendingCutscene = cutsceneCrc;
    entity.quests.cutsceneStarted = false;
    entity.quests.cutsceneTasks = std::move(tasks);
    ++m_counts.cutsceneGrants;
    log::write("quests: character %s gets the cutscene %08x (grant 6707)", entity.characterId.c_str(), static_cast<unsigned>(cutsceneCrc));
    return true;
}

bool Quests::cutscene_started(Entity& entity, std::uint32_t cutsceneCrc) {
    if (entity.quests.pendingCutscene == 0 || entity.quests.pendingCutscene != cutsceneCrc) {
        return false;
    }
    entity.quests.cutsceneStarted = true;
    return true;
}

bool Quests::cutscene_ended(Entity& entity) {
    const std::uint32_t pending = entity.quests.pendingCutscene;
    const bool started = entity.quests.cutsceneStarted;
    if (pending == 0 || !started) {
        // The end of a conversation camera, or a 6705 after a grant that the client did not start (a mismatched grant can send 6705:
        // research conversations.md): no quest change.
        return false;
    }
    entity.quests.pendingCutscene = 0;
    entity.quests.cutsceneStarted = false;
    QuestEvent ended;
    ended.kind = QuestEventKind::cutscene;
    ended.screenCrc = pending;
    // Only the tasks of the grant (finding QR8B-R7): a task of a replacement instance (a reset) did not get the grant.
    ended.matched = std::move(entity.quests.cutsceneTasks);
    ended.restricted = true;
    entity.quests.cutsceneTasks.clear();
    const std::uint64_t before = m_counts.progressChanges;
    event(entity, ended);
    const bool changed = m_counts.progressChanges != before;
    m_counts.cutsceneCompletions += changed ? 1 : 0;
    log::write("quests: the cutscene %08x of character %s ended (6705)%s", static_cast<unsigned>(pending), entity.characterId.c_str(),
               changed ? ": its task is complete" : "");
    return changed;
}

bool Quests::cinematic(Entity& entity, std::uint32_t cinematicCrc, bool ended) {
    if (!ended) {
        entity.quests.pendingCinematic = cinematicCrc;
    } else if (entity.quests.pendingCinematic != cinematicCrc || cinematicCrc == 0) {
        log::write("quests: the client of character %s ended the cinematic %08x, which it did not start: no change", entity.characterId.c_str(),
                   static_cast<unsigned>(cinematicCrc));
        return false;
    } else {
        entity.quests.pendingCinematic = 0;
    }
    QuestEvent cinematic;
    cinematic.kind = ended ? QuestEventKind::cinematicEnd : QuestEventKind::cinematicStart;
    cinematic.screenCrc = cinematicCrc;
    const std::uint64_t before = m_counts.progressChanges;
    event(entity, cinematic);
    return m_counts.progressChanges != before;
}

void Quests::topic_finished(Entity& entity, const std::string& npc, const std::string& topic, const std::string& achievementUnlock) {
    (void)topics_finished(entity, npc, {QuestTopic{topic, achievementUnlock}});
}

QuestError Quests::topics_finished(Entity& entity, const std::string& npc, const std::vector<QuestTopic>& topics) {
    if (data() == nullptr || !entity.quests.loaded) {
        return QuestError::none;
    }
    const auto found = m_subscriptions.find(entity.id);
    const Subscriptions* subscriptions = found != m_subscriptions.end() ? found->second.get() : nullptr;
    Plan plan(*this, entity, now_ns());
    std::string names;
    for (const QuestTopic& topic : topics) {
        names += (names.empty() ? "" : ", ") + topic.topic;
        // AchievementUnlock of the topic (data ConversationTopicData; one time: a bit). Live check of stage 1.8b: the client sends 3158 at the
        // selection and 5397 at the close; the bit changes nothing in the open conversation, and the topic of 06_demonstorm is at a walkaway
        // person: the effects come at the finish.
        if (!topic.achievementUnlock.empty()) {
            plan.action(topic.achievementUnlock, true);
        }
        if (const std::vector<Subscriber>* list = subscriptions != nullptr ? subscriptions->find("ct:" + person_key(npc, topic.topic)) : nullptr) {
            const std::vector<Subscriber> copy = *list;
            for (const Subscriber& entry : copy) {
                ++m_counts.subscribersVisited;
                if (QuestInstance* quest = plan.edit(entry.instance)) {
                    plan.add_progress(*quest, entry.index, 1);
                }
            }
        }
    }
    if (plan.actions.empty() && plan.changed.empty()) {
        return QuestError::none;
    }
    plan.settle(subscriptions);
    const QuestError error = commit(entity, plan, {});
    if (error == QuestError::none) {
        m_counts.topicEffects += topics.size();
    }
    log::write("quests: character %s finished the topic(s) %s of %s: %s", entity.characterId.c_str(), names.c_str(), npc.c_str(),
               quest_error_text(error));
    return error;
}

void Quests::state_selected(Entity& entity, const std::string& npc, const std::string& state) {
    QuestEvent selected;
    selected.kind = QuestEventKind::state;
    selected.keys = {person_key(npc, state)};
    event(entity, selected);
}

} // namespace soulwarden::backend::world

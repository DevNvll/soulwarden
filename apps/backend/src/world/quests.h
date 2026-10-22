#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gamedata/poi_data.h"
#include "gamedata/quest_data.h"
#include "gamedata/trigger_data.h"
#include "state/store.h"
#include "world/entity.h"

// The quest system (stage 1.8a, docs/systems/quests.md; research quest-engine.md, quest-engine-3.md): the quests of each character, their
// task trees, the achievement actions of the tasks, the evaluators of the containers, of TaskAchievement, and of TaskTimer, the one-time
// grants (the start objective of a new character, a backstory template), and the pins.
//
// The state of a character is in its entity (Entity::quests, rule MP-1); only this system changes it. A change is one plan: the system
// copies the quests and the achievement bits of the character, runs the rules of the tasks on the copy to a fixed point (an action can
// complete a TaskAchievement of the same character), writes all rows in one transaction (the quests, the tasks, the pins, the
// achievements, the undo rows, the grant rows), and only then gives the entity the new values and marks the states (World::touch). A plan
// that the database does not take changes nothing.
//
// The cost depends on the character and its active quests, not on the size of the world (rule MP-8). Each character in the world has an
// index of its active tasks by the key of their event (Subscriptions): an event visits only the tasks of its key, and a plan copies only
// the quests that it changes (code review of stage 1.8a, finding QR8A-R7). A timer is one timer of the world clock.
//
// The rules of the tasks (client code, research quest-engine.md Q1; the sources of each rule are in docs/systems/quests.md):
// - ConsecutiveTaskContainer: the children in data order; SimpleTaskContainer: all children, complete when each child is complete;
//   PartialTaskContainer: all children, complete when one child is complete.
// - TaskUnlockAchievement, TaskLockAchievement, TaskTemporaryUnlockAchievement: the action at the activation, then complete.
// - TaskAchievement: complete when its conditional (achievement ids and non-persisted achievements with !, &&, ||, parentheses) is true.
// - TaskTimer: complete at its start plus Duration on the world clock. The start is a time of the synced wall clock (field 0 of the state).
//   A failure timer (FailOnCondition) is satisfied while it runs and fails the objective at its end.
// - Stage 1.8c: the counters (a credited kill, a game event, a gather, a craft, a refine, a screen, an interaction), the predicates (the
//   items in the bag, the equipment, the points of interest, the achievements: evaluated at the activation and at each change, with the
//   regression of a CompleteActive task), the item actions (TaskGiveItem, TaskGiveAndTakeItem, TaskRemoveItem), the quest drops of a kill
//   and of a gather, TaskObjective, TaskDie, the abandon, the reset, and the failure of an objective.
// - Stage 1.8b: TaskTriggerArea (the accepted position in a trigger volume of the data), TaskCutsceneComplete (the cutscene that the
//   server granted, 6703 to 6707, started 6706, ended 6705), TaskCinematicStart and TaskCinematicComplete (6606, 6542), TaskConversationTopic
//   and TaskConversationState (the person system), TaskTriggerGameEvent (the game event of the task at its activation: its whisper 5070,
//   its achievements). The offer (3772) grants an objective; the hand-in (794) completes it and pays its rewards in one transaction (the
//   plan, write, finish pattern of the build presets).
// - Each other type stays active: its supplier is a later part.
//
// World thread only. A route gives the world a task (World::call).

namespace soulwarden::backend::world {

class World;

/** The start territory of a new character: Windsward (decision D1 of the user; TerritoryDefinition row 06 names 06_getoffbeach). */
inline constexpr std::uint16_t kStartTerritory = 6;
/**
 * The start position of a new character on the Windsward beach (meters; research quests.md 5.4: the place of the cinematic of the quest,
 * the ground of the region height map). The world spawns a character kSpawnLift above a saved position. A rule of this project: the
 * game data has no player spawner of the main world.
 */
inline constexpr state::WorldPosition kBeachStart{9622.62F, 2306.48F, 40.24F};
/** The client keeps 8 pinned objectives at most (reader 0x3A68340, research quest-engine.md Q2). */
inline constexpr std::size_t kMaxTracked = 8;

enum class QuestError {
    none,
    /** The backend has no quest data. */
    noData,
    unknownCharacter,
    unknownObjective,
    /** The character has an active instance of the objective. */
    alreadyActive,
    /** The template is not a backstory template of the data (an archetype row is not a template). */
    unknownTemplate,
    /** The database did not take the change: nothing changed. */
    storeFailed,
    /** The rules of the tasks did not end (a loop of achievement actions): nothing changed. */
    noFixedPoint,
    /** Stage 1.8c: the character has no active quest with this instance id. */
    unknownInstance,
    /** Stage 1.8c: the objective has CanBeAbandoned 0 (a reset is the way back of such a quest; client 0x825EEF0). */
    notAbandonable,
    /** Stage 1.8c: the character is not in the world. */
    notInWorld,
    /** Stage 1.8b: the root of the objective is not complete (a hand-in before the end of the tasks). */
    notReady,
    /** Stage 1.8b: the objective has a reward that the backend cannot pay yet (standing, a loot container, a faction reward): no change. */
    cannotPay,
    /** Stage 1.8b: the bag has no place for the reward items (decision D5 option A of the orchestrator): no change, the quest stays ready. */
    bagFull,
    /** Stage 1.8b: the RequiredAchievementId of the objective is false for the character (an offer of a different variant). */
    notAvailable,
    /** Stage 1.8b: the character has the completion achievement of the objective (a second accept of a quest that it completed). */
    completedBefore,
};

[[nodiscard]] const char* quest_error_text(QuestError error);

struct QuestResult {
    QuestError error{QuestError::none};
    std::uint64_t instance{0};
    [[nodiscard]] bool ok() const {
        return error == QuestError::none;
    }
};

/** The result of the template command (decision D2 option C). */
struct TemplateResult {
    QuestError error{QuestError::none};
    /** True: the character is in the world (the entity has the values); false: the rows only (the next entry gives the objective). */
    bool inWorld{false};
    std::vector<std::string> achievements;
    std::vector<std::string> achievementsWithNoBit;
    std::vector<std::string> objectives;
    std::vector<std::string> objectivesWithNoRow;
    [[nodiscard]] bool ok() const {
        return error == QuestError::none;
    }
};

/** The counts of the quest system (the load cases QG2 and QE9, and `tools/world quests`). */
struct QuestCounts {
    std::uint64_t grants{0};
    std::uint64_t plans{0};
    std::uint64_t storeFailures{0};
    /** Task activations and completions of all plans. */
    std::uint64_t steps{0};
    /** Achievement change events, and the TaskAchievement tasks that they visited (each visit, before any filter: finding QR8A-R7). */
    std::uint64_t events{0};
    std::uint64_t waitsVisited{0};
    std::uint64_t timersStarted{0};
    std::uint64_t timersDone{0};
    /** Stage 1.8c: timers whose completion write the store refused (a retry follows on the world clock: finding QR8A-R5). */
    std::uint64_t timerRetries{0};
    std::uint64_t trackedUpdates{0};
    /** Stage 1.8c: the events of the other systems (kills, items, gathers, crafts, places, screens, interactions, game events, deaths). */
    std::uint64_t worldEvents{0};
    /** The subscribers (indexed tasks) that the events visited, before any filter; and the progress changes that they made. */
    std::uint64_t subscribersVisited{0};
    std::uint64_t progressChanges{0};
    /** Events with a result id that came a second time: no change. */
    std::uint64_t repeatedResults{0};
    /** Quest drops given (kills and gathers), and the drops that a roll did not give. */
    std::uint64_t drops{0};
    std::uint64_t dropMisses{0};
    std::uint64_t abandons{0};
    std::uint64_t resets{0};
    std::uint64_t failures{0};
    /** Characters whose items or places the tick looked at (one look for each character and tick at most). */
    std::uint64_t itemLooks{0};
    std::uint64_t placeLooks{0};
    /**
     * Code review of stage 1.8c, finding QR8C-R11 (the cost of an event): the lookups of a quest instance by its id (the index of the
     * character), and the quests that a walk over the list of a character visited (a structural change of the list, an index of all).
     */
    std::uint64_t instanceLookups{0};
    std::uint64_t questVisits{0};
    /** Finding QR8C-R1: events whose plan the store refused (a retry follows on the world clock), and the retries. */
    std::uint64_t eventRetries{0};
    /** Stage 1.8b: the offers that the character accepted, the hand-ins, and the hand-ins that the system refused. */
    std::uint64_t accepts{0};
    std::uint64_t handIns{0};
    std::uint64_t handInRefusals{0};
    /** Stage 1.8b: the cutscene requests (6703) that got the grant 6707, the refused ones, and the cutscene ends that completed a task. */
    std::uint64_t cutsceneGrants{0};
    std::uint64_t cutsceneRefusals{0};
    std::uint64_t cutsceneCompletions{0};
    /** Stage 1.8b: the whispers that went to a client (5070), the game events that a task applied, and the trigger volumes visited. */
    std::uint64_t whispers{0};
    std::uint64_t gameEventsApplied{0};
    std::uint64_t triggerVisits{0};
    /** Stage 1.8b: the finished topics with an effect (an AchievementUnlock or a task of the topic). */
    std::uint64_t topicEffects{0};
};

/** Stage 1.8b: the result of a hand-in (794): the rewards that the transaction paid, or the cause of the refusal. */
struct HandInResult {
    QuestError error{QuestError::none};
    /** The cause of a refusal for the log (the reward that cannot be paid, the item that does not fit). */
    std::string detail;
    std::uint64_t instance{0};
    std::uint64_t experience{0};
    /** The coin in the units of the wallet (150 is 1.50 coin). */
    std::uint64_t coin{0};
    /** The reward items ("ITEM xN") and the achievements of the completion. */
    std::vector<std::string> items;
    std::vector<std::string> achievements;
    /** The next objective of ObjectiveReward that the hand-in granted (empty: none). */
    std::string next;
    [[nodiscard]] bool ok() const {
        return error == QuestError::none;
    }
};

/** The quests of a character for a reader (a command). */
struct QuestView {
    bool inWorld{false};
    std::vector<state::QuestRow> quests;
    std::vector<std::uint64_t> tracked;
    std::vector<state::QuestGrantRow> grants;
    std::vector<state::QuestAwardRow> awards;
};

/** The kinds of the events of the other systems (stage 1.8c). */
enum class QuestEventKind : std::uint8_t {
    /** A credited kill: `keys` are the vitals categories of the victim (each in lower case). */
    kill,
    /** A game event applied to the character (the event of a kill, of a gatherable): `keys` has the event id. */
    gameEvent,
    /** A gather: `keys` are the item ids that the gather gave (TaskRequireGatherItems), `amounts` their quantities. */
    gather,
    /** A craft or a refine: `keys` has the recipe id; `amount` is the number of the crafted items, `gearScore` their gear score. */
    craft,
    refine,
    /** The client opened a screen (message 2617): `screenCrc`. */
    screen,
    /** An interaction with a tag started (`started` true) or completed: `keys` has the tag. */
    interact,
    /** Stage 1.8b: the granted cutscene of the character ended (6705 after its 6706): `screenCrc` is the CRC-32 of the cutscene name. */
    cutscene,
    /** Stage 1.8b: the client started a cinematic (6606) or ended the cinematic that it started (6542): `screenCrc` is its CRC-32. */
    cinematicStart,
    cinematicEnd,
    /** Stage 1.8b: the character finished a topic of a person (the person system): `keys` has "NPC|TOPIC" (lower case). */
    topic,
    /** Stage 1.8b: the person system selected a conversation state for the character: `keys` has "NPC|STATE" (lower case). */
    state,
};

/** One event of a different system for one character (stage 1.8c; research quest-progress.md 8: the values that a task needs). */
struct QuestEvent {
    QuestEventKind kind{QuestEventKind::kill};
    std::vector<std::string> keys;
    /** The amount of each key (empty: 1 for each key). */
    std::vector<std::uint32_t> amounts;
    std::uint32_t gearScore{0};
    std::uint32_t screenCrc{0};
    bool started{false};
    /** The item of the kill (KillEnemyWeaponID; empty: not known). */
    std::string weapon;
    /** The result id of the source (a kill: the creature and its death; 0: none): an event with a result id counts one time. */
    std::uint64_t result{0};
    /** The number of the retry of an event whose plan the store refused (code review of stage 1.8c, finding QR8C-R1). 0: the first. */
    std::uint32_t attempt{0};
    /**
     * A retry (code review of stage 2.5c, finding PR5C-R5): the tasks (instance and index) that the first delivery matched. A retry credits
     * only these tasks, when they are still active: a quest that a reset replaced, and a task that became active later, get nothing.
     */
    std::vector<std::pair<std::uint64_t, std::uint8_t>> matched;
};

/** One quest drop of a kill or of a gather (stage 1.8c): an item of a possession or gather task of the character. */
struct QuestDrop {
    std::string itemId;
    std::uint32_t quantity{0};
    std::uint32_t objectiveCrc{0};
    std::string objective;
    std::uint64_t instance{0};
    std::uint8_t index{0};
    /** AwardOnlyOnce: the drop counts in the award row of the task (it survives a reset). */
    bool counted{false};
};

class Quests {
public:
    Quests(World& world, state::Store& store, const gamedata::QuestData* data);
    ~Quests();
    Quests(const Quests&) = delete;
    Quests& operator=(const Quests&) = delete;

    /** @return The quest data, or null when the backend has none. */
    [[nodiscard]] const gamedata::QuestData* data() const {
        return m_data != nullptr && m_data->loaded() ? m_data : nullptr;
    }
    /** Stage 1.8c: the points of interest (game data poi-data.json), or null: no task of a place progresses. */
    void set_pois(const gamedata::PoiData* pois) {
        m_pois = pois;
    }
    [[nodiscard]] const gamedata::PoiData* pois() const;
    /** Stage 1.8b: the trigger volumes (game data trigger-data.json), or null: a task of a trigger area waits. */
    void set_triggers(const gamedata::TriggerData* triggers) {
        m_triggers = triggers;
    }
    [[nodiscard]] const gamedata::TriggerData* triggers() const;

    /**
     * The entity of a character comes into the world (after its achievement bits, before its first record): its saved quests, its pins,
     * its timers, and its pending one-time grants (the start objective of a new character, a template objective). No data: nothing.
     * Stage 1.8c: its task index, and the predicates of its active tasks (its items, its place, its bits can have changed while away).
     */
    void entered(Entity& entity);
    /** The character leaves the world: its index and its marks go (its rows stay; its timers end themselves). */
    void left(const Entity& entity);

    /**
     * Grants an objective to a character in the world: a new instance, its tree activated to a fixed point, one transaction.
     * @param source The source of the grant ("territory", "backstory", "command").
     */
    [[nodiscard]] QuestResult grant(Entity& entity, const std::string& objectiveId, const std::string& source, std::uint16_t territory);

    /**
     * The achievement bits of a character changed (the ownership system calls it after each change that this system did not make: a lore
     * page, a first craft, a build preset, the DLC). The active TaskAchievement tasks of the character (Active or CompleteActive: the
     * regression of finding QR8A-R3) get the new value.
     */
    void achievements_changed(Entity& entity);
    /**
     * Stage 2.5c (the persons): @return The value of an achievement conditional for a character now: the rule of TaskAchievement (ids with
     * !, &&, ||, parentheses; a non-persisted achievement is its own conditional; an id that the data does not know is false). An empty
     * text is false.
     */
    [[nodiscard]] bool conditional(const Entity& entity, const std::string& text) const;
    /**
     * Stage 2.5c: @return The state of the task `task` (TaskID, any case) of an active instance of `objective` (ObjectiveID, any case; empty:
     * any) of a character, or no value (no such instance or task, or no quest data). An empty `task`: the Active state when the character
     * has an active instance of the objective.
     */
    [[nodiscard]] std::optional<std::uint8_t> task_state(const Entity& entity, std::string_view objective, std::string_view task) const;
    /**
     * Stage 2.5c: @return True when the character has the completion achievement of an objective (AchievementId of its row; client
     * IsObjectiveComplete 0x3A48A30). An objective with no row or with no achievement is false.
     */
    [[nodiscard]] bool objective_completed(const Entity& entity, std::string_view objective) const;

    /**
     * The client sent its pinned list (message 1458). Only active instances of the character, 8 at most, no instance two times; the rows
     * get the list. The state does not go back to the client (research quests.md 6.2: the list of the client is the truth).
     * @return False when the list has an instance that the character does not have, or when the database did not take it.
     */
    bool client_tracked(Entity& entity, const std::vector<std::uint64_t>& instances);

    /**
     * Decision D2 option C (decision of the orchestrator, 2026-10-18): gives a character the achievements and the start objectives of one
     * backstory template of the game data, not its level, items, or faction. A character in the world gets the values at once; a
     * character that is not in the world gets the rows, and its next entry gives the objective. An objective that the character has is
     * not given again.
     */
    [[nodiscard]] TemplateResult apply_template(const std::string& characterId, const std::string& templateId);

    /** @return The quests of a character (the rows; for a character in the world, the values of its entity). */
    [[nodiscard]] std::optional<QuestView> view(const std::string& characterId);

    /**
     * The rows of a new character (stage 1.8a; the create route, any thread: it reads only the data): its pending start grant (the start
     * objective of the start territory), its archetype achievement (the AchievementUnlockOverride of its backstory row), and its start
     * position on the beach. No data, or a data file with no start objective: nothing, and the position stays (rule A5a).
     */
    [[nodiscard]] static state::NewCharacterRows new_character_rows(const gamedata::QuestData* data, const std::string& archetype,
                                                                    std::optional<state::WorldPosition>& position);

    /** Timer task of the world clock: a TaskTimer of a character is due. */
    void timer_due(const std::string& characterId, std::uint64_t instance, std::uint8_t index, std::uint64_t startNs);

    // Stage 1.8c: the events of the other systems. Each system stays the only writer of its own state: it calls the quest system after
    // its own successful change, and the quest system changes only the quests (and, in its own transaction, the items of an item task).

    /**
     * An event of a different system for one character (a kill of its credit list, a game event, a gather, a craft, a screen, an
     * interaction). Only the tasks of the index of its key are visited; a task that the event changes gets one plan.
     */
    void event(Entity& entity, const QuestEvent& event);
    /**
     * A creature died by the hits of the characters of its credit list (Rewards::killed): each of them gets the kill (its vitals
     * categories) and the kill event of its vitals row. A character that is not in the list gets nothing.
     */
    void killed(const Entity& creature, const std::vector<Entity*>& credited, const std::vector<std::string>& categories,
                const std::string& killEvent, std::uint64_t result);
    /**
     * The quest drops of a kill (TaskHaveItems, TaskHaveAndReturnItems: ItemDropVC names a vitals category of the victim) or of a gather
     * (TaskGatherItems, TaskGatherAndReturnItems: ItemDropVC names the gatherable id) for one character: the roll of each matching
     * active task with its ItemDropProbability, PauseAndClearLootOnTaskComplete, AwardOnlyOnce, and GuranteedDropAttempts. No change: the
     * caller gives the items in its own transaction, writes the award rows there (write_drops), and calls drops_done after the commit.
     */
    [[nodiscard]] std::vector<QuestDrop> plan_drops(Entity& entity, bool gather, const std::vector<std::string>& sources);
    /** Writes the award rows of the counted drops (in the batch of the caller). */
    [[nodiscard]] bool write_drops(const Entity& entity, const std::vector<QuestDrop>& drops);
    /** The drops are in the database: the award counts of the entity, the counters of the guaranteed drops, the log. */
    void drops_done(Entity& entity, const std::vector<QuestDrop>& drops);
    /**
     * @return True when an active task of the character has this InteractTag (a quest object that InteractOnlyOnQuest shows only during
     *         the task: client 0x8261FC0). An empty tag: false.
     */
    [[nodiscard]] bool quest_object_open(const Entity& entity, const std::string& interactTag) const;
    /** The items (bag or paperdoll) of a character changed (World::touch): its possession and equip tasks look again at the next tick. */
    void items_touched(const Entity& entity);
    /** The accepted position of a character changed (World::place): its tasks of a place look again at the next tick. */
    void moved(const Entity& entity);
    /** The client reported a point of interest (2297 entered, 6416 left): a cue; the server looks at the shapes itself. */
    void place_reported(Entity& entity, std::uint16_t territory, bool entered);
    /** The character died (the health system): TaskDie with FailOnCondition fails its objective, with ResetOnCondition resets it. */
    void died(Entity& entity);
    /** The world tick: the characters whose items or place changed (one look each). */
    void tick();

    /**
     * The client abandons an instance (message 3836): only an objective with CanBeAbandoned. The instance goes, with the undo of its
     * achievement actions, the return of its items, and its pin; the award rows of the objective go (a new grant awards again: client
     * 0x825EEF0).
     */
    [[nodiscard]] QuestResult abandon(Entity& entity, std::uint64_t instance);
    /**
     * The client resets an instance (message 5600): the instance goes as at an abandon, but the award rows stay, and a new instance of the
     * objective starts (state 0 and state 4 on the wire do not reset a task of the client: live finding of stage 1.8a).
     * @return The new instance in `instance`.
     */
    [[nodiscard]] QuestResult reset(Entity& entity, std::uint64_t instance);

    // Stage 1.8b: the offers, the hand-in, the cutscenes, the cinematics, and the effects of a topic.

    /**
     * @return `none` when the character can accept the objective (an offer of a person, 3772): its RequiredAchievementId is true (an empty
     *         cell: true), the character has no active instance of it, and it does not have its completion achievement. Else the cause.
     */
    [[nodiscard]] QuestError available(const Entity& entity, const gamedata::QuestObjective& objective) const;
    /** The character accepts an offer (the person system checked the source): a new instance with the source "offer". */
    [[nodiscard]] QuestResult accept(Entity& entity, const gamedata::QuestObjective& objective, std::uint16_t territory);
    /** @return The active instance of an objective CRC of a character, or null. */
    [[nodiscard]] const QuestInstance* active_of(const Entity& entity, std::uint32_t objectiveCrc) const;
    /** @return True when the root of an instance is satisfied (Complete or CompleteActive): the quest waits for its hand-in. */
    [[nodiscard]] bool ready(const QuestInstance& quest) const;
    /**
     * The hand-in of a ready quest (794; the person system checked the source and the destination): one transaction removes the instance
     * (with the undo of its temporary unlocks, the return of its items, the pin), writes the completion achievements, the experience, the
     * coin, and the reward items; then the entity, the bits, the items, the level, and the completed notice of the Objectives state. A
     * reward that the backend cannot pay, a full bag (decision D5 option A), or a refused write changes nothing: the quest stays ready.
     */
    [[nodiscard]] HandInResult hand_in(Entity& entity, std::uint64_t instance);
    /**
     * A cutscene request of the client (6703): @return True (the grant 6707) when an Active TaskCutsceneComplete of the character waits for
     * this cutscene; it is the pending cutscene of the character then. Else false: no answer, nothing changes.
     */
    bool cutscene_requested(Entity& entity, std::uint32_t cutsceneCrc);
    /** The client started a cutscene (6706). @return True when it is the pending cutscene of the character. */
    bool cutscene_started(Entity& entity, std::uint32_t cutsceneCrc);
    /**
     * The client ended its cutscene (6705, no id). @return True when the pending cutscene of the character started (6706) and its tasks got
     * the completion. A 6705 with no started pending cutscene (the end of a conversation) changes no quest.
     */
    bool cutscene_ended(Entity& entity);
    /** A cinematic of the client: 6606 (`ended` false) or 6542 (the end of the cinematic that the client started). @return True: a change. */
    bool cinematic(Entity& entity, std::uint32_t cinematicCrc, bool ended);
    /**
     * The character finished a topic of a person (the person system checked it): one plan unlocks the AchievementUnlock of the topic and
     * gives the TaskConversationTopic tasks of the person and the topic their progress.
     */
    void topic_finished(Entity& entity, const std::string& npc, const std::string& topic, const std::string& achievementUnlock);
    /** The person system selected a conversation state for the character: the TaskConversationState tasks of the person and the state. */
    void state_selected(Entity& entity, const std::string& npc, const std::string& state);

    /** @return True when the character has an active task of a place or a location rule (a test of the index). */
    [[nodiscard]] bool has_place_tasks(const Entity& entity) const;
    /** @return The number of indexed subscribers of a character (a test of the index; 0 for a character with no index). */
    [[nodiscard]] std::size_t subscriber_count(const Entity& entity) const;

    [[nodiscard]] const QuestCounts& counts() const {
        return m_counts;
    }

private:
    struct Plan;
    struct Subscriptions;
    /** Commits a plan (one transaction), then gives the entity the values. */
    [[nodiscard]] QuestError commit(Entity& entity, Plan& plan, const std::vector<state::QuestGrantRow>& grants);
    /** Runs the pending grants of a character in the world. */
    void run_pending(Entity& entity);
    [[nodiscard]] std::uint64_t next_instance();
    void schedule(const std::string& characterId, std::uint64_t instance, std::uint8_t index, std::uint64_t startNs, double seconds);
    [[nodiscard]] std::uint64_t now_ns() const;
    /** Builds the index of a character again for the instances of a plan (all instances: `all`). */
    void index(const Entity& entity, const std::set<std::uint64_t>& instances, bool all);
    /** The predicates of the indexed tasks of some kinds (items, places, achievements) of a character: one plan when one changed. */
    void refresh(Entity& entity, bool items, bool places, bool achievements, const char* cause);
    /**
     * Ends an instance (abandon, reset, failure) in a plan: the undo, the returns, the pin. `completed` (the hand-in, stage 1.8b): only the
     * undo of a temporary unlock (client 0x8260590; an unlock or a lock reverts only at a reset or an abandon: 0x8260730, 0x8260880), and
     * the instance goes into the completed notice.
     */
    void end_instance(Plan& plan, std::uint64_t instance, bool keepAwards, const char* cause, bool completed = false);
    /** @return True for a result id that an earlier event handled: the event changes nothing. */
    [[nodiscard]] bool repeated(std::uint64_t result) const;
    /** Keeps a result id as handled (code review of stage 1.8c, finding QR8C-R1: only after the commit of its plan). */
    void remember(std::uint64_t result);
    /** @return The active quest of an instance id of a character (the index of the character; finding QR8C-R11), or null. */
    [[nodiscard]] const QuestInstance* instance_of(const Entity& entity, std::uint64_t instance);

    World& m_world;
    state::Store& m_store;
    const gamedata::QuestData* m_data;
    const gamedata::PoiData* m_pois{nullptr};
    const gamedata::TriggerData* m_triggers{nullptr};
    std::uint64_t m_nextInstance{0};
    QuestCounts m_counts;
    /** The task index of each character in the world (a cache of its active tasks; MP-1: the state stays in the entity). */
    std::unordered_map<EntityId, std::unique_ptr<Subscriptions>> m_subscriptions;
    std::set<EntityId> m_itemsDirty;
    std::set<EntityId> m_placesDirty;
    /** The misses of each task with GuranteedDropAttempts since its last drop (key: the character, the instance, the index). */
    std::map<std::tuple<std::string, std::uint64_t, std::uint8_t>, std::uint32_t> m_misses;
    /** The result ids of the last events (a ring of kResultWindow ids): a repeated delivery counts one time, with a bound store. */
    std::unordered_set<std::uint64_t> m_results;
    std::deque<std::uint64_t> m_resultOrder;
};

} // namespace soulwarden::backend::world

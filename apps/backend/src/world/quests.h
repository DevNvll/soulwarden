#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "gamedata/quest_data.h"
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
// The cost depends on the character and its active quests, not on the size of the world (rule MP-8): an achievement change visits the
// active TaskAchievement tasks of that character only; a timer is one timer of the world clock.
//
// The rules of the tasks (client code, research quest-engine.md Q1; the sources of each rule are in docs/systems/quests.md):
// - ConsecutiveTaskContainer: the children in data order; SimpleTaskContainer: all children, complete when each child is complete;
//   PartialTaskContainer: all children, complete when one child is complete.
// - TaskUnlockAchievement, TaskLockAchievement, TaskTemporaryUnlockAchievement: the action at the activation, then complete.
// - TaskAchievement: complete when its conditional (achievement ids and non-persisted achievements with !, &&, ||, parentheses) is true.
// - TaskTimer: complete at its start plus Duration on the world clock. The start is a time of the synced wall clock (field 0 of the state).
// - Each other type stays active: its supplier is a later part (1.8b to 1.8d).
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

/** The counts of the quest system (the load case QG2 and `tools/world quests`). */
struct QuestCounts {
    std::uint64_t grants{0};
    std::uint64_t plans{0};
    std::uint64_t storeFailures{0};
    /** Task activations and completions of all plans. */
    std::uint64_t steps{0};
    /** Achievement change events, and the TaskAchievement tasks that they visited. */
    std::uint64_t events{0};
    std::uint64_t waitsVisited{0};
    std::uint64_t timersStarted{0};
    std::uint64_t timersDone{0};
    std::uint64_t trackedUpdates{0};
};

/** The quests of a character for a reader (a command). */
struct QuestView {
    bool inWorld{false};
    std::vector<state::QuestRow> quests;
    std::vector<std::uint64_t> tracked;
    std::vector<state::QuestGrantRow> grants;
};

class Quests {
public:
    Quests(World& world, state::Store& store, const gamedata::QuestData* data);

    /** @return The quest data, or null when the backend has none. */
    [[nodiscard]] const gamedata::QuestData* data() const {
        return m_data != nullptr && m_data->loaded() ? m_data : nullptr;
    }

    /**
     * The entity of a character comes into the world (after its achievement bits, before its first record): its saved quests, its pins,
     * its timers, and its pending one-time grants (the start objective of a new character, a template objective). No data: nothing.
     */
    void entered(Entity& entity);

    /**
     * Grants an objective to a character in the world: a new instance, its tree activated to a fixed point, one transaction.
     * @param source The source of the grant ("territory", "backstory", "command").
     */
    [[nodiscard]] QuestResult grant(Entity& entity, const std::string& objectiveId, const std::string& source, std::uint16_t territory);

    /**
     * The achievement bits of a character changed (the ownership system calls it after each change that this system did not make: a lore
     * page, a first craft, a build preset, the DLC). The active TaskAchievement tasks of the character that the bits complete complete.
     */
    void achievements_changed(Entity& entity);

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

    [[nodiscard]] const QuestCounts& counts() const {
        return m_counts;
    }

private:
    struct Plan;
    /** Commits a plan (one transaction), then gives the entity the values. */
    [[nodiscard]] QuestError commit(Entity& entity, Plan& plan, const std::vector<state::QuestGrantRow>& grants);
    /** Runs the pending grants of a character in the world. */
    void run_pending(Entity& entity);
    [[nodiscard]] std::uint64_t next_instance();
    void schedule(const std::string& characterId, std::uint64_t instance, std::uint8_t index, std::uint64_t startNs, double seconds);
    [[nodiscard]] std::uint64_t now_ns() const;

    World& m_world;
    state::Store& m_store;
    const gamedata::QuestData* m_data;
    std::uint64_t m_nextInstance{0};
    QuestCounts m_counts;
};

} // namespace soulwarden::backend::world

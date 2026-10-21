#include "world/quests.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <set>

#include "core/logging/log.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

using gamedata::QuestObjective;
using gamedata::QuestTask;
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

bool done(std::uint8_t state) {
    return state == quest_state::kComplete || state == quest_state::kCompleteActive;
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
    }
    return "?";
}

/**
 * One change of the quests of one character: a copy of its quests and of its achievement bits, the rules of the tasks on the copy, and the
 * rows that the change writes. Nothing changes in the world before Quests::commit.
 */
struct Quests::Plan {
    Plan(const gamedata::QuestData& data, const Ownership& ownership, QuestCounts& counts, const Entity& entity, std::uint64_t now)
        : data(data), ownership(ownership), counts(counts), log(entity.quests), bits(entity.achievements.begin(), entity.achievements.end()),
          nowNs(now) {}

    const gamedata::QuestData& data;
    const Ownership& ownership;
    QuestCounts& counts;
    QuestLog log;
    std::set<std::uint32_t> bits;
    std::uint64_t nowNs;
    /** The achievement actions in their order: the id and true for an unlock, false for a lock. */
    std::vector<std::pair<std::string, bool>> actions;
    std::set<std::uint64_t> changed;
    bool trackedChanged{false};
    std::vector<state::QuestUndoRow> undo;
    struct TimerStart {
        std::uint64_t instance;
        std::uint8_t index;
        std::uint64_t startNs;
        double seconds;
    };
    std::vector<TimerStart> timers;
    std::uint64_t steps{0};
    bool overflow{false};

    QuestInstance* find(std::uint64_t instance) {
        for (QuestInstance& quest : log.active) {
            if (quest.instance == instance) {
                return &quest;
            }
        }
        return nullptr;
    }

    const QuestObjective* objective_of(const QuestInstance& quest) const {
        return data.objective_by_crc(quest.objectiveCrc);
    }

    void action(const std::string& id, bool unlock) {
        actions.emplace_back(id, unlock);
        if (const std::optional<std::uint32_t> bit = ownership.achievement_bit(id)) {
            if (unlock) {
                bits.insert(*bit);
            } else {
                bits.erase(*bit);
            }
        }
    }

    // The conditional of TaskAchievement and of a non-persisted achievement (client 0x31898D0, 0x3A8C640): ids with !, &&, ||, and
    // parentheses. An id is true when the character has its bit; a non-persisted achievement is its own conditional; an id that the data
    // does not know is false (research U7: it must not become a stored bit).
    struct Reader {
        const std::string& text;
        std::size_t position{0};
        Plan& plan;
        int depth;

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
            bool value = all();
            while (take("||")) {
                const bool right = all();
                value = value || right;
            }
            return value;
        }
        bool all() {
            bool value = unary();
            while (take("&&")) {
                const bool right = unary();
                value = value && right;
            }
            return value;
        }
        bool unary() {
            if (take("!")) {
                return !unary();
            }
            if (take("(")) {
                const bool value = any();
                (void)take(")");
                return value;
            }
            blank();
            const std::size_t start = position;
            while (position < text.size() && text[position] != ' ' && text[position] != '\t' && text[position] != '&'
                   && text[position] != '|' && text[position] != '(' && text[position] != ')' && text[position] != '!') {
                ++position;
            }
            return plan.id_value(text.substr(start, position - start), depth);
        }
    };

    bool id_value(const std::string& id, int depth) {
        if (id.empty()) {
            return false;
        }
        if (const std::optional<std::uint32_t> bit = ownership.achievement_bit(id)) {
            return bits.count(*bit) != 0;
        }
        if (const gamedata::QuestNpa* npa = data.npa(id)) {
            if (depth >= kNpaDepth || !npa->status.empty() || npa->conditional.empty()) {
                return false; // No rule for a status requirement in stage 1.8a.
            }
            return evaluate(npa->conditional, depth + 1);
        }
        return false;
    }

    bool evaluate(const std::string& conditional, int depth = 0) {
        if (conditional.empty()) {
            return false;
        }
        Reader reader{conditional, 0, *this, depth};
        return reader.any();
    }

    bool step() {
        ++steps;
        ++counts.steps;
        if (steps > kStepLimit) {
            overflow = true;
        }
        return !overflow;
    }

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
        if (done(state.state) || state.state == quest_state::kActive) {
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
            if (evaluate(task.itemName)) {
                complete(quest, index);
            }
            return;
        case task_type::kTimer: {
            state.startNs = nowNs;
            timers.push_back(TimerStart{quest.instance, index, nowNs, task.duration.value_or(0.0)});
            ++counts.timersStarted;
            return;
        }
        default:
            return; // The supplier of this type is a later part (1.8b to 1.8d): the task waits.
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

    /** Ends the active tasks under a node (the other children of a partial container that completed). */
    void deactivate(QuestInstance& quest, std::uint8_t index) {
        const QuestObjective* objective = objective_of(quest);
        if (objective == nullptr || !step()) {
            return;
        }
        QuestTaskState& state = quest.tasks[index];
        if (state.state != quest_state::kActive) {
            return;
        }
        state.state = quest_state::kInactive;
        state.startNs = 0;
        for (const std::uint8_t child : objective->nodes[index].children) {
            deactivate(quest, child);
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
        state.startNs = 0;
        changed.insert(quest.instance);
        if (!node.parent) {
            log::write("quests: the root of the objective %s (instance %016llx) is complete: the hand-in is stage 1.8b", quest.objective.c_str(),
                       static_cast<unsigned long long>(quest.instance));
            return;
        }
        const std::uint8_t parentIndex = *node.parent;
        if (quest.tasks[parentIndex].state != quest_state::kActive) {
            return;
        }
        const gamedata::QuestNode& parent = objective->nodes[parentIndex];
        // The count of the container (client 0x3AD6C60, 0x3AD6D20: the satisfied direct children while it is active).
        quest.tasks[parentIndex].progress = satisfied(quest, *objective, parentIndex);
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

    /** Completes each active TaskAchievement whose conditional is true, to a fixed point (an action can complete a later one). */
    void settle() {
        for (int round = 0; round < kSettleRounds && !overflow; ++round) {
            bool progress = false;
            for (std::size_t position = 0; position < log.active.size(); ++position) {
                QuestInstance& quest = log.active[position];
                const QuestObjective* objective = objective_of(quest);
                if (objective == nullptr) {
                    continue;
                }
                for (std::size_t index = 0; index < quest.tasks.size(); ++index) {
                    const gamedata::QuestNode& node = objective->nodes[index];
                    if (quest.tasks[index].state != quest_state::kActive || node.task == gamedata::kNoTask
                        || data.task(node.task).type != task_type::kAchievement) {
                        continue;
                    }
                    ++counts.waitsVisited;
                    if (evaluate(data.task(node.task).itemName)) {
                        complete(quest, static_cast<std::uint8_t>(index));
                        progress = true;
                    }
                }
            }
            if (!progress) {
                return;
            }
        }
        overflow = true;
    }

    /** Adds a new instance of an objective and activates its root. */
    QuestInstance& add(const QuestObjective& objective, std::uint64_t instance, const std::string& source, std::uint16_t territory) {
        QuestInstance quest;
        quest.instance = instance;
        quest.objective = objective.id;
        quest.objectiveCrc = objective.crc;
        quest.acceptNs = nowNs;
        quest.originTerritory = territory;
        quest.source = source;
        quest.tasks.resize(objective.nodes.size());
        log.active.push_back(std::move(quest));
        changed.insert(instance);
        QuestInstance& added = log.active.back();
        activate(added, objective.root);
        return *find(instance);
    }
};

Quests::Quests(World& world, state::Store& store, const gamedata::QuestData* data) : m_world(world), m_store(store), m_data(data) {}

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
    state::Store::Batch batch(m_store);
    bool ok = batch.open();
    for (const std::uint64_t instance : plan.changed) {
        const QuestInstance* quest = plan.find(instance);
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
    if (ok && plan.trackedChanged) {
        ok = m_store.write_quest_pins(entity.characterId, plan.log.tracked);
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
    for (const state::QuestGrantRow& grant : grants) {
        ok = ok && m_store.write_quest_grant(entity.characterId, grant);
    }
    ok = ok && batch.commit();
    if (!ok) {
        ++m_counts.storeFailures;
        log::write("quests: the database did not take the quest change of character %s: no change", entity.characterId.c_str());
        return QuestError::storeFailed;
    }
    // The rows are in the database: now the entity, the bits, the marks, and the timers.
    plan.log.loaded = true;
    entity.quests = std::move(plan.log);
    m_world.ownership().quest_achievements(entity, unlocks, locks);
    m_world.touch(entity.id, ComponentKey::objectives);
    for (const Plan::TimerStart& timer : plan.timers) {
        schedule(entity.characterId, timer.instance, timer.index, timer.startNs, timer.seconds);
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
    Plan plan(*m_data, m_world.ownership(), m_counts, entity, now_ns());
    const std::uint64_t instance = next_instance();
    plan.add(*objective, instance, source, territory);
    // The automatic pin (a server rule: the client has no automatic pin for these types, research quests.md 3.4): the flag of the type of
    // the objective in the last 2709 of the client; with no flag, the default options of the client.
    const std::optional<bool> flag = objective->typeIndex ? m_world.auto_pin(entity, *objective->typeIndex) : std::nullopt;
    const bool pin = flag.value_or(!objective->typeIndex || *objective->typeIndex != kCommunityGoalType);
    if (pin && plan.log.tracked.size() < kMaxTracked) {
        plan.log.tracked.push_back(instance);
        plan.trackedChanged = true;
    }
    plan.settle();
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
    log.loaded = true;
    log.data = m_data;
    entity.quests = std::move(log);
    if (skipped != 0) {
        log::write("quests: character %s has %zu saved quest row(s) that the quest data does not have: not loaded (the rows stay)",
                   entity.characterId.c_str(), skipped);
    }
    // The timers that run: their end on the world clock from the saved start (the time while the character was away counts: the client
    // shows start + Duration against the synced wall clock; decision D4 for the user).
    for (const QuestInstance& quest : entity.quests.active) {
        const QuestObjective* objective = m_data->objective_by_crc(quest.objectiveCrc);
        for (std::size_t index = 0; objective != nullptr && index < quest.tasks.size(); ++index) {
            const gamedata::QuestNode& node = objective->nodes[index];
            if (node.task != gamedata::kNoTask && quest.tasks[index].state == quest_state::kActive && quest.tasks[index].startNs != 0
                && m_data->task(node.task).type == task_type::kTimer) {
                schedule(entity.characterId, quest.instance, static_cast<std::uint8_t>(index), quest.tasks[index].startNs,
                         m_data->task(node.task).duration.value_or(0.0));
            }
        }
    }
    // The seasons readiness state: the HUD pin needs it (live check L1 of stage 1.8a).
    m_world.touch(entity.id, ComponentKey::seasons);
    if (!entity.quests.active.empty()) {
        m_world.touch(entity.id, ComponentKey::objectives);
    }
    run_pending(entity);
    // The bits can have changed while the character was away (a template, the DLC): the waits of the character.
    achievements_changed(entity);
    log::write("quests: character %s has %zu active quest(s), %zu pinned", entity.characterId.c_str(), entity.quests.active.size(),
               entity.quests.tracked.size());
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
        Plan plan(*m_data, m_world.ownership(), m_counts, entity, now_ns());
        const std::uint64_t instance = next_instance();
        const std::uint16_t territory = grant.source == "territory" ? static_cast<std::uint16_t>(std::strtoul(grant.key.c_str(), nullptr, 10)) : 0;
        plan.add(*objective, instance, grant.source, territory);
        const std::optional<bool> flag = objective->typeIndex ? m_world.auto_pin(entity, *objective->typeIndex) : std::nullopt;
        if (flag.value_or(!objective->typeIndex || *objective->typeIndex != kCommunityGoalType) && plan.log.tracked.size() < kMaxTracked) {
            plan.log.tracked.push_back(instance);
            plan.trackedChanged = true;
        }
        plan.settle();
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

void Quests::achievements_changed(Entity& entity) {
    if (data() == nullptr || !entity.quests.loaded || entity.quests.active.empty()) {
        return;
    }
    ++m_counts.events;
    Plan plan(*m_data, m_world.ownership(), m_counts, entity, now_ns());
    plan.settle();
    if (plan.changed.empty() && plan.actions.empty()) {
        return;
    }
    const QuestError error = commit(entity, plan, {});
    if (error != QuestError::none) {
        log::write("quests: the achievement change of character %s did not advance its quests: %s", entity.characterId.c_str(),
                   quest_error_text(error));
    }
}

void Quests::timer_due(const std::string& characterId, std::uint64_t instance, std::uint8_t index, std::uint64_t startNs) {
    Entity* entity = m_world.find_character(characterId);
    if (entity == nullptr || data() == nullptr) {
        return; // The character left: its next entry starts the timer again from the saved start.
    }
    Plan plan(*m_data, m_world.ownership(), m_counts, *entity, now_ns());
    QuestInstance* quest = plan.find(instance);
    if (quest == nullptr || index >= quest->tasks.size() || quest->tasks[index].state != quest_state::kActive
        || quest->tasks[index].startNs != startNs) {
        return; // An old timer: the task ended, or a later activation has its own timer.
    }
    const QuestObjective* objective = plan.objective_of(*quest);
    const QuestTask& task = m_data->task(objective->nodes[index].task);
    ++m_counts.timersDone;
    if (task.failOnCondition) {
        // FailOnCondition: the end of the timer fails the objective (the failure of an objective is stage 1.8d): the task fails.
        quest->tasks[index].state = quest_state::kFailed;
        quest->tasks[index].startNs = 0;
        plan.changed.insert(instance);
    } else {
        plan.complete(*quest, index);
    }
    plan.settle();
    const QuestError error = commit(*entity, plan, {});
    log::write("quests: the timer %s of character %s (instance %016llx, task %u) ended after %.3f s: %s", task.id.c_str(), characterId.c_str(),
               static_cast<unsigned long long>(instance), static_cast<unsigned>(index),
               static_cast<double>(now_ns() - startNs) / 1e9, error == QuestError::none ? (task.failOnCondition ? "failed" : "complete") : quest_error_text(error));
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
    Plan plan(*m_data, m_world.ownership(), m_counts, *entity, now_ns());
    for (const std::string& id : rows) {
        plan.action(id, true);
    }
    std::vector<state::QuestGrantRow> grants;
    for (const QuestObjective* objective : objectives) {
        const auto active = std::find_if(plan.log.active.begin(), plan.log.active.end(),
                                         [&](const QuestInstance& quest) { return quest.objectiveCrc == objective->crc; });
        std::uint64_t instance = active != plan.log.active.end() ? active->instance : 0;
        if (instance == 0) {
            instance = next_instance();
            plan.add(*objective, instance, "backstory", 0);
            if (plan.log.tracked.size() < kMaxTracked) {
                plan.log.tracked.push_back(instance);
                plan.trackedChanged = true;
            }
        }
        grants.push_back(state::QuestGrantRow{"backstory", backstory->id + "/" + objective->id, objective->id, "granted", instance});
    }
    plan.settle();
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

} // namespace soulwarden::backend::world

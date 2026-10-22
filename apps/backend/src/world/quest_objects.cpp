#include "world/quest_objects.h"

#include <algorithm>
#include <cmath>

#include "core/logging/log.h"
#include "state/store.h"
#include "world/inventory.h"
#include "world/quests.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

constexpr std::size_t verdict_index(QuestObjectVerdict verdict) {
    return static_cast<std::size_t>(verdict);
}

/** @return The strikes of the work of a row: BaseGatherTime rounded up, at least 1 (a rule of this project). */
std::uint32_t work_of(const gamedata::QuestObjectRow& row) {
    return std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::ceil(row.time - 0.0001F)));
}

} // namespace

const char* quest_object_verdict_text(QuestObjectVerdict verdict) {
    switch (verdict) {
    case QuestObjectVerdict::hit:
        return "hit";
    case QuestObjectVerdict::destroyed:
        return "destroyed";
    case QuestObjectVerdict::tooFar:
        return "too far";
    case QuestObjectVerdict::behind:
        return "not in front of the look";
    case QuestObjectVerdict::notOnQuest:
        return "no active task with its tag";
    case QuestObjectVerdict::alreadyDestroyed:
        return "destroyed before";
    case QuestObjectVerdict::sameStrike:
        return "the same strike";
    }
    return "?";
}

QuestObjects::QuestObjects(World& world, const gamedata::QuestObjectData* data) : m_world(world), m_data(data) {}

void QuestObjects::refuse(QuestObjectVerdict verdict) {
    ++m_counts.refused[verdict_index(verdict)];
}

void QuestObjects::strike(Entity& character, std::uint32_t visit, std::uint32_t strike) {
    const gamedata::QuestObjectData* objects = data();
    if (objects == nullptr || character.kind != EntityKind::player || character.life.dead || character.positionEstimated) {
        return;
    }
    ++m_counts.strikes;
    const std::vector<std::uint32_t> inReach = objects->nearby(character.position, quest_object_rules::kReach, &m_counts.visits);
    if (inReach.empty()) {
        return;
    }
    // The look of the camera (radians from north, positive to the west): the swing goes in its direction.
    const float forwardX = -std::sin(character.look);
    const float forwardY = std::cos(character.look);
    for (const std::uint32_t index : inReach) {
        const gamedata::QuestObjectPlacement& object = objects->objects()[index];
        const float dx = object.position.x - character.position.x;
        const float dy = object.position.y - character.position.y;
        const float dz = object.position.z - character.position.z;
        if (dx * dx + dy * dy + dz * dz > quest_object_rules::kReach * quest_object_rules::kReach) {
            refuse(QuestObjectVerdict::tooFar);
            continue;
        }
        if (dx * forwardX + dy * forwardY < 0.0F) {
            refuse(QuestObjectVerdict::behind);
            continue;
        }
        if (object.onlyOnQuest && !m_world.quests().quest_object_open(character, object.tag)) {
            refuse(QuestObjectVerdict::notOnQuest);
            continue;
        }
        QuestObjectHits& hits = character.questObjects[index];
        if (hits.destroyed) {
            refuse(QuestObjectVerdict::alreadyDestroyed);
            continue;
        }
        if (hits.hits > 0 && hits.visit == visit && hits.strike == strike) {
            refuse(QuestObjectVerdict::sameStrike);
            continue;
        }
        hits.visit = visit;
        hits.strike = strike;
        ++hits.hits;
        ++m_counts.hits;
        const gamedata::QuestObjectRow& row = objects->rows()[object.row];
        const std::string mark = log::session_mark(character.owner, character.characterId);
        const log::Context context(mark);
        log::write("world: quest object: %s hits %s (%s, %s) %u of %u, %.2f m", character.name.c_str(), object.key.c_str(), object.variant.c_str(),
                   row.id.c_str(), hits.hits, work_of(row), static_cast<double>(std::sqrt(dx * dx + dy * dy + dz * dz)));
        if (hits.hits >= work_of(row)) {
            destroy(character, index);
        }
    }
}

void QuestObjects::destroy(Entity& character, std::uint32_t index) {
    const gamedata::QuestObjectPlacement& object = m_data->objects()[index];
    const gamedata::QuestObjectRow& row = m_data->rows()[object.row];
    QuestObjectHits& hits = character.questObjects[index];
    // The quest drops of the row (TaskGatherItems, TaskGatherAndReturnItems: ItemDropVC names the gatherable row; 2 tasks of the data name a
    // destructible row): one transaction, as for a gather. A full bag refuses the destruction: the object keeps its last unit of work.
    const std::vector<QuestDrop> drops = m_world.quests().plan_drops(character, true, {row.id});
    if (!drops.empty()) {
        std::vector<GrantItem> given;
        for (const QuestDrop& drop : drops) {
            given.push_back(GrantItem{drop.itemId, drop.quantity, std::nullopt, {}});
        }
        const Bag bagBefore = character.bag;
        state::Store::Batch batch(m_world.store());
        bool written = batch.open();
        ItemResult grant;
        if (written) {
            grant = m_world.inventory().grant(character.characterId, given, 0, nullptr, true);
            written = grant.error == ItemError::none;
        }
        written = written && m_world.quests().write_drops(character, drops) && batch.commit();
        if (!written) {
            character.bag = bagBefore;
            m_world.touch(character.id, ComponentKey::container);
            hits.hits = work_of(row) - 1;
            ++m_counts.storeFailures;
            log::write("world: quest object: the quest drops of %s for %s did not go into the bag (%s): the object stays", object.key.c_str(),
                       character.characterId.c_str(), item_error_text(grant.error));
            return;
        }
        m_world.quests().drops_done(character, drops);
    }
    hits.destroyed = true;
    hits.generation = ++m_generation;
    ++m_counts.destroyed;
    publish(character);
    // The respawn for this character: one uniform draw of the bounds of the row on the world clock.
    float seconds = row.respawnMin;
    if (row.respawnMax > row.respawnMin) {
        seconds += m_world.random_unit() * (row.respawnMax - row.respawnMin);
    }
    const auto delay = std::chrono::milliseconds(static_cast<long long>(std::lround(std::max(seconds, 1.0F) * 1000.0F)));
    const std::string characterId = character.characterId;
    const std::uint64_t generation = hits.generation;
    m_world.after(delay, [characterId, index, generation](World& world) { world.quest_objects().respawn_due(characterId, index, generation); });
    log::write("world: quest object: %s destroyed %s (%s), the game event %s; it is back for the character in %.1f s", character.name.c_str(),
               object.key.c_str(), object.variant.c_str(), row.event.empty() ? "(none)" : row.event.c_str(), static_cast<double>(seconds));
    // The events of the quests after the change (the rule of a gather of a quest object, Gathering::quest_events): the game event of the
    // row (TaskGameEvent) and the InteractTag of the object (TaskInteract: a completed interaction).
    if (!row.event.empty()) {
        QuestEvent applied;
        applied.kind = QuestEventKind::gameEvent;
        applied.keys = {row.event};
        m_world.quests().event(character, applied);
    }
    if (!object.tag.empty()) {
        QuestEvent interacted;
        interacted.kind = QuestEventKind::interact;
        interacted.keys = {object.tag};
        m_world.quests().event(character, interacted);
    }
}

void QuestObjects::respawn_due(const std::string& characterId, std::uint32_t index, std::uint64_t generation) {
    Entity* character = m_world.find_character(characterId);
    if (character == nullptr) {
        return; // The character left: its objects went with its entity.
    }
    const auto found = character->questObjects.find(index);
    if (found == character->questObjects.end() || !found->second.destroyed || found->second.generation != generation) {
        return;
    }
    character->questObjects.erase(found);
    ++m_counts.respawns;
    publish(*character);
}

void QuestObjects::left(const Entity& character) {
    (void)character; // The state is in the entity: it goes with it.
}

void QuestObjects::publish(Entity& character) {
    std::vector<LootTrackerEntry> entries;
    for (const auto& [index, hits] : character.questObjects) {
        if (!hits.destroyed || m_data == nullptr || index >= m_data->objects().size()) {
            continue;
        }
        const gamedata::QuestObjectPlacement& object = m_data->objects()[index];
        LootTrackerEntry entry;
        entry.actor = object.actor;
        entry.state = quest_object_rules::kLooted;
        entry.kind = quest_object_rules::kEntryKind;
        entry.position = object.position;
        entries.push_back(entry);
    }
    if (entries != character.questObjectTracker) {
        character.questObjectTracker = std::move(entries);
        m_world.touch(character.id, ComponentKey::lootTracker);
    }
}

} // namespace soulwarden::backend::world

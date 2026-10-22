#include "world/quest_objects.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "core/logging/log.h"
#include "world/navigation.h"
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
    case QuestObjectVerdict::wall:
        return "a solid between the character and the object";
    case QuestObjectVerdict::wallDataPending:
        return "the navigation data of the line is not there yet";
    case QuestObjectVerdict::cutscene:
        return "a cutscene or a cinematic plays";
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
    // Code review of stage 1.8e, finding QR8E-R3: a strike while a cutscene that the server granted plays (the 6706 of the client) or a
    // cinematic plays (6606) hits nothing. The client takes no attack input then; a late step of an attack from before its start comes here
    // (also a strike that waited for its due time: Combat::run_event calls this at that time).
    if (character.quests.cutsceneStarted || character.quests.pendingCinematic != 0) {
        refuse(QuestObjectVerdict::cutscene);
        return;
    }
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
        // Code review of stage 1.8e, finding QR8E-R2: no solid between the character and the object (Navigation::line_stop, the line of a
        // projectile), on a line at the eye height of the sight of the navigation. The line ends at the near side of the object: the first
        // point from its place toward the character that no solid holds (the solid of the object itself in the navigation data: about 1.1 m
        // for a nodule, probe of stage 1.8f), at most kBody. A tile of the line that the world does not have yet: no hit now (the tile comes,
        // and the next strike tries again).
        const state::WorldPosition from{character.position.x, character.position.y, character.position.z + quest_object_rules::kLineHeight};
        const state::WorldPosition place{object.position.x, object.position.y, object.position.z + quest_object_rules::kLineHeight};
        if (!m_world.navigation().line_ready(from, place)) {
            refuse(QuestObjectVerdict::wallDataPending);
            continue;
        }
        const float flat = std::sqrt(dx * dx + dy * dy);
        float side = std::min(quest_object_rules::kBody, flat);
        for (float step = 0.0F; step < side; step += quest_object_rules::kBodyStep) {
            const state::WorldPosition point{place.x - dx / flat * step, place.y - dy / flat * step, place.z};
            if (!m_world.navigation().line_stop(point, point)) {
                side = step;
                break;
            }
        }
        if (flat > side) {
            if (const std::optional<float> stop = m_world.navigation().line_stop(from, {place.x - dx / flat * side, place.y - dy / flat * side, place.z})) {
                refuse(QuestObjectVerdict::wall);
                const std::string mark = log::session_mark(character.owner, character.characterId);
                const log::Context context(mark);
                log::write("world: quest object: %s strikes %s through a solid (%.2f m from the character, the near side %.2f m before the object): "
                           "no hit",
                           character.name.c_str(), object.key.c_str(), static_cast<double>(*stop * (flat - side)), static_cast<double>(side));
                continue;
            }
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
    // The quest change of the destruction is one plan and one transaction of the quest system (code review of stage 1.8e, finding QR8E-R4):
    // the quest drops of the row (TaskGatherItems, TaskGatherAndReturnItems: ItemDropVC names the gatherable row; 2 tasks of the data name a
    // destructible row), the game event of the row (TaskGameEvent: MSQ_CorruptedNodule_1_interact), and the InteractTag of the object
    // (TaskInteract: a completed interaction; the rule of a gather of a quest object, Gathering::quest_events). The object goes only after
    // the commit. A refusal (a full bag, a refused write) keeps the object with its last unit of work, and no LootTracker entry.
    const std::vector<QuestDrop> drops = m_world.quests().plan_drops(character, true, {row.id});
    std::vector<QuestEvent> events;
    if (!row.event.empty()) {
        QuestEvent applied;
        applied.kind = QuestEventKind::gameEvent;
        applied.keys = {row.event};
        events.push_back(std::move(applied));
    }
    if (!object.tag.empty()) {
        QuestEvent interacted;
        interacted.kind = QuestEventKind::interact;
        interacted.keys = {object.tag};
        events.push_back(std::move(interacted));
    }
    if (const QuestError error = m_world.quests().object_destroyed(character, drops, events); error != QuestError::none) {
        hits.hits = work_of(row) - 1;
        ++m_counts.storeFailures;
        log::write("world: quest object: the quest change of the destruction of %s for %s was refused (%s): the object stays",
                   object.key.c_str(), character.characterId.c_str(), quest_error_text(error));
        return;
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

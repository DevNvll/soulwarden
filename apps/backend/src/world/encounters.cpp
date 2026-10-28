#include "world/encounters.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "core/logging/log.h"
#include "gamedata/loot_tables.h"
#include "gamedata/quest_data.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

float distance3(const state::WorldPosition& a, const state::WorldPosition& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

const char* member_kind_text(gamedata::EncounterMemberKind kind) {
    switch (kind) {
    case gamedata::EncounterMemberKind::creature:
        return "creature";
    case gamedata::EncounterMemberKind::object:
        return "start object";
    case gamedata::EncounterMemberKind::unknown:
        break;
    }
    return "unknown";
}

std::string short_slice(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

const char* encounter_phase_text(EncounterPhase phase) {
    switch (phase) {
    case EncounterPhase::waiting:
        return "waiting";
    case EncounterPhase::running:
        return "running";
    case EncounterPhase::succeeded:
        return "succeeded";
    case EncounterPhase::failed:
        return "failed";
    }
    return "unknown";
}

const char* encounter_end_text(EncounterEnd end) {
    switch (end) {
    case EncounterEnd::succeeded:
        return "success";
    case EncounterEnd::failed:
        return "failure";
    case EncounterEnd::dormant:
        return "no character near the placement";
    case EncounterEnd::command:
        return "a command";
    }
    return "unknown";
}

Encounters::Encounters(World& world, const gamedata::EncounterData* data) : m_world(world), m_data(data) {}

std::int64_t Encounters::cell_of(float value) {
    return static_cast<std::int64_t>(std::floor(value / encounter_rules::kCellSize));
}

void Encounters::load() {
    m_loaded = true;
    if (data() == nullptr) {
        return;
    }
    m_placements.reserve(m_data->recipes().size());
    for (const gamedata::EncounterRecipe& recipe : m_data->recipes()) {
        EncounterPlacement placement;
        placement.recipe = &recipe;
        resolve(placement);
        const auto index = static_cast<std::uint32_t>(m_placements.size());
        ++m_counts.placements;
        if (placement.refused.empty()) {
            ++m_counts.taken;
            m_cells[{cell_of(recipe.position.x), cell_of(recipe.position.y)}].push_back(index);
        }
        m_placements.push_back(std::move(placement));
    }
    log::write("world: encounters: %llu quest encounter recipes, %llu taken by the runner, %zu refused (docs/systems/encounters.md)",
               static_cast<unsigned long long>(m_counts.placements), static_cast<unsigned long long>(m_counts.taken),
               static_cast<std::size_t>(m_counts.placements - m_counts.taken));
}

void Encounters::resolve(EncounterPlacement& placement) {
    const gamedata::EncounterRecipe& recipe = *placement.recipe;
    placement.refused = recipe.refused;
    const std::vector<gamedata::EncounterMember>& members = m_data->members();
    placement.kinds.assign(members.size(), nullptr);
    placement.templates.assign(members.size(), nullptr);
    const auto refuse = [&](std::string cause) {
        if (std::find(placement.refused.begin(), placement.refused.end(), cause) == placement.refused.end()) {
            placement.refused.push_back(std::move(cause));
        }
    };
    if (!recipe.autoActivate) {
        refuse("the manager does not start the encounter by itself");
    }
    if (!(recipe.detectorRadius > 0.0F)) {
        refuse("the detector of the participants has no radius");
    }
    if (recipe.stageOrder.empty()) {
        refuse("the manager names no stage");
    }
    const gamedata::CreatureData* creatures = m_world.creatures().data();
    const gamedata::InteractionData* interactions = m_world.interactions().data();
    const gamedata::GatheringData* gathering = m_world.gathering().data();
    for (const gamedata::EncounterStage& stage : recipe.stages) {
        for (const gamedata::EncounterSpawn& spawn : stage.spawns) {
            if (spawn.memberIndex >= members.size()) {
                refuse("a spawn of the stage " + stage.name + " names a member that the file does not have");
                continue;
            }
            const gamedata::EncounterMember& member = members[spawn.memberIndex];
            const std::string name = short_slice(member.slice);
            if (member.kind == gamedata::EncounterMemberKind::creature) {
                std::string cause;
                const std::optional<std::uint32_t> kind = creatures != nullptr && creatures->loaded()
                                                              ? creatures->member_kind(member.slice, &cause) : std::nullopt;
                if (!kind) {
                    refuse("the creature member " + name + ": " + (cause.empty() ? std::string("the backend has no creature data") : cause));
                } else {
                    placement.kinds[spawn.memberIndex] = &creatures->kinds()[*kind];
                }
            } else if (member.kind == gamedata::EncounterMemberKind::object) {
                const gamedata::EncounterObject* object = interactions != nullptr ? interactions->encounter_object(member.slice) : nullptr;
                const std::optional<gamedata::GatheringLink> link = gathering != nullptr ? gathering->encounter_object(member.slice)
                                                                                         : std::nullopt;
                if (object == nullptr) {
                    refuse("the start object " + name + " has no template of the interaction data (encounterObjects)");
                } else if (!link) {
                    refuse("the start object " + name + " has no row of the gathering data (encounterObjects)");
                } else {
                    placement.templates[spawn.memberIndex] = object;
                }
            } else if (!(stage.objective.kind == gamedata::EncounterObjectiveKind::destroyed && !stage.objective.tracked.empty()
                         && !counts_toward(recipe, stage, spawn))) {
                // A member that is no creature and no start object counts only as a visual of a stage that tracks a different slice.
                refuse("the member " + name + " is " + member_kind_text(member.kind) + ": " + member.cause);
            }
        }
    }
}

bool Encounters::counts_toward(const gamedata::EncounterRecipe& recipe, const gamedata::EncounterStage& stage,
                               const gamedata::EncounterSpawn& spawn) const {
    (void)recipe;
    const std::string& tracked = stage.objective.tracked;
    if (tracked.empty()) {
        return true;
    }
    if (tracked == spawn.member) {
        return true;
    }
    const gamedata::EncounterMember* member = m_data->member(spawn.member);
    return member != nullptr
           && (tracked == member->body || std::find(member->bodySlices.begin(), member->bodySlices.end(), tracked) != member->bodySlices.end());
}

std::optional<std::uint32_t> Encounters::placement_of(const std::string& key) const {
    const std::string lower = gamedata::lower_text(key);
    for (std::uint32_t index = 0; index < m_placements.size(); ++index) {
        if (m_placements[index].recipe->key == lower) {
            return index;
        }
    }
    return std::nullopt;
}

EncounterInstance* Encounters::instance_of(std::uint32_t placement, std::uint64_t generation) {
    const auto live = m_live.find(placement);
    if (live == m_live.end()) {
        return nullptr;
    }
    const auto found = m_instances.find(live->second);
    return found != m_instances.end() && found->second.generation == generation ? &found->second : nullptr;
}

void Encounters::schedule(WorldClock::time_point at, const Due& due) {
    m_due.emplace(at, due);
}

void Encounters::tick() {
    if (!m_loaded) {
        if (data() == nullptr) {
            return; // No data yet: the system loads at the first tick with data (a test sets its data after the start of the world).
        }
        load();
    }
    if (data() == nullptr || (m_counts.taken == 0 && m_payouts.empty())) {
        return;
    }
    take_events();
    take_due();
    const WorldClock::time_point now = m_world.clock().now();
    if (!m_lastScan || now - *m_lastScan >= encounter_rules::kScanInterval) {
        m_lastScan = now;
        const LoadTimer timer(m_scanLoad);
        scan();
    }
}

void Encounters::scan() {
    ++m_counts.scans;
    const WorldClock::time_point now = m_world.clock().now();
    // The placement rule: the cells near each character (MP-8: no look at each placement of the world).
    std::set<std::uint32_t> near;
    m_world.for_each_character([&](const Entity& character) {
        const std::int64_t lowX = cell_of(character.position.x - encounter_rules::kSpawnEnter);
        const std::int64_t highX = cell_of(character.position.x + encounter_rules::kSpawnEnter);
        const std::int64_t lowY = cell_of(character.position.y - encounter_rules::kSpawnEnter);
        const std::int64_t highY = cell_of(character.position.y + encounter_rules::kSpawnEnter);
        for (std::int64_t x = lowX; x <= highX; ++x) {
            for (std::int64_t y = lowY; y <= highY; ++y) {
                const auto cell = m_cells.find({x, y});
                if (cell == m_cells.end()) {
                    continue;
                }
                for (const std::uint32_t index : cell->second) {
                    ++m_counts.placementsLooked;
                    if (horizontal_distance(character.position, m_placements[index].recipe->position) <= encounter_rules::kSpawnEnter) {
                        near.insert(index);
                    }
                }
            }
        }
    });
    for (const std::uint32_t index : near) {
        EncounterPlacement& placement = m_placements[index];
        if (!placement.live && (!placement.respawnAt || now >= *placement.respawnAt)) {
            make_instance(index);
        }
    }
    // The leave rule and the participants of each instance (the characters near its root: the position index).
    std::vector<EntityId> dormant;
    for (auto& [root, instance] : m_instances) {
        const Entity* entity = m_world.find(root);
        if (entity == nullptr) {
            continue;
        }
        bool someone = false;
        m_world.for_each_near(entity->position, encounter_rules::kSpawnLeave, [&](Entity& other) {
            ++m_counts.entitiesLooked;
            someone = someone || other.kind == EntityKind::player;
        });
        if (!someone) {
            dormant.push_back(root);
            continue;
        }
        update_participants(instance);
    }
    for (const EntityId root : dormant) {
        end_instance(root, EncounterEnd::dormant);
    }
    // A fight with no participant: the encounter fails (a reset; a rule of this project, docs/systems/encounters.md).
    std::vector<EntityId> failed;
    for (const auto& [root, instance] : m_instances) {
        if (instance.phase != EncounterPhase::running || !instance.participants.empty()) {
            continue;
        }
        const bool fighting = std::any_of(instance.members.begin(), instance.members.end(), [](const auto& entry) {
            return !entry.second.object && !entry.second.done;
        });
        if (fighting) {
            failed.push_back(root);
        }
    }
    for (const EntityId root : failed) {
        const auto found = m_instances.find(root);
        if (found != m_instances.end()) {
            log::write("world: encounters: the encounter %s (generation %llu) has a fight and no participant: it fails",
                       m_placements[found->second.placement].recipe->key.c_str(), static_cast<unsigned long long>(found->second.generation));
            manager_status(found->second, encounter_rules::kFailed);
            end_instance(root, EncounterEnd::failed);
        }
    }
}

void Encounters::update_participants(EncounterInstance& instance) {
    if (instance.phase != EncounterPhase::running) {
        return;
    }
    const gamedata::EncounterRecipe& recipe = *m_placements[instance.placement].recipe;
    const Entity* root = m_world.find(instance.root);
    if (root == nullptr) {
        return;
    }
    const WorldClock::time_point now = m_world.clock().now();
    std::set<std::string> inside;
    // The detector sphere (m_Radius, OnlyPlayerTarget, m_ignoreHeight 0: a sphere in three dimensions).
    m_world.for_each_near(root->position, recipe.detectorRadius, [&](Entity& entity) {
        ++m_counts.entitiesLooked;
        if (entity.kind == EntityKind::player && distance3(entity.position, root->position) <= recipe.detectorRadius) {
            inside.insert(entity.characterId);
        }
    });
    for (const std::string& characterId : inside) {
        auto found = instance.participants.find(characterId);
        if (found != instance.participants.end()) {
            found->second.graceUntil.reset();
            continue;
        }
        if (instance.participants.size() >= encounter_rules::kMaxParticipants) {
            continue;
        }
        instance.participants.emplace(characterId, EncounterParticipant{now, std::nullopt});
        ++m_counts.participantsAdded;
        if (Entity* character = m_world.find_character(characterId)) {
            link(*character, instance.root, true);
            log::write("world: encounters: character %s is a participant of the encounter %s (generation %llu)", characterId.c_str(),
                       recipe.key.c_str(), static_cast<unsigned long long>(instance.generation));
            if (!recipe.participantEvent.empty()) {
                // m_participantAddedGameEventId (game data: 0503_DryadAltar_WaveParticipant): the event of a new participant.
                QuestEvent event;
                event.kind = QuestEventKind::gameEvent;
                event.keys = {recipe.participantEvent};
                (void)m_world.quests().event(*character, event);
            }
        }
    }
    for (auto participant = instance.participants.begin(); participant != instance.participants.end();) {
        if (inside.count(participant->first) != 0) {
            ++participant;
            continue;
        }
        if (!participant->second.graceUntil) {
            participant->second.graceUntil = now + std::chrono::seconds(recipe.participantGrace);
        }
        if (now >= *participant->second.graceUntil) {
            if (Entity* character = m_world.find_character(participant->first)) {
                link(*character, instance.root, false);
            }
            log::write("world: encounters: character %s is no participant of the encounter %s any more (out of the sphere for %u s)",
                       participant->first.c_str(), recipe.key.c_str(), recipe.participantGrace);
            ++m_counts.participantsLeft;
            participant = instance.participants.erase(participant);
            continue;
        }
        ++participant;
    }
}

void Encounters::link(Entity& character, EntityId root, bool on) {
    std::vector<EntityId>& links = character.encounterLinks;
    const auto found = std::find(links.begin(), links.end(), root);
    if (on == (found != links.end())) {
        return;
    }
    if (on) {
        links.push_back(root);
    } else {
        links.erase(found);
        m_world.quests().encounter_unlinked(character, root);
    }
    character.encounterLinked = true;
    m_world.touch(character.id, ComponentKey::objectives);
}

void Encounters::make_instance(std::uint32_t index) {
    EncounterPlacement& placement = m_placements[index];
    const gamedata::EncounterRecipe& recipe = *placement.recipe;
    EncounterBody body;
    body.asset = recipe.asset;
    body.subId = recipe.sub;
    body.managerSlot = recipe.managerSlot;
    body.placeSlot = recipe.placeSlot;
    body.objectivesSlot = recipe.objectivesSlot;
    body.rotation = recipe.rotation;
    body.placement = index;
    body.generation = ++placement.generation;
    body.managerStatus = encounter_rules::kNotStarted;
    body.objectiveCrc = gamedata::quest_crc(recipe.rootName);
    for (const gamedata::EncounterStage& stage : recipe.stages) {
        body.stages.push_back(EncounterStageState{stage.slot, 0, {}});
    }
    Entity* root = m_world.make_encounter(body, recipe.position);
    if (root == nullptr) {
        placement.respawnAt = m_world.clock().now() + encounter_rules::kScanInterval * 20;
        return;
    }
    // The objective of the root: its instance id is the entity id of the root (a new instance has a new id: a new banner of the client).
    root->encounter.objectiveInstance = root->id;
    EncounterInstance instance;
    instance.placement = index;
    instance.generation = body.generation;
    instance.root = root->id;
    instance.stages.resize(recipe.stages.size());
    instance.made = m_world.clock().now();
    placement.live = true;
    placement.respawnAt.reset();
    m_live[index] = root->id;
    EncounterInstance& stored = m_instances[root->id] = instance;
    ++m_counts.made;
    log::write("world: encounters: the encounter %s (%s) is in the world: root %016llx, generation %llu, at %.2f %.2f %.2f",
               recipe.key.c_str(), short_slice(recipe.slice).c_str(), static_cast<unsigned long long>(root->id),
               static_cast<unsigned long long>(stored.generation), static_cast<double>(recipe.position.x),
               static_cast<double>(recipe.position.y), static_cast<double>(recipe.position.z));
    if (recipe.activateDelay.count() > 0) {
        schedule(m_world.clock().now() + recipe.activateDelay, Due{DueKind::activate, index, stored.generation, 0, 0, 0});
    } else {
        activate(stored);
    }
}

void Encounters::activate(EncounterInstance& instance) {
    if (instance.phase != EncounterPhase::waiting) {
        return;
    }
    const gamedata::EncounterRecipe& recipe = *m_placements[instance.placement].recipe;
    instance.phase = EncounterPhase::running;
    manager_status(instance, encounter_rules::kInProgress);
    instance.current = 0;
    start_stage(instance, recipe.stageOrder.front());
    // The participants of the start (the characters near the root now).
    update_participants(instance);
}

void Encounters::start_stage(EncounterInstance& instance, std::uint32_t index) {
    const gamedata::EncounterRecipe& recipe = *m_placements[instance.placement].recipe;
    const gamedata::EncounterStage& stage = recipe.stages[index];
    EncounterStageRun& run = instance.stages[index];
    run.started = true;
    stage_status(instance, index, encounter_rules::kStageStarted);
    log::write("world: encounters: the encounter %s (generation %llu) starts the stage %s", recipe.key.c_str(),
               static_cast<unsigned long long>(instance.generation), stage.name.c_str());
    if (stage.killSpawnsAtStart) {
        remove_members(instance, false);
    }
    const WorldClock::time_point now = m_world.clock().now();
    if (stage.objective.timeLimit > 0.0F) {
        const auto time = std::chrono::milliseconds(static_cast<std::int64_t>(std::lround(stage.objective.timeLimit * 1000.0F)));
        schedule(now + time, Due{DueKind::stageTime, instance.placement, instance.generation, index, 0, 0});
    }
    for (std::uint32_t spawn = 0; spawn < stage.spawns.size(); ++spawn) {
        if (stage.spawns[spawn].delay.count() > 0) {
            schedule(now + stage.spawns[spawn].delay, Due{DueKind::spawn, instance.placement, instance.generation, index, spawn, 0});
        } else {
            this->spawn(instance, index, spawn);
        }
    }
    publish_objectives(instance);
}

void Encounters::spawn(EncounterInstance& instance, std::uint32_t index, std::uint32_t entry) {
    EncounterPlacement& placement = m_placements[instance.placement];
    const gamedata::EncounterRecipe& recipe = *placement.recipe;
    const gamedata::EncounterStage& stage = recipe.stages[index];
    EncounterStageRun& run = instance.stages[index];
    if (instance.phase != EncounterPhase::running || !run.started || run.ended) {
        return; // A spawn of a stage that ended (its due entry came later): no member.
    }
    const gamedata::EncounterSpawn& spawn = stage.spawns[entry];
    const gamedata::EncounterMember& member = m_data->members()[spawn.memberIndex];
    for (std::uint32_t number = 0; number < spawn.count; ++number) {
        if (instance.members.size() >= encounter_rules::kMaxMembers) {
            log::write("world: encounters: the encounter %s has %zu members: no more", recipe.key.c_str(), instance.members.size());
            return;
        }
        state::WorldPosition place = recipe.position;
        float yaw = recipe.yaw;
        if (!spawn.locations.empty()) {
            const gamedata::EncounterPlace& location = spawn.locations[number % spawn.locations.size()];
            place = location.position;
            yaw = location.yaw;
        }
        Entity* entity = nullptr;
        if (member.kind == gamedata::EncounterMemberKind::creature && placement.kinds[spawn.memberIndex] != nullptr) {
            entity = m_world.creatures().spawn_member(*placement.kinds[spawn.memberIndex], place, yaw, instance.placement, instance.generation);
            if (entity != nullptr) {
                ++m_counts.membersMade;
            }
        } else if (member.kind == gamedata::EncounterMemberKind::object && placement.templates[spawn.memberIndex] != nullptr) {
            const gamedata::EncounterObject& object = *placement.templates[spawn.memberIndex];
            const std::string key = recipe.key + "/" + std::to_string(instance.generation) + "/" + std::to_string(index) + "/"
                                    + std::to_string(entry) + "/" + std::to_string(number);
            const Interactions::SpawnedContainer child = m_world.interactions().spawn_container(object.templateIndex, place, yaw, key);
            entity = child.entity;
            if (entity != nullptr) {
                // The variation of the slice (the VariationData state): without it the client shows no prompt (live check L3).
                entity->object.variationSlot = object.variationSlot;
                entity->object.variant = object.variant;
                m_world.touch(entity->id, ComponentKey::variation);
                m_world.gathering().encounter_object_made(*entity, member.slice);
                m_objects[entity->id] = member.slice;
                ++m_counts.objectsMade;
            }
        } else {
            continue; // A visual of the stage that the backend does not make (resolve).
        }
        if (entity == nullptr) {
            log::write("world: encounters: the encounter %s did not make its member %s at %.2f %.2f %.2f", recipe.key.c_str(),
                       short_slice(member.slice).c_str(), static_cast<double>(place.x), static_cast<double>(place.y),
                       static_cast<double>(place.z));
            continue;
        }
        instance.members[entity->id] = EncounterMemberRun{index, entry, member.kind == gamedata::EncounterMemberKind::object, false};
        m_memberOf[entity->id] = {instance.placement, instance.generation};
        ++run.spawned;
        if (member.kind == gamedata::EncounterMemberKind::creature && recipe.overrideAggroDelay) {
            // m_overrideAggroDelay of the manager (game data; the AITargetCollector of the root): the member takes the nearest participant
            // as its target after m_aggroDelayOverride (0 s in the recipes of the stage: at once).
            if (recipe.aggroDelay > 0.0F) {
                const auto delay = std::chrono::milliseconds(static_cast<std::int64_t>(std::lround(recipe.aggroDelay * 1000.0F)));
                schedule(m_world.clock().now() + delay, Due{DueKind::aggro, instance.placement, instance.generation, index, 0, entity->id});
            } else {
                provoke(instance, *entity);
            }
        }
        log::write("world: encounters: the encounter %s (generation %llu, stage %s) makes the %s %s: entity %016llx at %.2f %.2f %.2f",
                   recipe.key.c_str(), static_cast<unsigned long long>(instance.generation), stage.name.c_str(),
                   member_kind_text(member.kind), short_slice(member.slice).c_str(), static_cast<unsigned long long>(entity->id),
                   static_cast<double>(place.x), static_cast<double>(place.y), static_cast<double>(place.z));
    }
}

void Encounters::provoke(EncounterInstance& instance, Entity& member) {
    const Entity* nearest = nullptr;
    for (const auto& [characterId, participant] : instance.participants) {
        const Entity* character = m_world.find_character(characterId);
        if (character != nullptr && !character->life.dead
            && (nearest == nullptr || distance3(character->position, member.position) < distance3(nearest->position, member.position))) {
            nearest = character;
        }
    }
    if (nearest != nullptr) {
        m_world.creatures().behavior().provoke(member, nearest->id);
    }
}

void Encounters::member_died(const Entity& creature) {
    if (creature.kind != EntityKind::creature || !creature.creature.encounter) {
        return;
    }
    m_events.push_back(Event{true, creature.id, creature.creature.encounter->first, creature.creature.encounter->second, {}});
}

void Encounters::object_gathered(EntityId object, const Entity& character) {
    const auto found = m_memberOf.find(object);
    if (found == m_memberOf.end()) {
        ++m_counts.oldGeneration; // An old interaction: the object of an instance that went away.
        return;
    }
    m_events.push_back(Event{false, object, found->second.first, found->second.second, character.characterId});
}

const std::string* Encounters::start_object(EntityId object) const {
    const auto found = m_objects.find(object);
    return found != m_objects.end() ? &found->second : nullptr;
}

void Encounters::take_events() {
    std::vector<Event> events;
    events.swap(m_events);
    for (const Event& event : events) {
        EncounterInstance* instance = instance_of(event.placement, event.generation);
        if (instance == nullptr || instance->members.count(event.entity) == 0) {
            ++m_counts.oldGeneration;
            log::write("world: encounters: a %s of the old generation %llu of the encounter %s changes nothing",
                       event.death ? "death" : "gather", static_cast<unsigned long long>(event.generation),
                       event.placement < m_placements.size() ? m_placements[event.placement].recipe->key.c_str() : "?");
            continue;
        }
        if (event.death) {
            ++m_counts.memberDeaths;
        } else {
            // The gather of a start object (the stage tracks its slice): the object goes, and the stage counts it (SliceDestroyedObjective).
            ++m_counts.objectsGathered;
            log::write("world: encounters: character %s used the start object %016llx of the encounter %s", event.character.c_str(),
                       static_cast<unsigned long long>(event.entity), m_placements[event.placement].recipe->key.c_str());
            m_objects.erase(event.entity);
            m_memberOf.erase(event.entity);
            m_world.interactions().remove_object(event.entity);
        }
        count_destroyed(*instance, event.entity);
    }
}

void Encounters::count_destroyed(EncounterInstance& instance, EntityId member) {
    const auto found = instance.members.find(member);
    if (found == instance.members.end() || found->second.done) {
        return;
    }
    found->second.done = true;
    const gamedata::EncounterRecipe& recipe = *m_placements[instance.placement].recipe;
    const gamedata::EncounterStage& stage = recipe.stages[found->second.stage];
    EncounterStageRun& run = instance.stages[found->second.stage];
    if (instance.phase != EncounterPhase::running || !run.started || run.ended
        || stage.objective.kind != gamedata::EncounterObjectiveKind::destroyed) {
        return;
    }
    if (!counts_toward(recipe, stage, stage.spawns[found->second.spawn])) {
        return;
    }
    ++run.destroyed;
    log::write("world: encounters: the encounter %s (stage %s): %u of %u destroyed", recipe.key.c_str(), stage.name.c_str(), run.destroyed,
               stage.objective.requiredCount);
    publish_objectives(instance);
    if (run.destroyed >= stage.objective.requiredCount) {
        end_stage(instance, found->second.stage, true);
    }
}

void Encounters::take_due() {
    const WorldClock::time_point now = m_world.clock().now();
    std::vector<Due> due;
    const auto end = m_due.upper_bound(now);
    for (auto entry = m_due.begin(); entry != end; ++entry) {
        due.push_back(entry->second);
    }
    m_due.erase(m_due.begin(), end);
    for (const Due& entry : due) {
        ++m_counts.dueTaken;
        if (entry.kind == DueKind::payout) {
            pay(entry.payout);
            continue;
        }
        EncounterInstance* instance = instance_of(entry.placement, entry.generation);
        if (instance == nullptr) {
            ++m_counts.oldGeneration; // An old timer: its instance ended.
            continue;
        }
        switch (entry.kind) {
        case DueKind::activate:
            activate(*instance);
            break;
        case DueKind::spawn:
            spawn(*instance, entry.stage, entry.spawn);
            break;
        case DueKind::stageTime: {
            const gamedata::EncounterStage& stage = m_placements[entry.placement].recipe->stages[entry.stage];
            if (instance->phase == EncounterPhase::running && instance->stages[entry.stage].started && !instance->stages[entry.stage].ended) {
                end_stage(*instance, entry.stage, stage.objective.succeedOnTimeout);
            }
            break;
        }
        case DueKind::aggro:
            if (Entity* member = m_world.find(static_cast<EntityId>(entry.payout)); member != nullptr && instance->members.count(member->id) != 0) {
                provoke(*instance, *member);
            }
            break;
        case DueKind::payout:
            break;
        }
    }
}

void Encounters::end_stage(EncounterInstance& instance, std::uint32_t index, bool success) {
    const gamedata::EncounterRecipe& recipe = *m_placements[instance.placement].recipe;
    const gamedata::EncounterStage& stage = recipe.stages[index];
    instance.stages[index].ended = true;
    stage_status(instance, index, success ? encounter_rules::kStageSucceeded : encounter_rules::kStageFailed);
    log::write("world: encounters: the encounter %s (generation %llu): the stage %s %s", recipe.key.c_str(),
               static_cast<unsigned long long>(instance.generation), stage.name.c_str(), success ? "succeeded" : "failed");
    if (stage.killSpawnsAtEnd) {
        remove_members(instance, false);
    }
    publish_objectives(instance);
    if (!success) {
        instance.phase = EncounterPhase::failed;
        manager_status(instance, encounter_rules::kFailed);
        end_instance(instance.root, EncounterEnd::failed);
        return;
    }
    ++instance.current;
    if (instance.current < recipe.stageOrder.size()) {
        start_stage(instance, recipe.stageOrder[instance.current]);
        return;
    }
    succeed(instance);
}

void Encounters::succeed(EncounterInstance& instance) {
    const gamedata::EncounterRecipe& recipe = *m_placements[instance.placement].recipe;
    instance.phase = EncounterPhase::succeeded;
    manager_status(instance, encounter_rules::kSucceeded);
    ++m_counts.succeeded;
    if (!recipe.successEvent.empty()) {
        // The credit (docs/systems/encounters.md): a snapshot of the participants and the generation; the event of the recipe goes one time
        // to each of them. The ledger keeps a recipient until its event is in the store.
        EncounterPayout payout;
        payout.placement = instance.placement;
        payout.generation = instance.generation;
        payout.event = recipe.successEvent;
        payout.result = 0xE4C0000000000000ULL ^ (static_cast<std::uint64_t>(instance.placement) << 32) ^ instance.generation;
        for (const auto& [characterId, participant] : instance.participants) {
            payout.pending.push_back(characterId);
        }
        log::write("world: encounters: the encounter %s (generation %llu) succeeded: the event %s for %zu participant(s)",
                   recipe.key.c_str(), static_cast<unsigned long long>(instance.generation), recipe.successEvent.c_str(),
                   payout.pending.size());
        m_payouts.push_back(std::move(payout));
        pay(m_payouts.size() - 1);
    } else {
        log::write("world: encounters: the encounter %s (generation %llu) succeeded (no success event of the recipe)", recipe.key.c_str(),
                   static_cast<unsigned long long>(instance.generation));
    }
    end_instance(instance.root, EncounterEnd::succeeded);
}

void Encounters::pay(std::size_t index) {
    if (index >= m_payouts.size() || m_payouts[index].pending.empty()) {
        return;
    }
    EncounterPayout& payout = m_payouts[index];
    ++payout.attempts;
    std::vector<std::string> pending;
    for (const std::string& characterId : payout.pending) {
        Entity* character = m_world.find_character(characterId);
        if (character == nullptr) {
            // A recipient that left the world gets no event (the backend has no event of a character that is not in the world).
            log::write("world: encounters: the participant %s of the encounter event %s is not in the world: no event", characterId.c_str(),
                       payout.event.c_str());
            continue;
        }
        QuestEvent event;
        event.kind = QuestEventKind::gameEvent;
        event.keys = {payout.event};
        event.result = payout.result;
        event.callerRetries = true;
        const auto matched = payout.matched.find(characterId);
        if (matched != payout.matched.end()) {
            event.matched = matched->second;
            event.restricted = true;
            event.attempt = payout.attempts - 1;
        }
        const QuestEventOutcome outcome = m_world.quests().event(*character, event);
        if (outcome.result == QuestEventResult::storeFailed) {
            if (matched == payout.matched.end()) {
                payout.matched[characterId] = outcome.matched;
            }
            pending.push_back(characterId);
            continue;
        }
        ++m_counts.credits;
        log::write("world: encounters: the event %s for character %s: %s", payout.event.c_str(), characterId.c_str(),
                   quest_event_result_text(outcome.result));
    }
    payout.pending = std::move(pending);
    if (payout.pending.empty()) {
        payout.matched.clear();
        return;
    }
    if (payout.attempts >= encounter_rules::kPayoutAttempts) {
        m_counts.creditsDropped += payout.pending.size();
        log::write("world: encounters: the store did not take the event %s for %zu participant(s) in %u attempts: no event",
                   payout.event.c_str(), payout.pending.size(), payout.attempts);
        payout.pending.clear();
        payout.matched.clear();
        return;
    }
    ++m_counts.creditRetries;
    schedule(m_world.clock().now() + encounter_rules::kPayoutRetry, Due{DueKind::payout, payout.placement, payout.generation, 0, 0, index});
}

void Encounters::remove_members(EncounterInstance& instance, bool objectsToo) {
    std::vector<EntityId> removed;
    for (auto& [entity, member] : instance.members) {
        if (member.object && !objectsToo) {
            continue;
        }
        if (m_memberOf.erase(entity) == 0) {
            continue; // Gone already.
        }
        removed.push_back(entity);
        member.done = true;
    }
    for (const EntityId entity : removed) {
        if (m_objects.erase(entity) != 0) {
            m_world.interactions().remove_object(entity);
        } else {
            m_world.creatures().remove_member(entity);
        }
        ++m_counts.membersRemoved;
    }
}

void Encounters::end_instance(EntityId root, EncounterEnd end) {
    const auto found = m_instances.find(root);
    if (found == m_instances.end()) {
        return;
    }
    EncounterInstance instance = std::move(found->second);
    m_instances.erase(found);
    EncounterPlacement& placement = m_placements[instance.placement];
    const gamedata::EncounterRecipe& recipe = *placement.recipe;
    for (const auto& [characterId, participant] : instance.participants) {
        if (Entity* character = m_world.find_character(characterId)) {
            link(*character, root, false);
        }
    }
    // The root spawner has m_destroySpawnsOnDestroyed (game data): the members and the objects of the instance go with it.
    remove_members(instance, true);
    m_world.remove_encounter(root);
    m_live.erase(instance.placement);
    placement.live = false;
    const WorldClock::time_point now = m_world.clock().now();
    if (end == EncounterEnd::succeeded || end == EncounterEnd::failed) {
        // The parent spawner makes a new instance after its respawn time: a uniform draw of its respawn range (an area spawner, stage QG5c1
        // rule; the cooldowns of a point spawner).
        const float low = placement.recipe->parent.respawnMin;
        const float high = std::max(low, placement.recipe->parent.respawnMax);
        const float seconds = low + (high - low) * m_world.random_unit();
        placement.respawnAt = now + std::chrono::milliseconds(static_cast<std::int64_t>(std::lround(seconds * 1000.0F)));
        if (end == EncounterEnd::failed) {
            ++m_counts.failed; // succeed() counts a success.
        }
        log::write("world: encounters: the encounter %s (generation %llu) ended (%s); a new instance in %.1f s", recipe.key.c_str(),
                   static_cast<unsigned long long>(instance.generation), encounter_end_text(end), static_cast<double>(seconds));
    } else {
        placement.respawnAt.reset();
        if (end == EncounterEnd::dormant) {
            ++m_counts.dormant;
        }
        log::write("world: encounters: the encounter %s (generation %llu) ended (%s)", recipe.key.c_str(),
                   static_cast<unsigned long long>(instance.generation), encounter_end_text(end));
    }
}

void Encounters::entity_removed(const Entity& entity) {
    const auto found = m_memberOf.find(entity.id);
    if (found != m_memberOf.end()) {
        // A member that a different path removed (a command): it is done, and it does not count as destroyed.
        if (EncounterInstance* instance = instance_of(found->second.first, found->second.second)) {
            if (auto member = instance->members.find(entity.id); member != instance->members.end()) {
                member->second.done = true;
            }
        }
        m_memberOf.erase(found);
        m_objects.erase(entity.id);
    }
}

void Encounters::character_left(const Entity& character) {
    for (auto& [root, instance] : m_instances) {
        if (instance.participants.erase(character.characterId) != 0) {
            ++m_counts.participantsLeft;
        }
    }
}

std::optional<EntityId> Encounters::use_start_object(Entity& character) {
    // DEVELOPMENT AID (the development routes only): the start object of a running stage near the character, as at the commit of its gather
    // (the game event of its row for the character, then the stage counts it). The rule of a gather (the prompt, the range, the time, the
    // tool) is not checked here.
    const Entity* nearest = nullptr;
    for (const auto& [object, slice] : m_objects) {
        const Entity* entity = m_world.find(object);
        if (entity != nullptr && horizontal_distance(entity->position, character.position) <= 10.0F
            && (nearest == nullptr || horizontal_distance(entity->position, character.position)
                                          < horizontal_distance(nearest->position, character.position))) {
            nearest = entity;
        }
    }
    if (nearest == nullptr) {
        return std::nullopt;
    }
    const gamedata::EncounterMember* member = m_data->member(m_objects.at(nearest->id));
    if (member != nullptr && !member->event.empty()) {
        QuestEvent event;
        event.kind = QuestEventKind::gameEvent;
        event.keys = {member->event};
        (void)m_world.quests().event(character, event);
    }
    log::write("world: encounters: DEVELOPMENT COMMAND: character %s uses the start object %016llx (no gather)", character.characterId.c_str(),
               static_cast<unsigned long long>(nearest->id));
    const EntityId id = nearest->id;
    object_gathered(id, character);
    return id;
}

bool Encounters::reset(const std::string& key) {
    const std::optional<std::uint32_t> index = placement_of(key);
    if (!index) {
        return false;
    }
    const auto live = m_live.find(*index);
    if (live == m_live.end()) {
        m_placements[*index].respawnAt.reset();
        return false;
    }
    end_instance(live->second, EncounterEnd::command);
    return true;
}

void Encounters::stage_status(EncounterInstance& instance, std::uint32_t stage, std::uint32_t status) {
    Entity* root = m_world.find(instance.root);
    if (root == nullptr || stage >= root->encounter.stages.size()) {
        return;
    }
    EncounterStageState& state = root->encounter.stages[stage];
    state.history.emplace_back(++state.sequence, status);
    if (state.history.size() > encounter_rules::kHistory) {
        state.history.erase(state.history.begin(), state.history.end() - static_cast<std::ptrdiff_t>(encounter_rules::kHistory));
    }
    m_world.touch(instance.root, ComponentKey::encounterStages);
}

void Encounters::manager_status(EncounterInstance& instance, std::uint32_t status) {
    Entity* root = m_world.find(instance.root);
    if (root == nullptr || root->encounter.managerStatus == status) {
        return;
    }
    root->encounter.managerStatus = status;
    m_world.touch(instance.root, ComponentKey::encounterManager);
}

void Encounters::publish_objectives(EncounterInstance& instance) {
    Entity* root = m_world.find(instance.root);
    if (root == nullptr) {
        return;
    }
    // The generated task tree of the client (static read B of stage QG5c2: 0x44650F0, 0x272E0E0): from 1, the leaf of each user-wave stage
    // then its wrapper (2k + 1 and 2k + 2 for the user-wave stage k), and the outer container last (2n + 1). The progress of a leaf is the
    // N of "N / M" of the HUD (field 4 of this state, the setter 0x3ADDDB0).
    const gamedata::EncounterRecipe& recipe = *m_placements[instance.placement].recipe;
    std::vector<EncounterTaskState> tasks;
    for (std::uint32_t index = 0; index < recipe.stages.size(); ++index) {
        const gamedata::EncounterStage& stage = recipe.stages[index];
        const EncounterStageRun& run = instance.stages[index];
        if (stage.userIndex < 0 || !run.started) {
            continue;
        }
        const auto leaf = static_cast<std::uint8_t>(2 * stage.userIndex + 1);
        const std::uint8_t state = run.ended ? quest_state::kComplete : quest_state::kActive;
        const std::uint32_t progress = stage.objective.kind == gamedata::EncounterObjectiveKind::destroyed
                                           ? std::min(run.destroyed, stage.objective.requiredCount)
                                           : 0;
        tasks.push_back(EncounterTaskState{leaf, progress, state});
        tasks.push_back(EncounterTaskState{static_cast<std::uint8_t>(leaf + 1), 0, state});
    }
    if (recipe.userWaves > 0) {
        tasks.push_back(EncounterTaskState{static_cast<std::uint8_t>(2 * recipe.userWaves + 1), 0, quest_state::kActive});
    }
    std::sort(tasks.begin(), tasks.end(), [](const EncounterTaskState& a, const EncounterTaskState& b) { return a.index < b.index; });
    root->encounter.tasks = std::move(tasks);
    m_world.touch(instance.root, ComponentKey::encounterObjectives);
}

std::vector<EncounterView> Encounters::view(const state::WorldPosition& place, float radius) const {
    std::vector<EncounterView> out;
    for (const auto& [root, instance] : m_instances) {
        const gamedata::EncounterRecipe& recipe = *m_placements[instance.placement].recipe;
        const float distance = horizontal_distance(place, recipe.position);
        if (distance > radius) {
            continue;
        }
        EncounterView view;
        view.key = recipe.key;
        view.slice = recipe.slice;
        view.root = root;
        view.generation = instance.generation;
        view.phase = instance.phase;
        view.distance = distance;
        if (instance.current < recipe.stageOrder.size()) {
            const std::uint32_t stage = recipe.stageOrder[instance.current];
            view.stage = recipe.stages[stage].name;
            view.destroyed = instance.stages[stage].destroyed;
            view.required = recipe.stages[stage].objective.requiredCount;
        }
        for (const auto& [characterId, participant] : instance.participants) {
            view.participants.push_back(characterId);
        }
        for (const auto& [entity, member] : instance.members) {
            if (!member.done) {
                view.members.push_back(entity);
            }
        }
        out.push_back(std::move(view));
    }
    std::sort(out.begin(), out.end(), [](const EncounterView& a, const EncounterView& b) { return a.distance < b.distance; });
    return out;
}

} // namespace soulwarden::backend::world

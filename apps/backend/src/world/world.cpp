#include "world/world.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "gamedata/poi_data.h"

namespace soulwarden::backend::world {
namespace {

/**
 * Spawn position of a character that has no saved position (meters): a street in Oxboro,
 * 0.3 m above the ground. A spawn high above the ground makes the client fall for some seconds.
 */
constexpr state::WorldPosition kDefaultSpawn{8880.0F, 4250.0F, 138.5F};
/**
 * The position of an entity goes to the store at this interval. While the character moves,
 * only a position that is this far from the saved position goes to the store. The position of
 * a character at rest goes to the store when it is not the saved position.
 */
constexpr std::chrono::seconds kPositionSaveInterval{5};
constexpr float kPositionSaveDistance = 0.5F;
constexpr float kRestSaveDistance = 0.05F;
/**
 * A character moves when two accepted position reports are this far from each other (meters).
 * The reports of a character at rest are equal.
 */
constexpr float kReportMoveDistance = 0.01F;

constexpr ComponentKey kAllComponents[] = {ComponentKey::metadata,    ComponentKey::player,    ComponentKey::movement,
                                           ComponentKey::vitals,      ComponentKey::appearance, ComponentKey::progression,
                                           ComponentKey::attribute,   ComponentKey::currency,  ComponentKey::container,
                                           ComponentKey::paperdoll,   ComponentKey::categorical, ComponentKey::points,
                                           ComponentKey::abilities, ComponentKey::mana, ComponentKey::cooldowns,
                                           ComponentKey::entitlements, ComponentKey::achievements};
/**
 * The states that a new entity of a character also marks (code review of stage 3.3b-S1, finding F2): the statuses that the
 * character keeps across a new session and their speed table; stage QG7: the faction. The first record has them; their encoders give no
 * fragment for a character with no status and no faction. A full record again (touch_all) does not send them.
 */
constexpr ComponentKey kEnteredComponents[] = {ComponentKey::status, ComponentKey::statMultipliers, ComponentKey::stamina,
                                                ComponentKey::faction, ComponentKey::social};
/**
 * The component states of a spell entity (stage 3.3b-S2a; stage 3.3b-S2b: the place of a slice with no Spell component, the vitals
 * of a summon or a trap). A codec gives no fragment for a state that the slice of the spell does not have.
 */
constexpr ComponentKey kSpellComponents[] = {ComponentKey::metadata, ComponentKey::spell, ComponentKey::spellPlace, ComponentKey::vitals,
                                             ComponentKey::delayedEvents};
/** The component states of a creature. */
// Stage QG5c1: the place of a stationary target (a creature with no movement component; slot 0 for a creature that has one).
// Stage QG5c2: the states of an encounter root.
constexpr ComponentKey kEncounterComponents[] = {ComponentKey::metadata, ComponentKey::encounterPlace, ComponentKey::encounterManager,
                                                 ComponentKey::encounterObjectives, ComponentKey::encounterStages};
constexpr ComponentKey kCreatureComponents[] = {ComponentKey::metadata, ComponentKey::movement, ComponentKey::targetPlace,
                                                ComponentKey::vitals,   ComponentKey::marker,   ComponentKey::status};

} // namespace

World::World(const WorldClock& clock,
             state::Store& store,
             const gamedata::AppearanceTables& appearanceTables,
             WorldOptions options)
    : m_clock(clock),
      m_store(store),
      m_appearanceTables(appearanceTables),
      m_options(options),
      m_inventory(*this, store, options.itemCatalog),
      m_progress(*this, store, options.progressionTables),
      m_skills(*this, store, options.progressionTables),
      m_factions(*this, store, options.progressionTables),
      m_ownership(*this, store, options.progressionTables),
      m_navigation(*this, options.navigationData, options.terrain, options.navigationThread),
      m_creatures(*this, options.creatureData),
      m_combat(*this, options.combatTables, options.weaponActions),
      m_health(*this),
      m_rewards(*this, options.lootTables),
      m_abilities(*this, options.abilityData, options.combatTables),
      m_gear(*this),
      m_passives(*this),
      m_consumables(*this),
      m_runes(*this),
      m_statuses(*this),
      m_spells(*this, options.spellData),
      m_projectiles(*this, options.projectileData, options.combatTables),
      m_stamina(*this),
      m_interactions(*this, options.interactionData),
      m_chests(*this, options.chestData),
      m_lootBags(*this),
      m_itemContainers(*this),
      m_worldTime(*this),
      m_gathering(*this, options.gatheringData),
      m_crafting(*this, options.craftingData),
      m_quests(*this, store, options.questData),
      m_questObjects(*this, options.questObjectData),
      m_encounters(*this, options.encounterData),
      m_persons(*this, options.conversationData),
      m_presets(*this, project_presets()) {
    m_random = options.randomSeed;
    m_quests.set_pois(options.poiData);
    m_quests.set_triggers(options.triggerData);
}

World::~World() {
    stop_thread();
}

void World::post(Task task) {
    std::lock_guard lock(m_mutex);
    m_tasks.push_back(std::move(task));
}

void World::run(Task& task, const char* kind) {
    // One bad task must not stop the world thread: all sessions are on it.
    try {
        task(*this);
    } catch (const std::exception& error) {
        log::write("world: a %s stopped with an exception: %s", kind, error.what());
    } catch (...) {
        log::write("world: a %s stopped with an unknown exception", kind);
    }
}

void World::tick() {
    m_thread = std::this_thread::get_id();
    // The real time of the tick: a measurement, not a time of the world.
    const LoadTimer timer(m_load.worldTick);
    std::vector<Task> tasks;
    {
        std::lock_guard lock(m_mutex);
        tasks.swap(m_tasks);
    }
    for (Task& task : tasks) {
        run(task, "task");
    }
    // The timers that are due now. A timer that one of them adds waits for the next tick, so
    // that a timer with no delay cannot keep the tick in a loop.
    const WorldClock::time_point now = m_clock.now();
    std::vector<Task> due;
    const auto end = m_timers.upper_bound(now);
    for (auto timer = m_timers.begin(); timer != end; ++timer) {
        due.push_back(std::move(timer->second));
    }
    m_timers.erase(m_timers.begin(), end);
    for (Task& task : due) {
        run(task, "timer");
    }
    // The tiles of the navigation data that are ready, and the path searches of this tick. Then
    // the spawn point rule and the behavior of the creatures, on the world clock.
    try {
        m_navigation.tick();
        // The world time before the creatures (stage 2.2): a phase change scans the points with a time constraint in this tick.
        m_worldTime.tick();
        m_creatures.tick();
        m_health.tick();
        m_statuses.tick();
        m_passives.tick();
        m_abilities.tick();
        m_stamina.tick();
        m_spells.tick();
        m_projectiles.tick();
        // The server children of the placements near the characters (stage 2.5b).
        m_interactions.tick();
        // The claims of the chests whose reset time came (stage 3.5b.1).
        m_chests.tick();
        // The bags of the store at the first tick, and the bags whose end came (stage 3.5b.2).
        m_lootBags.tick();
        // The depleted nodes of the store at the first tick, and the nodes whose respawn came (stage 3.6a).
        m_gathering.tick();
        // The characters whose items or place changed: their possession, equip, and place tasks (stage 1.8c; one look each).
        m_quests.tick();
        // Stage QG5a: the characters that moved: their proximity quest objects (one look each, the cells near them).
        m_questObjects.tick();
        // Stage QG5c2: the encounters: the queued deaths and gathers, the due list, the placements near the characters, the participants.
        m_encounters.tick();
        // The characters with a person conversation whose bits changed: the presence of the person (stage 2.5c; one look each).
        m_persons.tick();
    } catch (const std::exception& error) {
        log::write("world: the creature system stopped with an exception: %s", error.what());
    }
    save_due_positions();
}

void World::after(std::chrono::milliseconds delay, Task task) {
    m_timers.emplace(m_clock.now() + delay, std::move(task));
}

void World::start_thread(std::chrono::milliseconds period) {
    if (m_running.exchange(true)) {
        return;
    }
    if (m_worker.joinable()) {
        m_worker.join(); // A worker that one of its tasks stopped.
    }
    m_worker = std::thread([this, period] {
        while (m_running) {
            tick();
            std::this_thread::sleep_for(period);
        }
        release_thread();
    });
}

void World::stop_thread() {
    m_running = false;
    // A task of the worker can ask for the stop: the worker then ends after its tick, and it
    // cannot wait for itself. A different thread, and the destructor, wait for a worker that
    // did not get its join, also when the run flag is off already.
    if (m_worker.joinable() && m_worker.get_id() != std::this_thread::get_id()) {
        m_worker.join();
    }
}

void World::release_thread() {
    // Only the thread that is the owner gives the world away: a different thread can be the
    // owner already.
    std::thread::id owner = std::this_thread::get_id();
    m_thread.compare_exchange_strong(owner, std::thread::id{});
}

void World::forget_thread() {
    m_thread = std::thread::id{};
}

state::AppearanceIndexes World::appearance_of(const state::Character& character) const {
    if (character.creationParams.empty()) {
        log::write("world: character %s has no creation data: default appearance", character.characterId.c_str());
        return state::kDefaultAppearance;
    }
    const auto creationData = state::read_creation_data(character.creationParams);
    if (!creationData) {
        log::write("world: the creation data of character %s did not parse: default appearance",
                   character.characterId.c_str());
        return state::kDefaultAppearance;
    }
    const auto indexes = m_appearanceTables.indexes(creationData->appearance);
    if (!indexes) {
        log::write("world: no appearance tables: character %s gets the default appearance",
                   character.characterId.c_str());
        return state::kDefaultAppearance;
    }
    std::string text;
    for (const std::uint8_t index : *indexes) {
        text += " " + std::to_string(index);
    }
    log::write("world: appearance of character %s:%s", character.characterId.c_str(), text.c_str());
    return *indexes;
}

Entity& World::make_entity(const state::Character& character) {
    const ActorId actor = m_ids.next(ActorKind::playerCharacter);
    Entity& entity = m_entities[entity_group_id(actor)];
    entity.id = entity_group_id(actor);
    entity.actor = actor;
    entity.kind = EntityKind::player;
    entity.characterId = character.characterId;
    entity.personaId = character.personaId;
    entity.name = character.name;
    if (character.position && valid_position(*character.position)) {
        entity.position = *character.position;
        entity.positionEstimated = true;
        entity.savedPosition = *character.position;
    } else {
        entity.position = kDefaultSpawn;
    }
    entity.appearance = appearance_of(character);
    m_inventory.load(entity);
    m_progress.load(entity);
    m_skills.load(entity);
    // Stage QG7: the faction, before the quests (their gates read it).
    m_factions.load(entity);
    m_abilities.entered(entity);
    // The statuses, the item cooldowns, the charge, and the values of the equipment (stages 1.3b and 3.3b-S1): the gear
    // values need the statuses, and the gear refresh gives the statuses of the items again.
    m_statuses.entered(entity);
    m_consumables.entered(entity);
    m_runes.entered(entity);
    m_gear.refresh(entity);
    // The stamina (stage 3.4f-B): each world entry starts with the full stamina, as the health; the client starts full too.
    m_stamina.entered(entity);
    const Revision revision = ++m_revision;
    entity.positionRevision = revision;
    for (const ComponentKey key : kAllComponents) {
        entity.changed[key] = revision;
    }
    for (const ComponentKey key : kEnteredComponents) {
        entity.changed[key] = revision;
    }
    m_characters[entity.characterId] = entity.id;
    m_index.insert(entity.id, entity.position);
    // The entitlements and the achievements of the DLC (stage 1.1b), before the first record: the client reads the
    // expansion of the character before its level window and its mastery tree. After the insert of the entity: the
    // reward item of a DLC pack goes into the bag of the entity.
    m_ownership.entered(entity);
    // The loot claims of the character on the chests (stage 3.5b.1): its rows, and its LootTracker.
    m_chests.entered(entity);
    // The states of the loot bags of the character (stage 3.5b.2): its LootTracker entries and its storage map.
    m_lootBags.entered(entity);
    // The cooldowns of the recipes of the character (stage 3.7b): its rows with an active window, and its Crafting state.
    m_crafting.entered(entity);
    // The quests of the character (stage 1.8a), after its achievement bits: its saved quests, its timers, its pending one-time grants
    // (the start objective of a new character). Their states go to the client after the first 2709 of its session.
    m_quests.entered(entity);
    log::write("world: entity %016llx (actor %s) of character %s (%s) at %.2f %.2f %.2f",
               static_cast<unsigned long long>(entity.id),
               actor_text(entity.actor).c_str(),
               entity.characterId.c_str(),
               entity.name.c_str(),
               entity.position.x,
               entity.position.y,
               entity.position.z);
    return entity;
}

void World::remove_entity(EntityId id) {
    const auto found = m_entities.find(id);
    if (found == m_entities.end()) {
        return;
    }
    Entity& entity = found->second;
    m_index.erase(id, entity.position);
    // The committed interaction of a character ends with its entity (stage 2.5a: the users of the object).
    if (entity.kind == EntityKind::player) {
        m_interactions.left(entity);
        m_persons.left(entity);
        // The open of a chest ends, and the claims leave the memory (stage 3.5b.1; the rows stay).
        m_chests.left(entity);
        m_lootBags.left(entity);
        // The work of a gather ends (stage 3.6a: a work is not kept over a leave).
        m_gathering.left(entity);
        // The craft batch ends (stage 3.7a: the completed jobs are in the store; the base ended the station use before).
        m_crafting.left(entity);
        // The task index of the character goes (stage 1.8c: its quests stay in the store; its timers end themselves).
        m_quests.left(entity);
        // Stage QG5c2: the character is no participant of an encounter (its link goes with its entity).
        m_encounters.character_left(entity);
    }
    // Stage QG5c2: a member or a start object of an encounter that a different path removed is done for its instance.
    m_encounters.entity_removed(entity);
    // The auras that follow the entity end with it (stage 3.3b-S2b: no aura stays with no attachment).
    if (entity.kind == EntityKind::creature || entity.kind == EntityKind::player) {
        m_spells.target_left(id);
    }
    if (entity.kind == EntityKind::creature) {
        log::write("world: creature %016llx (%s) removed",
                   static_cast<unsigned long long>(entity.id),
                   entity.creature.kind->id.c_str());
        m_creatures.entity_removed(entity);
        // The external object of its body goes (stage 3.6a: the skinning of a body; a use of it ends at its next check).
        m_interactions.body_removed(entity.id);
        // The entry of its skinned body goes with it (code review of stage 3.6a, finding GA-R1).
        m_gathering.body_removed(entity.id);
        // The projectiles of the creature in the air end with no hit (stage 3.4f-A: the leave of the source).
        m_projectiles.source_left(entity.id);
        // The ground areas of the creature end with it (stage 3.4f-C: the leave of the caster, the rule of a character).
        m_spells.caster_left(entity.id);
    } else if (entity.kind == EntityKind::projectile) {
        // A projectile of a creature (stage 3.4f-A): the projectile system ended its flight before the call.
    } else if (entity.kind == EntityKind::probe) {
        log::write("world: DEVELOPMENT COMMAND: probe entity %016llx removed", static_cast<unsigned long long>(entity.id));
    } else if (entity.kind == EntityKind::object) {
        // A server child (stage 2.5b): the interaction system ended its uses and its grants before the call.
    } else if (entity.kind == EntityKind::spell) {
        m_spells.entity_removed(entity);
    } else if (entity.kind == EntityKind::encounter) {
        // The root of an encounter (stage QG5c2): the encounter system ended its instance before the call.
    } else {
        // The spells of the character end (a cancel: stage 3.3b-S2a). Their entities go away before the entity of the caster.
        m_spells.caster_left(entity.id);
        // The projectiles of the character end with no hit (stage 3.3b-S3).
        m_projectiles.caster_left(entity.id);
        log::write("world: entity %016llx of character %s removed",
                   static_cast<unsigned long long>(entity.id),
                   entity.characterId.c_str());
        // The activation of a heart rune ends with no explosion (finding F4 of the review of stage 1.3b), and the statuses
        // of the gear end (the next entry gives them again from the items). The other statuses stay with the character.
        m_runes.left(found->second);
        // The auras that give the character their statuses take them back while the entity is here (code review of stage
        // 3.3b-S2b, finding F6).
        m_spells.member_left(found->second);
        // The statuses of the rules near the character end: the next entry evaluates them again (stage 3.3b-S5).
        m_passives.left(found->second);
        m_statuses.left(found->second);
        // A dodge of the character ends (stage 3.4f-B): a payment before its mark is cancelled.
        m_stamina.interrupted(entity.characterId, "the character left the world");
        m_characters.erase(entity.characterId);
    }
    m_dirtyPositions.erase(id);
    if (entity.owner != 0) {
        m_owned.erase(entity.owner);
    }
    m_entities.erase(found);
    ++m_revision;
}

void World::damage_dealt(SessionId session, const DamageDealt& damage) {
    const auto sink = m_sinks.find(session);
    if (sink != m_sinks.end() && sink->second != nullptr) {
        sink->second->damage_dealt(damage);
    }
}

void World::healed_player(SessionId session, EntityId target, float amount) {
    const auto sink = m_sinks.find(session);
    if (sink != m_sinks.end() && sink->second != nullptr) {
        sink->second->healed_player(target, amount);
    }
}

void World::interaction_cancelled(SessionId session) {
    const auto sink = m_sinks.find(session);
    if (sink != m_sinks.end() && sink->second != nullptr) {
        sink->second->interaction_cancelled();
    }
}

void World::whisper(SessionId session, std::uint32_t whisperCrc) {
    const auto sink = m_sinks.find(session);
    if (sink != m_sinks.end() && sink->second != nullptr) {
        sink->second->whisper(whisperCrc);
    }
}

void World::damage_taken(SessionId session, const DamageTaken& damage) {
    const auto sink = m_sinks.find(session);
    if (sink != m_sinks.end() && sink->second != nullptr) {
        sink->second->damage_taken(damage);
    }
}

void World::died(SessionId session, float respawnSeconds) {
    const auto sink = m_sinks.find(session);
    if (sink != m_sinks.end() && sink->second != nullptr) {
        sink->second->died(respawnSeconds);
    }
}

std::optional<bool> World::auto_pin(const Entity& entity, std::uint8_t type) const {
    const auto sink = entity.owner != 0 ? m_sinks.find(entity.owner) : m_sinks.end();
    return sink != m_sinks.end() && sink->second != nullptr ? sink->second->auto_pin(type) : std::nullopt;
}

bool World::development_message(const std::string& characterId, std::uint32_t type, const std::vector<std::uint8_t>& body) {
    const Entity* entity = find_character(characterId);
    const auto sink = entity != nullptr && entity->owner != 0 ? m_sinks.find(entity->owner) : m_sinks.end();
    if (sink == m_sinks.end() || sink->second == nullptr) {
        return false;
    }
    sink->second->development_message(type, body);
    return true;
}

EnterResult World::enter(SessionId session,
                         SessionSink& sink,
                         const std::string& characterId,
                         const std::string& personaId) {
    EnterResult result;
    const auto character = m_store.find_character(characterId);
    if (!character) {
        log::write("world: no character %s in the store: session s%u does not enter", characterId.c_str(), session);
        result.error = EnterError::unknownCharacter;
        return result;
    }
    if (character->personaId != personaId) {
        log::write("world: character %s is not a character of account %s: session s%u does not enter",
                   characterId.c_str(),
                   personaId.c_str(),
                   session);
        result.error = EnterError::wrongAccount;
        return result;
    }
    Entity* entity = find_character(characterId);
    if (entity == nullptr) {
        entity = &make_entity(*character);
        // A character that comes into the world: no creature takes it while its client shows the loading screen, and for some time after it.
        m_creatures.behavior().shelter(*entity, true);
    } else if (entity->owner != 0 && entity->owner != session) {
        // A character is in one session at most: the newer session wins. The entity stays
        // with its position, and the row gets the newest position before the change of the
        // owner.
        result.replaced = entity->owner;
        save_position(*entity, 0.0F);
        log::write("world: character %s is in session s%u: that session loses it, and session s%u gets its entity",
                   characterId.c_str(),
                   result.replaced,
                   session);
        m_owned.erase(result.replaced);
        const auto old = m_sinks.find(result.replaced);
        if (old != m_sinks.end()) {
            SessionSink* oldSink = old->second;
            m_sinks.erase(old);
            oldSink->character_lost(LostCause::replaced);
        }
    }
    entity->owner = session;
    m_owned[session] = entity->id;
    m_sinks[session] = &sink;
    result.entity = entity;
    return result;
}

bool World::leave(SessionId session) {
    m_sinks.erase(session);
    const auto owned = m_owned.find(session);
    if (owned == m_owned.end()) {
        return false;
    }
    const EntityId id = owned->second;
    m_owned.erase(owned);
    Entity* entity = find(id);
    if (entity == nullptr || entity->owner != session) {
        return false;
    }
    // The sequence of the basic attacks of the character goes away with its entity (stage 3.3b-M1).
    m_combat.forget_sequence(entity->characterId);
    if (entity->life.dead) {
        // A dead character is not dead at its next world entry: its row gets its respawn
        // place, and each world entry starts with the full health (world/health.h).
        const RespawnPlace respawn = m_health.respawn_place(*entity);
        const std::string mark = log::session_mark(entity->owner, entity->characterId);
        const log::Context context(mark);
        log::write("world: character %s leaves the world dead: its row gets its respawn place (%s)",
                   entity->characterId.c_str(),
                   respawn.name.empty() ? "the place of its death" : respawn.name.c_str());
        place(*entity, respawn.position, true, false);
    }
    save_position(*entity, 0.0F);
    remove_entity(id);
    return true;
}

SpawnError World::spawn_character(const std::string& characterId, const state::WorldPosition& position) {
    if (!valid_position(position)) {
        log::write("world: no entity for character %s: the position is not a position of the world",
                   characterId.c_str());
        return SpawnError::badPosition;
    }
    if (find_character(characterId) != nullptr) {
        return SpawnError::inWorld;
    }
    const auto character = m_store.find_character(characterId);
    if (!character) {
        return SpawnError::unknownCharacter;
    }
    Entity& entity = make_entity(*character);
    m_index.move(entity.id, entity.position, position);
    entity.position = position;
    entity.positionEstimated = false;
    log::write("world: entity %016llx of character %s has no session, position %.2f %.2f %.2f",
               static_cast<unsigned long long>(entity.id),
               characterId.c_str(),
               position.x,
               position.y,
               position.z);
    return SpawnError::none;
}

bool World::remove_character(const std::string& characterId, LostCause cause) {
    Entity* entity = find_character(characterId);
    if (entity == nullptr) {
        return false;
    }
    const SessionId owner = entity->owner;
    const EntityId id = entity->id;
    if (owner != 0) {
        m_owned.erase(owner);
        entity->owner = 0;
        const auto sink = m_sinks.find(owner);
        if (sink != m_sinks.end()) {
            SessionSink* lost = sink->second;
            m_sinks.erase(sink);
            lost->character_lost(cause);
        }
    }
    remove_entity(id);
    return true;
}

Entity& World::make_creature(const Creature& creature, const state::WorldPosition& position, float health) {
    const ActorId actor = m_ids.next(ActorKind::otherEntity);
    Entity& entity = m_entities[entity_group_id(actor)];
    entity.id = entity_group_id(actor);
    entity.actor = actor;
    entity.kind = EntityKind::creature;
    entity.creature = creature;
    entity.name = creature.kind->name.empty() ? creature.kind->id : creature.kind->name;
    entity.position = position;
    entity.vitals = Vitals{};
    entity.vitals.health = health;
    entity.vitals.healthMax = health;
    const Revision revision = ++m_revision;
    entity.positionRevision = revision;
    for (const ComponentKey key : kCreatureComponents) {
        entity.changed[key] = revision;
    }
    m_index.insert(entity.id, entity.position);
    log::write("world: creature %016llx (actor %s) of the kind %s, level %u, health %.1f, at %.2f %.2f %.2f%s",
               static_cast<unsigned long long>(entity.id),
               actor_text(entity.actor).c_str(),
               creature.kind->id.c_str(),
               creature.level,
               static_cast<double>(health),
               static_cast<double>(position.x),
               static_cast<double>(position.y),
               static_cast<double>(position.z),
               creature.spawnPoint ? " (spawn point)" : " (command)");
    return entity;
}

void World::place_creature(Entity& entity, const state::WorldPosition& position) {
    if (entity.kind != EntityKind::creature || !valid_position(position)) {
        return;
    }
    m_index.move(entity.id, entity.position, position);
    entity.position = position;
    entity.positionRevision = ++m_revision;
}

void World::remove_creature(EntityId id) {
    const Entity* entity = find(id);
    if (entity != nullptr && entity->kind == EntityKind::creature) {
        remove_entity(id);
    }
}

Entity* World::make_probe(const ProbeBody& body, const state::WorldPosition& position, std::vector<RawFragment> fragments) {
    if (!valid_position(position)) {
        return nullptr;
    }
    // An actor id of the command (a level placement) must not be the id of an entity of the world.
    if (body.actor && m_entities.count(entity_group_id(*body.actor)) != 0) {
        return nullptr;
    }
    const ActorId actor = body.actor ? *body.actor : m_ids.next(ActorKind::otherEntity);
    Entity& entity = m_entities[entity_group_id(actor)];
    entity.id = entity_group_id(actor);
    entity.actor = actor;
    entity.kind = EntityKind::probe;
    entity.probe = body;
    entity.name = "probe";
    entity.position = position;
    const Revision revision = ++m_revision;
    entity.positionRevision = revision;
    entity.changed[ComponentKey::metadata] = revision;
    for (RawFragment& fragment : fragments) {
        fragment.changed = revision;
    }
    entity.rawFragments = std::move(fragments);
    m_index.insert(entity.id, entity.position);
    log::write("world: DEVELOPMENT COMMAND: probe entity %016llx (actor %s) at %.2f %.2f %.2f with %zu fragment(s)",
               static_cast<unsigned long long>(entity.id),
               actor_text(entity.actor).c_str(),
               static_cast<double>(position.x),
               static_cast<double>(position.y),
               static_cast<double>(position.z),
               entity.rawFragments.size());
    return &entity;
}

Entity& World::make_spell(const SpellBody& body, const state::WorldPosition& position) {
    const ActorId actor = m_ids.next(ActorKind::otherEntity);
    Entity& entity = m_entities[entity_group_id(actor)];
    entity.id = entity_group_id(actor);
    entity.actor = actor;
    entity.kind = EntityKind::spell;
    entity.spell = body;
    entity.name = body.spellId;
    entity.position = position;
    const Revision revision = ++m_revision;
    entity.positionRevision = revision;
    for (const ComponentKey key : kSpellComponents) {
        entity.changed[key] = revision;
    }
    m_lastSpell = revision;
    m_index.insert(entity.id, entity.position);
    return entity;
}

Entity& World::make_projectile(const ProjectileBody& body, const state::WorldPosition& position) {
    const ActorId actor = m_ids.next(ActorKind::otherEntity);
    Entity& entity = m_entities[entity_group_id(actor)];
    entity.id = entity_group_id(actor);
    entity.actor = actor;
    entity.kind = EntityKind::projectile;
    entity.shot = body;
    entity.name = "projectile";
    entity.position = position;
    const Revision revision = ++m_revision;
    entity.positionRevision = revision;
    entity.changed[ComponentKey::metadata] = revision;
    entity.changed[ComponentKey::projectile] = revision;
    m_lastProjectile = revision;
    m_index.insert(entity.id, entity.position);
    return entity;
}

void World::remove_projectile(EntityId id) {
    const Entity* entity = find(id);
    if (entity != nullptr && entity->kind == EntityKind::projectile) {
        remove_entity(id);
    }
}

void World::place_projectile(Entity& entity, const state::WorldPosition& position) {
    if (entity.kind != EntityKind::projectile || !valid_position(position)) {
        return;
    }
    m_index.move(entity.id, entity.position, position);
    entity.position = position;
}

std::uint16_t World::territory_of(const Entity& entity) const {
    const gamedata::PoiData* data = m_options.poiData;
    return data != nullptr && data->loaded() ? data->territory_at(entity.position.x, entity.position.y) : 0;
}

std::array<float, 3> World::velocity_of(const Entity& entity) const {
    if (entity.kind != EntityKind::player || m_clock.now() - entity.velocityAt > kVelocityKeep) {
        return {0.0F, 0.0F, 0.0F};
    }
    return entity.velocity;
}

void World::remove_spell(EntityId id) {
    const Entity* entity = find(id);
    if (entity != nullptr && entity->kind == EntityKind::spell) {
        remove_entity(id);
    }
}

void World::place_spell(Entity& entity, const state::WorldPosition& position) {
    if (entity.kind != EntityKind::spell || !valid_position(position)) {
        return;
    }
    if (entity.position.x == position.x && entity.position.y == position.y && entity.position.z == position.z) {
        return;
    }
    m_index.move(entity.id, entity.position, position);
    entity.position = position;
    entity.positionRevision = ++m_revision;
}

bool World::friendly(const Entity& a, const Entity& b) const {
    if (a.kind != EntityKind::player || b.kind != EntityKind::player) {
        return false;
    }
    return a.characterId == b.characterId || m_testFriends.contains({a.characterId, b.characterId});
}

bool World::hostile(const Entity& a, const Entity& b) const {
    const auto side = [](const Entity& entity) {
        // 1: a creature; 2: a character or a spell of a character; 0: neither.
        return entity.kind == EntityKind::creature ? 1 : entity.kind == EntityKind::player || entity.kind == EntityKind::spell ? 2 : 0;
    };
    return side(a) != 0 && side(b) != 0 && side(a) != side(b);
}

void World::set_test_friends(const std::string& characterA, const std::string& characterB) {
    m_testFriends.insert({characterA, characterB});
    m_testFriends.insert({characterB, characterA});
}

Entity& World::make_object(const ObjectBody& body, const ActorId& actor, const state::WorldPosition& position) {
    Entity& entity = m_entities[entity_group_id(actor)];
    entity.id = entity_group_id(actor);
    entity.actor = actor;
    entity.kind = EntityKind::object;
    entity.object = body;
    entity.name = "object";
    entity.position = position;
    const Revision revision = ++m_revision;
    entity.positionRevision = revision;
    entity.changed[ComponentKey::metadata] = revision;
    entity.changed[ComponentKey::interact] = revision;
    m_index.insert(entity.id, entity.position);
    log::write("world: object entity %016llx (actor %s) of the interaction data, %zu bound(s), at %.2f %.2f %.2f",
               static_cast<unsigned long long>(entity.id), actor_text(entity.actor).c_str(), body.bounds.size(),
               static_cast<double>(position.x), static_cast<double>(position.y), static_cast<double>(position.z));
    return entity;
}

Entity& World::spawn_object(const ObjectBody& body, const state::WorldPosition& position) {
    const ActorId actor = m_ids.next(ActorKind::otherEntity);
    Entity& entity = m_entities[entity_group_id(actor)];
    entity.id = entity_group_id(actor);
    entity.actor = actor;
    entity.kind = EntityKind::object;
    entity.object = body;
    entity.name = "object";
    entity.position = position;
    const Revision revision = ++m_revision;
    entity.positionRevision = revision;
    for (const ComponentKey key : {ComponentKey::metadata, ComponentKey::objectPlace, ComponentKey::buildable, ComponentKey::ownership,
                                   ComponentKey::interact}) {
        entity.changed[key] = revision;
    }
    m_index.insert(entity.id, entity.position);
    return entity;
}

void World::remove_object(EntityId id) {
    // Code review of stage QG5c5, finding QG5C5R-R5: the completion claim of an encounter start object goes with it.
    m_gathering.object_removed(id);
    const Entity* entity = find(id);
    if (entity != nullptr && entity->kind == EntityKind::object && entity->object.spawned) {
        remove_entity(id);
    }
}

Entity* World::make_encounter(const EncounterBody& body, const state::WorldPosition& position) {
    if (!valid_position(position)) {
        return nullptr;
    }
    const ActorId actor = m_ids.next(ActorKind::otherEntity);
    Entity& entity = m_entities[entity_group_id(actor)];
    entity.id = entity_group_id(actor);
    entity.actor = actor;
    entity.kind = EntityKind::encounter;
    entity.encounter = body;
    entity.name = "encounter";
    entity.position = position;
    const Revision revision = ++m_revision;
    entity.positionRevision = revision;
    for (const ComponentKey key : kEncounterComponents) {
        entity.changed[key] = revision;
    }
    m_index.insert(entity.id, entity.position);
    return &entity;
}

void World::remove_encounter(EntityId id) {
    const Entity* entity = find(id);
    if (entity != nullptr && entity->kind == EntityKind::encounter) {
        remove_entity(id);
    }
}

bool World::remove_probe(EntityId id) {
    const Entity* entity = find(id);
    if (entity == nullptr || entity->kind != EntityKind::probe) {
        return false;
    }
    remove_entity(id);
    return true;
}

float World::interest_enter(const Entity& entity) const {
    if (entity.kind == EntityKind::projectile) {
        // A projectile of a creature (stage 3.4f-A, a rule of this project): the distance of a character, while it flies. A session
        // that comes near after its end gets no record of it.
        return entity.shot.ended ? 0.0F : m_options.interestEnter;
    }
    if (entity.kind == EntityKind::probe) {
        return std::min(entity.probe.interestRadius, m_options.interestEnter);
    }
    if (entity.kind == EntityKind::spell) {
        // m_sliceSpawnRadius of the slice of the spell (game data: 50 m for the vortex), not more than the distance of a
        // character. With no slice data: 0 (no session observes it).
        return entity.spell.slice != nullptr ? std::min(entity.spell.slice->interestRadius, m_options.interestEnter) : 0.0F;
    }
    if (entity.kind != EntityKind::creature) {
        return m_options.interestEnter;
    }
    return std::min(std::max(entity.creature.body->interestRadius, m_options.creatureInterestMinimum),
                    m_options.interestEnter);
}

float World::interest_leave(const Entity& entity) const {
    if (entity.kind == EntityKind::projectile) {
        return m_options.interestEnter + m_options.creatureInterestMargin;
    }
    if (entity.kind == EntityKind::probe || entity.kind == EntityKind::spell) {
        return interest_enter(entity) + m_options.creatureInterestMargin;
    }
    if (entity.kind != EntityKind::creature) {
        return m_options.interestLeave;
    }
    return interest_enter(entity) + m_options.creatureInterestMargin;
}

Entity* World::find(EntityId id) {
    const auto found = m_entities.find(id);
    return found == m_entities.end() ? nullptr : &found->second;
}

std::vector<Entity*> World::characters_of_account(const std::string& personaId) {
    std::vector<Entity*> entities;
    for (const auto& [character, id] : m_characters) {
        const auto found = m_entities.find(id);
        if (found != m_entities.end() && found->second.personaId == personaId) {
            entities.push_back(&found->second);
        }
    }
    return entities;
}

Entity* World::find_character(const std::string& characterId) {
    const auto found = m_characters.find(characterId);
    return found == m_characters.end() ? nullptr : find(found->second);
}

Entity* World::entity_of(SessionId session) {
    const auto owned = m_owned.find(session);
    return owned == m_owned.end() ? nullptr : find(owned->second);
}

bool World::set_position(EntityId id, const state::WorldPosition& position, bool moving) {
    Entity* entity = find(id);
    if (entity == nullptr) {
        return false;
    }
    if (entity->kind == EntityKind::player && entity->life.dead) {
        return false; // The camera estimate of the client does not move a dead character.
    }
    if (!valid_position(position)) {
        // The index cannot hold the cell of such a number, and an observer must not get it.
        if (!entity->badPositionLogged) {
            entity->badPositionLogged = true;
            const std::string mark = log::session_mark(entity->owner, entity->characterId);
            const log::Context context(mark);
            log::write("world: a position of entity %016llx is not a position of the world: the entity keeps its "
                       "position",
                       static_cast<unsigned long long>(id));
        }
        return false;
    }
    if (entity->kind == EntityKind::player && entity->owner != 0) {
        m_creatures.behavior().client_arrived(*entity); // The camera estimate of its client.
    }
    place(*entity, position, true, moving);
    return true;
}

bool World::move_character(Entity& entity, const state::WorldPosition& position) {
    if (entity.kind != EntityKind::player || entity.owner != 0 || entity.life.dead || !valid_position(position)) {
        return false;
    }
    place(entity, position, false, true);
    return true;
}

TravelResult World::travel(const std::string& characterId, const state::WorldPosition& position, TravelScreen screen) {
    TravelResult result;
    Entity* entity = find_character(characterId);
    if (entity == nullptr) {
        result.error = TravelError::notInWorld;
        return result;
    }
    if (!travel_position(position)) {
        result.error = TravelError::badPosition;
        return result;
    }
    Travel travel;
    travel.from = entity->position;
    travel.to = position;
    travel.distance =
        std::hypot(position.x - travel.from.x, position.y - travel.from.y, position.z - travel.from.z);
    travel.screen = screen;
    // The row gets the new place first: a travel shows in the entity and in the session only
    // when its row is written. With a write that failed, the row has the old place, and the
    // next world entry of the character would be there. Such a travel changes nothing: the
    // caller gets the error and can do the command again.
    if (!m_store.set_character_position(entity->characterId, position)) {
        const std::string mark = log::session_mark(entity->owner, entity->characterId);
        const log::Context context(mark);
        log::write("world: no travel of character %s (%s) to %.2f %.2f %.2f: the database did not accept the position, "
                   "and the character stays at its place",
                   entity->characterId.c_str(),
                   entity->name.c_str(),
                   static_cast<double>(position.x),
                   static_cast<double>(position.y),
                   static_cast<double>(position.z));
        result.error = TravelError::storage;
        return result;
    }
    // The place is a place on the ground: the client gets it with the spawn lift, as the
    // position of a row, and the character falls to the ground. The next accepted position
    // report of its client is the exact position.
    place(*entity, position, true, false);
    // The position of the entity is the position of the row: a session that ends now saves
    // nothing, and the row has the new place, not the old one.
    entity->savedPosition = position;
    entity->positionDirty = false;
    m_dirtyPositions.erase(entity->id);
    entity->lastSave = m_clock.now();
    // The next number, 1 to 15: the client of the owner takes the position of a movement state
    // only when its teleport number is not the number that the client has, and 0 is the start
    // number of a client. The count starts at the number that the client has, not at the
    // number of the entity: a session sends only the last movement state of a tick, and 15
    // travels in one tick would give the entity the number of the client again. Thus each
    // travel before the next movement state of the owner has the same number. An entity with
    // no client counts from its own number.
    const auto sink = entity->owner != 0 ? m_sinks.find(entity->owner) : m_sinks.end();
    const std::optional<std::uint8_t> known =
        sink != m_sinks.end() && sink->second != nullptr ? sink->second->client_teleport() : std::nullopt;
    entity->teleport = static_cast<std::uint8_t>(known.value_or(entity->teleport) % 15 + 1);
    entity->changed[ComponentKey::movement] = m_revision;
    m_lastTravel = m_revision;
    {
        const std::string mark = log::session_mark(entity->owner, entity->characterId);
        const log::Context context(mark);
        log::write("world: travel of character %s (%s): from %.2f %.2f %.2f to %.2f %.2f %.2f (%.1f m), teleport "
                   "number %u",
                   entity->characterId.c_str(),
                   entity->name.c_str(),
                   static_cast<double>(travel.from.x),
                   static_cast<double>(travel.from.y),
                   static_cast<double>(travel.from.z),
                   static_cast<double>(position.x),
                   static_cast<double>(position.y),
                   static_cast<double>(position.z),
                   static_cast<double>(travel.distance),
                   static_cast<unsigned>(entity->teleport));
        log::write("rep: position %.2f %.2f %.2f saved", position.x, position.y, position.z);
    }
    // The basic attack of the character ends with the travel (stage 3.3b-M1), and its aim of an ability (code review of stage 3.3b-S6,
    // finding S6-R8: a confirm after the travel would use the place before it).
    m_combat.end_sequence(entity->characterId, SequenceEnd::travel);
    m_abilities.cancel_aim(entity->characterId);
    // A dodge of the character ends with the travel (stage 3.4f-B).
    m_stamina.interrupted(entity->characterId, "a travel");
    // The committed interaction of the character ends (stage 2.5a): the session sends 3746 and 6709 (RepSession::traveled).
    m_interactions.traveled(*entity);
    m_persons.traveled(*entity);
    // The creatures of the new place: the spawn point rule runs in this tick, not at its interval.
    m_creatures.scan_soon();
    m_encounters.scan_soon();
    // No creature of the new place takes the character at once, and the creatures of the old place lose it.
    m_creatures.behavior().shelter(*entity, entity->owner != 0);
    if (sink != m_sinks.end() && sink->second != nullptr) {
        sink->second->traveled(travel);
    }
    result.entity = entity;
    result.distance = travel.distance;
    return result;
}

PositionVerdict World::report_position(SessionId session, const PositionReport& report, PositionTrack& track) {
    // The character of a report is the character of the session (rule MP-5).
    Entity* entity = entity_of(session);
    if (entity == nullptr) {
        return PositionVerdict::noEntity;
    }
    if (entity->life.dead) {
        // A dead character stays at the place of its death: a report of its client changes nothing.
        return track.refused(PositionVerdict::dead);
    }
    // The speed multiplier that the client of the session has (the speed table of slot 43 that the session sent; code
    // review of stage 3.3b-S1, finding F1): a haste makes the limit of the window larger only when the client runs faster.
    const float speedFactor = track.client_speed(m_clock.now());
    // The envelope of a dash of an accepted cast (stage 3.3b-S4): the client moves its character with the root motion of
    // the action, one time, and the check accepts it in that envelope.
    const MotionAllowance allowance = m_abilities.motion_allowance(*entity, m_clock.now());
    const PositionVerdict verdict = track.judge(report, entity->position, m_clock.now(), speedFactor, allowance);
    if (verdict != PositionVerdict::accepted) {
        return verdict; // The entity, the index, and the row do not change.
    }
    m_creatures.behavior().client_arrived(*entity); // The client is at the place of its character.
    if (std::isfinite(report.look)) {
        entity->look = report.look; // The look of the camera (Blood Lust: the box in front of it).
    }
    const state::WorldPosition& position = report.position;
    const float moved = std::hypot(position.x - entity->position.x,
                                   position.y - entity->position.y,
                                   position.z - entity->position.z);
    if (moved == 0.0F) {
        // A character at rest sends the same position 30 times each second.
        entity->positionEstimated = false;
        entity->moving = false;
        // Code review of stage QG5a, finding QG5AR-R2: an accepted report with no movement is a look of the proximity objects too (a task
        // that becomes Active, or an object that respawns, while the character stands in the sphere). The set of the marks makes one look
        // for each character at a tick (the cells of the grid near it, as for a move).
        m_questObjects.moved(*entity);
        return verdict;
    }
    place(*entity, position, false, moved >= kReportMoveDistance);
    // In the movement of a cast (a dash), the observers of the character get its position (stage 3.3b-S4).
    m_abilities.caster_moved(*entity);
    return verdict;
}

void World::place(Entity& entity, const state::WorldPosition& position, bool estimated, bool moving) {
    // The velocity of a character (stage 3.4f-A, velocity_of): the change of the place since the sample, over kVelocitySample at least.
    if (entity.kind == EntityKind::player) {
        const WorldClock::time_point now = m_clock.now();
        const auto passed = now - entity.velocitySampleAt;
        if (entity.velocitySampleAt == WorldClock::time_point{} || passed > kVelocityKeep) {
            entity.velocity = {0.0F, 0.0F, 0.0F};
            entity.velocitySample = position;
            entity.velocitySampleAt = now;
        } else if (passed >= kVelocitySample) {
            const float seconds = std::chrono::duration<float>(passed).count();
            std::array<float, 3> velocity{(position.x - entity.velocitySample.x) / seconds, (position.y - entity.velocitySample.y) / seconds,
                                          (position.z - entity.velocitySample.z) / seconds};
            const float speed = std::sqrt(velocity[0] * velocity[0] + velocity[1] * velocity[1] + velocity[2] * velocity[2]);
            entity.velocity = std::isfinite(speed) && speed <= kVelocityLargest ? velocity : std::array<float, 3>{0.0F, 0.0F, 0.0F};
            entity.velocitySample = position;
            entity.velocitySampleAt = now;
            entity.velocityAt = now;
        }
    }
    m_index.move(entity.id, entity.position, position);
    entity.position = position;
    entity.positionEstimated = estimated;
    entity.positionRevision = ++m_revision;
    entity.moving = moving;
    entity.positionDirty = true;
    m_dirtyPositions.insert(entity.id);
    // The range of the committed interaction of the character (stage 2.5b) and of the source of its service grant (code review of stage
    // 2.5b, finding ST-R3): only a character with one (O(1)).
    if (entity.kind == EntityKind::player && (entity.interaction || entity.serviceGrant)) {
        m_interactions.moved(entity);
    }
    // The LootTracker of the character at a new cell (stage 3.5b.1: the claims near it; O(1) when the cell is the same).
    if (entity.kind == EntityKind::player) {
        m_chests.moved(entity);
        // The GlobalStorage selection of its loot bags after a walk (code review of stage 3.5b.2, finding LB-R3; O(1) for a short walk).
        m_lootBags.moved(entity);
        // The tasks of a place of its quests look at the next tick (stage 1.8c; O(1) for a character with no such task).
        m_quests.moved(entity);
        // Stage QG5a: its proximity quest objects look at the next tick (an accepted position only; O(1)).
        if (!estimated) {
            m_questObjects.moved(entity);
        }
    }
}

Revision World::touch(EntityId id, ComponentKey key) {
    Entity* entity = find(id);
    if (entity == nullptr) {
        return 0;
    }
    entity->changed[key] = ++m_revision;
    // Stage 1.8c: a change of the items of a character (each system marks the bag or the paperdoll state after its own change) is the one
    // event of its possession and equip tasks: they look at the next tick (O(1) for a character with no such task).
    if (entity->kind == EntityKind::player && (key == ComponentKey::container || key == ComponentKey::paperdoll)) {
        m_quests.items_touched(*entity);
    }
    // Stage 2.5c: a change of the bits of a character with a person conversation: the presence of the person at the next tick. Stage QG5b:
    // also a change of its quests (the Objectives state): the ObjectiveFlow of a non-persisted achievement reads the quest state.
    if (entity->kind == EntityKind::player && (key == ComponentKey::achievements || key == ComponentKey::objectives)) {
        m_persons.achievements_touched(*entity);
    }
    return m_revision;
}

void World::touch_all(EntityId id) {
    Entity* entity = find(id);
    if (entity == nullptr) {
        return;
    }
    const Revision revision = ++m_revision;
    if (entity->kind == EntityKind::creature) {
        for (const ComponentKey key : kCreatureComponents) {
            entity->changed[key] = revision;
        }
        return;
    }
    if (entity->kind == EntityKind::spell) {
        for (const ComponentKey key : kSpellComponents) {
            entity->changed[key] = revision;
        }
        return;
    }
    if (entity->kind == EntityKind::probe) {
        entity->changed[ComponentKey::metadata] = revision;
        for (RawFragment& fragment : entity->rawFragments) {
            fragment.changed = revision;
        }
        return;
    }
    if (entity->kind == EntityKind::encounter) {
        for (const ComponentKey key : kEncounterComponents) {
            entity->changed[key] = revision;
        }
        return;
    }
    for (const ComponentKey key : kAllComponents) {
        entity->changed[key] = revision;
    }
    // The full record has the full bag and each slot of the paperdoll, not only the slots that changed.
    entity->bag.reset = revision;
    entity->equipment.reset = revision;
}

void World::set_raw_fragment(EntityId id, std::uint32_t slot, std::uint32_t type, std::vector<std::uint8_t> state) {
    Entity* entity = find(id);
    if (entity == nullptr) {
        return;
    }
    RawFragment* fragment = nullptr;
    for (RawFragment& existing : entity->rawFragments) {
        if (existing.slot == slot) {
            fragment = &existing;
        }
    }
    if (fragment == nullptr) {
        fragment = &entity->rawFragments.emplace_back();
    }
    fragment->slot = slot;
    fragment->type = type;
    fragment->state = std::move(state);
    fragment->changed = ++m_revision;
}

bool World::save_position(Entity& entity, float minimumDistance) {
    entity.lastSave = m_clock.now();
    if (!entity.positionDirty) {
        return false;
    }
    if (entity.savedPosition) {
        const float distance = std::hypot(entity.position.x - entity.savedPosition->x,
                                          entity.position.y - entity.savedPosition->y,
                                          entity.position.z - entity.savedPosition->z);
        if (distance == 0.0F) {
            entity.positionDirty = false;
            m_dirtyPositions.erase(entity.id);
            return false;
        }
        if (distance < minimumDistance) {
            return false;
        }
    }
    // The log line names the session that plays the character.
    const std::string mark = log::session_mark(entity.owner, entity.characterId);
    const log::Context context(mark);
    if (!m_store.set_character_position(entity.characterId, entity.position)) {
        // The mark stays: the position is not the position of the row, and the world tries
        // the write again at the next interval.
        log::write("rep: the position of character %s was not saved", entity.characterId.c_str());
        return false;
    }
    entity.positionDirty = false;
    m_dirtyPositions.erase(entity.id);
    entity.savedPosition = entity.position;
    log::write("rep: position %.2f %.2f %.2f saved", entity.position.x, entity.position.y, entity.position.z);
    return true;
}

void World::save_due_positions() {
    if (m_dirtyPositions.empty()) {
        return;
    }
    const WorldClock::time_point now = m_clock.now();
    // save_position changes the set: walk a copy.
    const std::vector<EntityId> dirty(m_dirtyPositions.begin(), m_dirtyPositions.end());
    for (const EntityId id : dirty) {
        Entity* entity = find(id);
        if (entity != nullptr && now - entity->lastSave >= kPositionSaveInterval) {
            save_position(*entity, entity->moving ? kPositionSaveDistance : kRestSaveDistance);
        }
    }
}

} // namespace soulwarden::backend::world

#include "world/spells.h"

#include <algorithm>
#include <cmath>

#include "core/logging/log.h"
#include "gamedata/progression_tables.h"
#include "world/projectiles.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

/**
 * @return A delay of the data on the world clock. A delay of 0 or less is the start of the spell (the game data has -6 s and
 *         0.0001 s in some slices); a positive delay is 1 ms at least (the due list advances: finding F9 of the review of
 *         stage 3.3b-S1).
 */
/** @return The key of a status row in the aura rows: the id with the letters A to Z in lower case. */
std::string lower(std::string_view text) {
    std::string key(text);
    for (char& letter : key) {
        if (letter >= 'A' && letter <= 'Z') {
            letter = static_cast<char>(letter - 'A' + 'a');
        }
    }
    return key;
}

std::chrono::milliseconds milliseconds_of(float seconds) {
    if (!(seconds > 0.0F)) {
        return std::chrono::milliseconds(0);
    }
    return std::chrono::milliseconds(std::max<long long>(static_cast<long long>(std::lround(seconds * 1000.0F)), 1));
}

float distance3(const state::WorldPosition& a, const state::WorldPosition& b) {
    return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

/** @return The distance of a point to the surface of a capsule (0 in it). */
float capsule_gap(const state::WorldPosition& point, const WorldCapsule& capsule) {
    const float abx = capsule.b.x - capsule.a.x;
    const float aby = capsule.b.y - capsule.a.y;
    const float abz = capsule.b.z - capsule.a.z;
    const float squared = abx * abx + aby * aby + abz * abz;
    float part = 0.0F;
    if (squared > 0.0F) {
        part = std::clamp(((point.x - capsule.a.x) * abx + (point.y - capsule.a.y) * aby + (point.z - capsule.a.z) * abz) / squared, 0.0F, 1.0F);
    }
    const state::WorldPosition nearest{capsule.a.x + abx * part, capsule.a.y + aby * part, capsule.a.z + abz * part};
    return std::max(distance3(point, nearest) - capsule.radius, 0.0F);
}

state::WorldPosition capsule_center(const WorldCapsule& capsule) {
    return {(capsule.a.x + capsule.b.x) * 0.5F, (capsule.a.y + capsule.b.y) * 0.5F, (capsule.a.z + capsule.b.z) * 0.5F};
}

/** @return The largest reach of a creature capsule from the place of its creature (a part of the range of a query). */
float creature_reach(const World& world) {
    float reach = projectile_rules::kDefaultRadius * 2.0F + projectile_rules::kDefaultHeight;
    if (const gamedata::CreatureData* creatures = world.options().creatureData) {
        for (const gamedata::CreatureSlice& slice : creatures->slices()) {
            if (slice.capsule) {
                const float offset = std::hypot(slice.capsule->offset[0], slice.capsule->offset[1], slice.capsule->offset[2]);
                reach = std::max(reach, 2.0F * slice.capsule->radius + slice.capsule->height + offset);
            }
        }
    }
    return reach;
}

/**
 * @return The time from the start of a spell to the trigger of its end chain at the init (the trap: the event of its Lifetime
 *         entity that triggers the entity Trigger at 30 s, whose timeline destroys the group 0.508 s later), and the entity of
 *         that trigger. No value: none.
 */
std::optional<std::pair<float, std::string>> life_trigger(const gamedata::SpellSlice& slice) {
    std::optional<std::pair<float, std::string>> found;
    for (const gamedata::SpellEvent& event : slice.events) {
        if (event.type == gamedata::spell_event::kTrigger && event.onInit && slice.destroy_after_trigger(event.target)
            && (!found || event.delay < found->first)) {
            found = std::make_pair(event.delay, event.target);
        }
    }
    return found;
}

/** @return The AreaStatusEffectComponent of a slice that gives its statuses to the enemies (m_detectedTargetType 2), or null (stage 3.4f-C). */
const gamedata::SpellAreaStatus* enemy_area(const gamedata::SpellSlice& slice) {
    for (const gamedata::SpellAreaStatus& area : slice.areaStatuses) {
        if (area.targets == 2 && !area.statuses.empty()) {
            return &area;
        }
    }
    return nullptr;
}

/** @return The detection volume of a strike (stage 3.4f-C): no shape, active between an activation and a deactivation. Null: none. */
const gamedata::SpellVolume* strike_volume(const gamedata::SpellSlice& slice) {
    for (const gamedata::SpellVolume& volume : slice.volumes) {
        if (volume.shape == gamedata::VolumeShape::none && volume.activeAfter >= 0.0F && volume.inactiveAfter > volume.activeAfter) {
            return &volume;
        }
    }
    return nullptr;
}

/** @return The milliseconds of a time of the data (seconds) with no lower bound of 1 ms (a start at 0 s is the start). */
std::chrono::milliseconds at_time(float seconds) {
    return std::chrono::milliseconds(static_cast<long long>(std::lround(std::max(seconds, 0.0F) * 1000.0F)));
}

} // namespace

const char* spell_end_text(SpellEnd cause) {
    switch (cause) {
    case SpellEnd::time:
        return "its end on the world clock";
    case SpellEnd::casterLeft:
        return "its caster left the world";
    case SpellEnd::casterDied:
        return "its caster died (EndOnCasterDeath)";
    case SpellEnd::command:
        return "a command";
    case SpellEnd::destroyed:
        return "its health went to 0";
    case SpellEnd::triggered:
        return "it triggered";
    case SpellEnd::targetLeft:
        return "the entity that it follows left the world";
    case SpellEnd::targetDied:
        return "the entity that it follows died";
    }
    return "unknown";
}

const char* spell_role_text(SpellRole role) {
    switch (role) {
    case SpellRole::area:
        return "area";
    case SpellRole::summon:
        return "summon";
    case SpellRole::trap:
        return "trap";
    case SpellRole::aura:
        return "aura";
    case SpellRole::shield:
        return "shield";
    case SpellRole::visual:
        return "picture";
    case SpellRole::statusArea:
        return "status area";
    case SpellRole::strike:
        return "strike";
    }
    return "unknown";
}

Spells::Spells(World& world, const gamedata::SpellData* data) : m_world(world), m_data(data) {}

std::optional<std::chrono::milliseconds> Spells::activation_time(std::string_view spellId, std::string_view row) const {
    const gamedata::SpellSlice* slice = has_data() ? m_data->slice_of(spellId) : nullptr;
    const std::optional<float> delay = slice != nullptr ? slice->activation_delay(row) : std::nullopt;
    return delay ? std::optional<std::chrono::milliseconds>(milliseconds_of(*delay)) : std::nullopt;
}

void Spells::schedule(EntityId spell, WorldClock::time_point at, DueKind kind, std::int32_t phase) {
    m_due.emplace(at, Due{spell, kind, phase});
}

Entity* Spells::make(const SpellRequest& request) {
    const Entity* caster = m_world.find(request.caster);
    if (caster == nullptr || caster->kind != EntityKind::player || !valid_position(request.position)) {
        return nullptr;
    }
    const gamedata::SpellSlice* slice = has_data() ? m_data->slice_of(request.spellId) : nullptr;
    // A summon, a trap, and an aura need the values of their slice (stage 3.3b-S2b): with no spell data the cast makes no entity.
    if (request.role != SpellRole::area && slice == nullptr) {
        log::write("world: spell: %s of %s: no spell data for the slice (a %s): no entity", request.spellId.c_str(), caster->name.c_str(),
                   spell_role_text(request.role));
        return nullptr;
    }
    // A summon needs the AutoSpell of its slice and a trap its detection volume (the values of format 2): a file of format 1 has
    // none, and the entity would end at once.
    if ((request.role == SpellRole::summon && !slice->autoCast) || (request.role == SpellRole::trap && slice->volume() == nullptr)) {
        log::write("world: spell: %s of %s: the slice has no %s (spell data of format 1): no entity", request.spellId.c_str(),
                   caster->name.c_str(), request.role == SpellRole::summon ? "AutoSpell" : "detection volume");
        return nullptr;
    }
    // A summon and a trap with a Vitals component take damage: they need the vitals row of their slice (code review of stage
    // 3.3b-S2b, finding F8: a Pylon with no row was paid for and took no damage).
    // A Vitals slot with no vitals metadata needs the row too (code review of stage 1.1b, finding F2): a paid summon with no
    // damage path is no entity. A summon and a trap are entities that take damage in each case: a slice with no Vitals component
    // at all (no slot and no metadata) makes no entity either (code review of stage 3.3b-S5, finding F11).
    if ((request.role == SpellRole::summon || request.role == SpellRole::trap)
        && (m_data == nullptr || !slice->vitals || m_data->vitals_row(slice->vitals->row) == nullptr)) {
        log::write("world: spell: %s of %s: the spell data has no vitals row %s of its slice: no entity", request.spellId.c_str(),
                   caster->name.c_str(), slice->vitals ? slice->vitals->row.c_str() : "(none)");
        return nullptr;
    }
    // An aura follows a living entity (finding F3): a projectile that killed its target makes no aura on the body.
    if (request.attach != 0) {
        const Entity* target = m_world.find(request.attach);
        if (target == nullptr || target->life.dead || (target->kind == EntityKind::creature && target->creature.dead)) {
            log::write("world: spell: %s of %s: the entity that it would follow is not in the world or is dead: no entity",
                       request.spellId.c_str(), caster->name.c_str());
            return nullptr;
        }
    }
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    const gamedata::SpellRow* row = abilities != nullptr ? abilities->spell(request.spellId) : nullptr;
    const WorldClock::time_point now = m_world.clock().now();
    SpellBody body;
    body.spellId = row != nullptr ? row->id : request.spellId;
    // The key of the row on the wire (field 0 of the Spell state): the CRC-32 of the lower-case SpellID (client code
    // 0x683DE00 resolves the row with it; live check L4: B5715A91 for the vortex).
    body.key = gamedata::progression_id(body.spellId);
    body.slice = slice;
    body.caster = caster->id;
    body.casterActor = caster->actor;
    body.casterCharacter = caster->characterId;
    body.started = now;
    body.role = request.role;
    body.endsOnCasterDeath = row != nullptr && row->cells.number("EndOnCasterDeath") != 0.0F;
    body.cast = request.cast;
    state::WorldPosition position = request.position;
    // An aura follows the entity that its projectile hit (field 6 of the Spell state; live check of stage 3.3b-S2b).
    if (request.attach != 0) {
        if (const Entity* target = m_world.find(request.attach)) {
            body.attached = target->id;
            body.attachedActor = target->actor;
            position = target->position;
        }
    }
    // The life and the end (game data): the end event of the slice (the vortex), the life of the AutoSpell of a summon, the
    // trigger of the end chain of a trap at the init, or the life of the ability rule (the Duration of the spell row of an aura).
    Live live;
    live.cast = request.cast;
    live.phases = request.phases;
    live.done.assign(request.phases.size(), false);
    live.role = request.role;
    live.radius = request.radius;
    live.statuses = request.statuses;
    // The statuses of an area leave a friend that leaves it, and each friend at its end, when the spell row says so (ClearOnExit,
    // ClearOnEnd; stage 3.3b-M2: the fortify of Clear Out has neither, and its statuses keep their time). An area of a slice component
    // (the fortify in the well of Unyielding) takes them at the exit and at the end (m_removeOnExit of its AreaStatusEffectComponent).
    live.clearOnExit = request.areaStatuses || row == nullptr || row->cells.number("ClearOnExit") != 0.0F;
    live.clearOnEnd = request.areaStatuses || row == nullptr || row->cells.number("ClearOnEnd") != 0.0F;
    live.heal = request.heal;
    live.effects = request.effects;
    live.weapon = request.weapon;
    live.conversion = request.conversion;
    std::optional<std::string> lifeTrigger;
    std::chrono::milliseconds endChain{0};
    if (request.role == SpellRole::summon && slice->autoCast) {
        live.lifeEnds = now + milliseconds_of(slice->autoCast->lifetime);
        lifeTrigger = slice->autoCast->expireTarget;
        endChain = milliseconds_of(slice->destroy_after_trigger(*lifeTrigger).value_or(0.0F));
        body.ends = live.lifeEnds + endChain;
    } else if (request.role == SpellRole::trap && life_trigger(*slice)) {
        const auto [delay, entity] = *life_trigger(*slice);
        live.lifeEnds = now + milliseconds_of(delay);
        lifeTrigger = entity;
        endChain = milliseconds_of(slice->destroy_after_trigger(entity).value_or(0.0F));
        body.ends = live.lifeEnds + endChain;
    } else {
        const std::optional<float> end = slice != nullptr ? slice->end_delay() : std::nullopt;
        body.ends = now + (end ? milliseconds_of(*end) : request.life.value_or(request.fallbackEnd));
        live.lifeEnds = body.ends;
    }
    // The health of a summon or a trap: the base vitals row of its slice (HealthOverridePercentSpellOwnerMaxHealth of the maximum
    // health of the caster, else HealthBaseMax; game data).
    float health = 0.0F;
    if (slice != nullptr && slice->vitals && m_data != nullptr) {
        body.vitalsRow = m_data->vitals_row(slice->vitals->row);
        if (body.vitalsRow != nullptr) {
            health = body.vitalsRow->ownerHealthPart > 0.0F ? body.vitalsRow->ownerHealthPart * caster->vitals.healthMax
                                                            : body.vitalsRow->healthBaseMax;
            health = std::max(health, 1.0F);
        }
    }
    const std::string casterName = caster->name;
    const std::uint32_t casterLevel = caster->progression.level;
    Entity& entity = m_world.make_spell(body, position);
    if (entity.spell.vitalsRow != nullptr) {
        entity.vitals.health = health;
        entity.vitals.healthMax = health;
    }
    // The level of the caster: the level factor of the damage of a creature on a summon (Combat::creature_damage).
    entity.progression.level = casterLevel;
    const EntityId id = entity.id;
    m_byCaster[body.caster].push_back(id);
    if (body.attached != 0) {
        m_byTarget[body.attached].push_back(id);
    }
    for (std::size_t index = 0; index < request.phases.size(); ++index) {
        schedule(id, now + request.phases[index].at, DueKind::phase, static_cast<std::int32_t>(index));
    }
    if (lifeTrigger) {
        schedule(id, live.lifeEnds, DueKind::end);
    } else {
        schedule(id, body.ends, DueKind::end);
    }
    if (request.role == SpellRole::summon && slice->autoCast) {
        schedule(id, now + milliseconds_of(slice->autoCast->initialDelay), DueKind::shot);
    } else if (request.role == SpellRole::trap) {
        const gamedata::SpellVolume* volume = slice->volume();
        if (volume != nullptr && volume->activeAfter >= 0.0F) {
            schedule(id, now + milliseconds_of(volume->activeAfter), DueKind::detect);
        }
    } else if (request.role == SpellRole::aura || request.role == SpellRole::shield || !request.statuses.empty()) {
        // An aura, a shield that follows its caster, and an area with statuses for friends (the well of Unyielding): their steps.
        schedule(id, now, DueKind::aura);
    }
    m_live[id] = std::move(live);
    ++m_counts.made;
    m_counts.largest = std::max(m_counts.largest, m_live.size());
    log::write("world: spell: %s (%s) of %s at %.2f %.2f %.2f, entity %016llx (actor %s): %zu phase(s), the end in %lld ms%s%s%s",
               body.spellId.c_str(),
               spell_role_text(request.role),
               casterName.c_str(),
               static_cast<double>(position.x),
               static_cast<double>(position.y),
               static_cast<double>(position.z),
               static_cast<unsigned long long>(id),
               actor_text(entity.actor).c_str(),
               request.phases.size(),
               static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(entity.spell.ends - now).count()),
               slice != nullptr ? "" : " (no spell data: no record)",
               entity.spell.vitalsRow != nullptr ? (", health " + std::to_string(static_cast<int>(health))).c_str() : "",
               body.attached != 0 ? (", attached to " + std::string(m_world.find(body.attached)->name)).c_str() : "");
    return &entity;
}

void Spells::tick() {
    const WorldClock::time_point now = m_world.clock().now();
    while (!m_due.empty() && m_due.begin()->first <= now) {
        const WorldClock::time_point at = m_due.begin()->first;
        const Due due = m_due.begin()->second;
        m_due.erase(m_due.begin());
        ++m_counts.dueTaken;
        const auto live = m_live.find(due.spell);
        Entity* spell = m_world.find(due.spell);
        if (live == m_live.end() || spell == nullptr || spell->kind != EntityKind::spell) {
            continue; // The spell went away (a cancel): its entries are old.
        }
        switch (due.kind) {
        case DueKind::end:
            if ((live->second.role == SpellRole::summon || live->second.role == SpellRole::trap) && spell->spell.slice != nullptr) {
                // The end of the life of a summon or a trap: its end chain (the expire event of the AutoSpell, the trigger of the
                // Lifetime entity), then the removal at the destroy of that chain.
                const std::string entity = live->second.role == SpellRole::summon && spell->spell.slice->autoCast
                                               ? spell->spell.slice->autoCast->expireTarget
                                               : (life_trigger(*spell->spell.slice) ? life_trigger(*spell->spell.slice)->second : std::string());
                start_end(*spell, entity, SpellEnd::time);
            } else {
                end(due.spell, SpellEnd::time);
            }
            break;
        case DueKind::remove: {
            const auto cause = m_endCauses.find(due.spell);
            end(due.spell, cause != m_endCauses.end() ? cause->second : SpellEnd::time);
            break;
        }
        case DueKind::phase: {
            const auto index = static_cast<std::size_t>(due.phase);
            if (index >= live->second.phases.size() || live->second.done[index]) {
                break;
            }
            live->second.done[index] = true;
            ++m_counts.phases;
            // The hits of the phase at the place of the spell: one time, while the spell is in the world.
            const std::uint64_t cast = live->second.cast;
            const std::uint32_t phase = live->second.phases[index].phase;
            const state::WorldPosition center = spell->position;
            m_world.abilities().spell_phase(cast, phase, center);
            break;
        }
        case DueKind::shot:
            summon_shot(*spell, live->second);
            break;
        case DueKind::detect:
            trap_step(*spell, live->second);
            break;
        case DueKind::aura:
            aura_step(*spell, live->second);
            break;
        case DueKind::areaStep:
            area_step(*spell, live->second);
            break;
        case DueKind::strikeStep:
            strike_step(*spell, live->second, at);
            break;
        }
    }
}

void Spells::trigger(Entity& spell, std::string_view entity) {
    if (entity.empty()) {
        return;
    }
    spell.spell.triggers.push_back(SpellTrigger{std::string(entity), m_world.clock().now()});
    m_world.touch(spell.id, ComponentKey::delayedEvents);
}

void Spells::start_end(Entity& spell, std::string_view entity, SpellEnd cause) {
    if (spell.spell.ending) {
        return;
    }
    spell.spell.ending = true;
    const WorldClock::time_point now = m_world.clock().now();
    trigger(spell, entity);
    const std::chrono::milliseconds chain =
        spell.spell.slice != nullptr ? milliseconds_of(spell.spell.slice->destroy_after_trigger(entity).value_or(0.0F)) : std::chrono::milliseconds(0);
    spell.spell.ends = now + chain;
    m_endCauses[spell.id] = cause;
    schedule(spell.id, spell.spell.ends, DueKind::remove);
    log::write("world: spell: %s, entity %016llx: its end chain starts (%s): the trigger of %s, the removal in %lld ms",
               spell.spell.spellId.c_str(),
               static_cast<unsigned long long>(spell.id),
               spell_end_text(cause),
               entity.empty() ? "no entity" : std::string(entity).c_str(),
               static_cast<long long>(chain.count()));
}

void Spells::summon_shot(Entity& spell, Live& live) {
    const gamedata::SpellSlice* slice = spell.spell.slice;
    if (spell.spell.ending || slice == nullptr || !slice->autoCast) {
        return;
    }
    const WorldClock::time_point now = m_world.clock().now();
    const gamedata::SpellAutoCast& cast = *slice->autoCast;
    const gamedata::ProjectileData* projectiles = m_world.options().projectileData;
    const gamedata::ShotRule* rule = projectiles != nullptr && projectiles->loaded() ? projectiles->ability_shot(cast.spell) : nullptr;
    const gamedata::SpellVolume* volume = slice->volume();
    if (rule == nullptr || volume == nullptr || m_world.find(spell.spell.caster) == nullptr) {
        return; // No projectile data for the spell of the AutoSpell, or no target area: no shot.
    }
    // The target: the nearest creature of the detection volume (3D, the sphere of the slice) that lives, that is not on its way
    // back, and that the summon sees (a decision of an agent: the data has no order of the targets; research U8).
    const state::WorldPosition origin{spell.position.x, spell.position.y, spell.position.z + spell_rules::kShotHeight};
    Entity* target = nullptr;
    float best = 0.0F;
    m_world.for_each_near(spell.position, volume->radius + creature_reach(m_world), [&](Entity& entity) {
        ++m_counts.looked;
        if (entity.kind != EntityKind::creature || entity.creature.dead || entity.creature.immune()) {
            return;
        }
        const WorldCapsule capsule = capsule_of(entity);
        const float gap = capsule_gap(spell.position, capsule);
        if (gap > volume->radius || (target != nullptr && gap >= best) || !m_world.navigation().sees(origin, capsule_center(capsule))) {
            return;
        }
        target = &entity;
        best = gap;
    });
    if (target == nullptr) {
        if (now + spell_rules::kSummonLook < live.lifeEnds) {
            schedule(spell.id, now + spell_rules::kSummonLook, DueKind::shot);
        }
        return;
    }
    const state::WorldPosition aim = capsule_center(capsule_of(*target));
    const float length = distance3(origin, aim);
    SpellShot shot;
    shot.caster = spell.spell.caster;
    shot.source = spell.id;
    shot.rule = rule;
    shot.origin = origin;
    shot.direction = length > 1.0e-4F ? std::array<float, 3>{(aim.x - origin.x) / length, (aim.y - origin.y) / length, (aim.z - origin.z) / length}
                                      : std::array<float, 3>{0.0F, 1.0F, 0.0F};
    shot.weapon = live.weapon;
    shot.conversion = live.conversion;
    // A creature that the shot hurts takes the summon as its target (OverrideTargetForAiThreat of the spell row of the Pylon).
    shot.threat = spell.id;
    const ShotResult result = m_world.projectiles().launch_spell(shot);
    if (result.verdict == ShotVerdict::accepted) {
        ++m_counts.shots;
        // The cast event of the AutoSpell (m_castEvent: the entity FireTrigger, the charge picture of the Pylon).
        trigger(spell, cast.castTarget);
    }
    if (now + milliseconds_of(cast.cooldown) < live.lifeEnds) {
        schedule(spell.id, now + milliseconds_of(std::max(cast.cooldown, 0.001F)), DueKind::shot);
    }
}

void Spells::trap_step(Entity& spell, Live& live) {
    const gamedata::SpellSlice* slice = spell.spell.slice;
    if (spell.spell.ending || slice == nullptr) {
        return;
    }
    const WorldClock::time_point now = m_world.clock().now();
    const gamedata::SpellVolume* volume = slice->volume();
    if (volume == nullptr) {
        return;
    }
    // The first creature whose capsule is in the detection volume (a sphere at the place of the trap).
    const float reach = creature_reach(m_world);
    bool found = false;
    m_world.for_each_near(spell.position, volume->radius + reach, [&](Entity& entity) {
        ++m_counts.looked;
        if (!found && entity.kind == EntityKind::creature && !entity.creature.dead && !entity.creature.immune()
            && capsule_gap(spell.position, capsule_of(entity)) <= volume->radius) {
            found = true;
        }
    });
    if (!found) {
        if (now + spell_rules::kDetectStep < live.lifeEnds) {
            schedule(spell.id, now + spell_rules::kDetectStep, DueKind::detect);
        }
        return;
    }
    ++m_counts.triggers;
    // The hit: each creature in the sphere of the offense of the slice (TrapActivate: the row Musket_Trap, 1.1 m; checkObstructions),
    // through Combat::strike with the weapon of the cast, and the effects of the plan (the root).
    Entity* caster = m_world.find(spell.spell.caster);
    const gamedata::SpellOffense* offense = slice->offense();
    const gamedata::CombatTables* tables = m_world.combat().tables();
    const std::optional<std::uint32_t> rowIndex = offense != nullptr && tables != nullptr ? tables->row_index(offense->row) : std::nullopt;
    std::vector<EntityId> hit;
    if (offense != nullptr) {
        const state::WorldPosition eye{spell.position.x, spell.position.y, spell.position.z + 0.5F};
        m_world.for_each_near(spell.position, offense->radius + reach, [&](Entity& entity) {
            ++m_counts.looked;
            if (entity.kind == EntityKind::creature && !entity.creature.dead && !entity.creature.immune()
                && capsule_gap(spell.position, capsule_of(entity)) <= offense->radius
                && m_world.navigation().sees(eye, capsule_center(capsule_of(entity)))) {
                hit.push_back(entity.id);
            }
        });
    }
    log::write("world: spell: the trap %016llx of %s triggers at %.2f %.2f %.2f: %zu creature(s) in its offense (%s)",
               static_cast<unsigned long long>(spell.id),
               caster != nullptr ? caster->name.c_str() : "a caster that left",
               static_cast<double>(spell.position.x),
               static_cast<double>(spell.position.y),
               static_cast<double>(spell.position.z),
               hit.size(),
               offense != nullptr ? offense->row.c_str() : "no offense");
    const state::WorldPosition source = spell.position;
    const EntityId spellId = spell.id;
    for (const EntityId id : hit) {
        Entity* creature = m_world.find(id);
        if (creature == nullptr || caster == nullptr || !rowIndex) {
            continue;
        }
        const HitResult result = m_world.combat().strike(*caster, *creature, live.weapon, *rowIndex, 1.0F, "the trap", &live.conversion, &source);
        if (result.verdict == HitVerdict::accepted) {
            ++m_counts.trapHits;
        }
        if (Entity* still = m_world.find(id)) {
            for (const std::string& status : live.effects) {
                m_world.abilities().apply_status(*still, status, caster->id);
            }
        }
    }
    // The detection events of the slice (the trigger timeline, the dissolve), then the removal at the destroy of the timeline.
    Entity* again = m_world.find(spellId);
    if (again == nullptr) {
        return;
    }
    std::string chain;
    for (const gamedata::SpellDetectionEvent& event : slice->detectionEvents) {
        if (event.type == gamedata::spell_event::kTrigger) {
            if (chain.empty() && slice->destroy_after_trigger(event.target)) {
                chain = event.target;
            } else {
                trigger(*again, event.target);
            }
        }
    }
    start_end(*again, chain, SpellEnd::triggered);
}

void Spells::aura_step(Entity& spell, Live& live) {
    const WorldClock::time_point now = m_world.clock().now();
    if (spell.spell.attached != 0) {
        const Entity* target = m_world.find(spell.spell.attached);
        if (target == nullptr) {
            end(spell.id, SpellEnd::targetLeft);
            return;
        }
        if (target->life.dead || (target->kind == EntityKind::creature && target->creature.dead)) {
            end(spell.id, SpellEnd::targetDied);
            return;
        }
        // The aura follows its attachment (the client moves its picture with the Spell state; the server keeps the place for the
        // interest and the area).
        m_world.place_spell(spell, target->position);
    }
    const Entity* caster = m_world.find(spell.spell.caster);
    std::set<EntityId> inside;
    if (caster != nullptr && live.radius > 0.0F) {
        // The sight from the center of the area to the chest of the friend (code review of stage 3.3b-S6, finding S6-R4; the same heights
        // as the sight test of a direct heal, 1.5 m: a decision of an agent): a wall between them keeps the friend out, and a friend that
        // goes behind a wall leaves the area.
        state::WorldPosition center = spell.position;
        center.z += 1.5F;
        m_world.for_each_near(spell.position, live.radius, [&](Entity& entity) {
            ++m_counts.looked;
            if (entity.kind == EntityKind::player && !entity.life.dead && distance3(entity.position, spell.position) <= live.radius
                && std::fabs(entity.position.z - spell.position.z) <= spell_rules::kHeight && m_world.friendly(*caster, entity)) {
                state::WorldPosition chest = entity.position;
                chest.z += 1.5F;
                if (m_world.navigation().sees(center, chest)) {
                    inside.insert(entity.id);
                }
            }
        });
    }
    // A friend that comes in gets the statuses of the spell row (the status system; the time of each row: UseStatusEffectDuration).
    for (const EntityId id : inside) {
        if (live.members.contains(id)) {
            continue;
        }
        Entity* friendly = m_world.find(id);
        if (friendly != nullptr && give_member(spell, live, *friendly)) {
            live.members.emplace(id, friendly->characterId);
            ++m_counts.auraGiven;
        }
    }
    if (live.clearOnExit) {
        clear_members(spell, live, inside);
    }
    renew_members(spell, live);
    if (now + spell_rules::kAuraStep < spell.spell.ends) {
        schedule(spell.id, now + spell_rules::kAuraStep, DueKind::aura);
    }
}

bool Spells::give_member(const Entity& spell, const Live& live, Entity& friendly) {
    const StatusSource source{StatusSourceKind::spell, spell.spell.spellId, 0, spell.id};
    bool given = false;
    for (const std::string& status : live.statuses) {
        AuraRow& row = m_auraRows[friendly.characterId][lower(status)];
        if (!row.auras.empty()) {
            // A different aura gives the row already (finding F4): one status of the row, no new application, no new tick.
            row.auras.insert(spell.id);
            given = true;
            continue;
        }
        StatusApply how;
        how.source = source;
        how.heal = live.heal;
        // A status with no duration (BaseDuration -1: the fortify in the well of Unyielding, stage 3.3b-M2) stays while the friend is in
        // the area: the area takes it at the exit and at its end.
        const gamedata::AbilityData* abilities = m_world.options().abilityData;
        const gamedata::StatusRow* data = abilities != nullptr ? abilities->status(status) : nullptr;
        how.permanent = data != nullptr && data->duration <= 0.0F;
        const StatusVerdict verdict = m_world.statuses().apply(friendly, status, how);
        if (verdict == StatusVerdict::applied || verdict == StatusVerdict::renewed) {
            row.auras.insert(spell.id);
            row.owner = spell.id;
            given = true;
        } else if (row.auras.empty()) {
            m_auraRows[friendly.characterId].erase(lower(status));
        }
    }
    if (const auto rows = m_auraRows.find(friendly.characterId); rows != m_auraRows.end() && rows->second.empty()) {
        m_auraRows.erase(rows);
    }
    return given;
}

void Spells::remove_member(const Entity& spell, const Live& live, EntityId member, const std::string& characterId, bool takeStatus) {
    const StatusSource source{StatusSourceKind::spell, spell.spell.spellId, 0, spell.id};
    const auto rows = m_auraRows.find(characterId);
    if (rows == m_auraRows.end()) {
        return;
    }
    for (const std::string& status : live.statuses) {
        const auto found = rows->second.find(lower(status));
        if (found == rows->second.end() || !found->second.auras.contains(spell.id)) {
            continue;
        }
        AuraRow& row = found->second;
        row.auras.erase(spell.id);
        if (row.owner != spell.id) {
            continue; // The status has the source of a different aura that gives it still.
        }
        if (row.auras.empty()) {
            // The last aura of the row: the status ends (ClearOnExit, ClearOnEnd), also for a character whose entity left the
            // world (finding F6: the status of a character stays with its character id). With no such column the status keeps its
            // time (finding M2-R3), and only the row goes.
            if (takeStatus) {
                (void)m_world.statuses().remove_row_of(characterId, status, source, StatusEnd::source);
            }
            rows->second.erase(found);
            continue;
        }
        // A different aura gives the row still (finding F4): the status stays, with the source of that aura.
        row.owner = *row.auras.begin();
        const Entity* owner = m_world.find(row.owner);
        if (owner != nullptr) {
            // The heal source of that aura too (code review of stage 3.3b-S6, finding S6-R5): its healer gets the notices, and its
            // base heals.
            const auto ownerLive = m_live.find(owner->id);
            (void)m_world.statuses().change_source(characterId, status, source,
                                                   StatusSource{StatusSourceKind::spell, owner->spell.spellId, 0, owner->id},
                                                   ownerLive != m_live.end() ? &ownerLive->second.heal : nullptr);
        }
    }
    if (rows->second.empty()) {
        m_auraRows.erase(rows);
    }
    (void)member;
}

void Spells::renew_members(const Entity& spell, const Live& live) {
    const StatusSource source{StatusSourceKind::spell, spell.spell.spellId, 0, spell.id};
    for (const auto& [member, characterId] : live.members) {
        Entity* friendly = m_world.find(member);
        const auto rows = m_auraRows.find(characterId);
        if (friendly == nullptr || rows == m_auraRows.end()) {
            continue;
        }
        for (const std::string& status : live.statuses) {
            const auto found = rows->second.find(lower(status));
            if (found == rows->second.end() || found->second.owner != spell.id || m_world.statuses().has(*friendly, status)) {
                continue;
            }
            // The row of the status data is shorter than the aura (the Beacon with Radiance's Blessing: 15 s, its rows 10 s): a
            // friend in the aura gets the row again when it ended, one application (no new stack while it has the row).
            StatusApply how;
            how.source = source;
            how.heal = live.heal;
            if (m_world.statuses().apply(*friendly, status, how) == StatusVerdict::applied) {
                ++m_counts.auraRenewed;
            }
        }
    }
}

void Spells::clear_members(const Entity& spell, Live& live, const std::set<EntityId>& keep) {
    for (auto member = live.members.begin(); member != live.members.end();) {
        if (keep.contains(member->first)) {
            ++member;
            continue;
        }
        remove_member(spell, live, member->first, member->second);
        ++m_counts.auraTaken;
        member = live.members.erase(member);
    }
}

std::vector<EntityId> Spells::members(EntityId spell) const {
    const auto live = m_live.find(spell);
    std::vector<EntityId> members;
    if (live != m_live.end()) {
        for (const auto& [member, characterId] : live->second.members) {
            members.push_back(member);
        }
    }
    return members;
}

float Spells::creature_hit(Entity& creature, const gamedata::CreatureContact& contact, Entity& spell) {
    if (spell.kind != EntityKind::spell || spell.spell.vitalsRow == nullptr || spell.spell.ending) {
        return 0.0F;
    }
    const CreatureDamage damage = m_world.combat().creature_damage(creature, contact, spell);
    const float before = spell.vitals.health;
    spell.vitals.health = std::max(spell.vitals.health - damage.dealt, 0.0F);
    m_world.touch(spell.id, ComponentKey::vitals);
    ++m_counts.summonHits;
    log::write("world: hit: creature %016llx (%s) hurts the spell %s %016llx with %s: %.1f damage (armor %.1f, mitigation %.3f), health "
               "%.1f to %.1f",
               static_cast<unsigned long long>(creature.id),
               creature.creature.kind != nullptr ? creature.creature.kind->id.c_str() : "?",
               spell.spell.spellId.c_str(),
               static_cast<unsigned long long>(spell.id),
               contact.row.c_str(),
               static_cast<double>(damage.dealt),
               static_cast<double>(damage.armor),
               static_cast<double>(damage.mitigation),
               static_cast<double>(before),
               static_cast<double>(spell.vitals.health));
    if (spell.vitals.health <= 0.0F) {
        // The death event of its Vitals component (m_onDeathEvent: the entity DestroyTrigger of the Pylon).
        start_end(spell, spell.spell.slice != nullptr && spell.spell.slice->vitals ? spell.spell.slice->vitals->deathTarget : std::string(),
                  SpellEnd::destroyed);
    }
    return before - spell.vitals.health;
}

bool Spells::hurt(EntityId id, float amount) {
    Entity* spell = m_world.find(id);
    if (spell == nullptr || spell->kind != EntityKind::spell || spell->spell.vitalsRow == nullptr || !std::isfinite(amount) || amount < 0.0F) {
        return false;
    }
    if (spell->spell.ending) {
        return true;
    }
    spell->vitals.health = std::max(spell->vitals.health - amount, 0.0F);
    m_world.touch(spell->id, ComponentKey::vitals);
    if (spell->vitals.health <= 0.0F) {
        start_end(*spell, spell->spell.slice != nullptr && spell->spell.slice->vitals ? spell->spell.slice->vitals->deathTarget : std::string(),
                  SpellEnd::destroyed);
    }
    return true;
}

bool Spells::end(EntityId id, SpellEnd cause) {
    const auto live = m_live.find(id);
    Entity* spell = m_world.find(id);
    if (live == m_live.end() || spell == nullptr || spell->kind != EntityKind::spell) {
        return false;
    }
    const auto cancelled = static_cast<std::uint64_t>(std::count(live->second.done.begin(), live->second.done.end(), false));
    m_counts.phasesCancelled += cancelled;
    ++m_counts.ended[static_cast<std::size_t>(cause)];
    // An aura takes its statuses back from each friend at its end (ClearOnEnd of the spell row).
    if (live->second.role != SpellRole::statusArea && live->second.clearOnEnd && !live->second.statuses.empty()) {
        clear_members(*spell, live->second, {});
    }
    // A status area of a creature (stage 3.4f-C): each character in it loses its statuses (m_removeOnExit of the component, ClearOnEnd
    // of the spell row: the end of the area is the exit of each character).
    if (live->second.role == SpellRole::statusArea) {
        const bool take = live->second.clearOnEnd || (live->second.areaStatus != nullptr && live->second.areaStatus->removeOnExit);
        std::vector<EntityId> members;
        for (const auto& [member, entry] : live->second.areaMembers) {
            members.push_back(member);
        }
        for (const EntityId member : members) {
            leave_area(*spell, live->second, member, take);
        }
    }
    const WorldClock::time_point now = m_world.clock().now();
    log::write("world: spell: %s, entity %016llx, ends after %lld ms: %s%s",
               spell->spell.spellId.c_str(),
               static_cast<unsigned long long>(id),
               static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(now - spell->spell.started).count()),
               spell_end_text(cause),
               cancelled != 0 ? (", " + std::to_string(cancelled) + " phase(s) did not come").c_str() : "");
    m_world.remove_spell(id);
    return true;
}

std::size_t Spells::end_caster(EntityId caster, SpellEnd cause) {
    const auto found = m_byCaster.find(caster);
    if (found == m_byCaster.end()) {
        return 0;
    }
    const std::vector<EntityId> spells = found->second;
    std::size_t ended = 0;
    for (const EntityId spell : spells) {
        ended += end(spell, cause) ? 1 : 0;
    }
    return ended;
}

bool Spells::area_usable(const gamedata::AreaCast& cast) const {
    // Spell data of format 4 or later (code review of stage 3.4f-C, finding AR-R4): an older file has no shape of a volume and no
    // application delays of an area status, and its defaults (a sphere, no delay) would make an area with no initial delay.
    const gamedata::SpellSlice* slice = has_data() && m_data->tables().format >= 4 ? m_data->slice_of(cast.spell) : nullptr;
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    const gamedata::SpellRow* row = abilities != nullptr ? abilities->spell(cast.spell) : nullptr;
    if (slice == nullptr || row == nullptr) {
        return false;
    }
    if (cast.shape == gamedata::AreaShape::status) {
        const gamedata::SpellAreaStatus* area = enemy_area(*slice);
        const gamedata::SpellVolume* volume = area != nullptr ? slice->volume(area->detection) : nullptr;
        return slice->positionSlot != 0 && volume != nullptr && volume->activeAfter >= 0.0F
               && ((volume->shape == gamedata::VolumeShape::cylinder && volume->shapeRadius > 0.0F && volume->height > 0.0F)
                   || (volume->shape == gamedata::VolumeShape::sphere && volume->radius > 0.0F));
    }
    return slice->spellSlot != 0 && strike_volume(*slice) != nullptr && row->radius > 0.0F;
}

float Spells::cast_distance(const gamedata::AreaCast& cast) const {
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    const gamedata::SpellRow* row = abilities != nullptr ? abilities->spell(cast.spell) : nullptr;
    return row != nullptr ? row->castDistance : 0.0F;
}

Entity* Spells::make_area(const AreaRequest& request) {
    Entity* creature = m_world.find(request.creature);
    if (creature == nullptr || creature->kind != EntityKind::creature || creature->creature.dead || request.cast == nullptr
        || !valid_position(request.position) || !area_usable(*request.cast)) {
        return nullptr;
    }
    const gamedata::AreaCast& cast = *request.cast;
    const gamedata::SpellSlice* slice = m_data->slice_of(cast.spell);
    const gamedata::SpellRow* row = m_world.options().abilityData->spell(cast.spell);
    const WorldClock::time_point now = m_world.clock().now();
    SpellBody body;
    body.spellId = row->id;
    // The key of the row on the wire (field 0 of the Spell state of a strike): the CRC-32 of the lower-case SpellID.
    body.key = gamedata::progression_id(body.spellId);
    body.slice = slice;
    body.caster = creature->id;
    body.casterActor = creature->actor;
    body.started = now;
    // The rotation of the request is the rotation of the caster (client code 0x676AAF0; stage 3.4f-D: the shockwave of the Evil Knight).
    body.yaw = creature->creature.yaw;
    body.role = cast.shape == gamedata::AreaShape::status ? SpellRole::statusArea : SpellRole::strike;
    // EndOnCasterDeath of the spell row (game data): the lightning strike has it, the mushroom has not.
    body.endsOnCasterDeath = row->cells.number("EndOnCasterDeath") != 0.0F;
    // The life: the destroy event of the slice (the mushroom: 26 s), else the Duration of the spell row (the strike: 5 s).
    const std::optional<float> end = slice->end_delay();
    body.ends = now + (end ? milliseconds_of(*end) : milliseconds_of(row->duration / 1000.0F));
    Live live;
    live.role = body.role;
    live.lifeEnds = body.ends;
    live.areaCast = &cast;
    live.clearOnExit = row->cells.number("ClearOnExit") != 0.0F;
    live.clearOnEnd = row->cells.number("ClearOnEnd") != 0.0F;
    if (body.role == SpellRole::statusArea) {
        live.areaStatus = enemy_area(*slice);
        live.volume = slice->volume(live.areaStatus->detection);
        live.statuses = live.areaStatus->statuses;
    } else {
        live.volume = strike_volume(*slice);
        live.strikeRadius = row->radius;
    }
    live.activeFrom = now + at_time(live.volume->activeAfter);
    if (live.volume->inactiveAfter >= 0.0F) {
        live.activeTo = now + at_time(live.volume->inactiveAfter);
    }
    // The damage source of the statuses of the area: the creature and its core damage at the cast (the damage of its level and kind).
    const gamedata::CreatureData* creatures = m_world.creatures().data();
    live.damage = DamageSource{creature->id,
                               creatures != nullptr && creature->creature.kind != nullptr
                                   ? creatures->damage(*creature->creature.kind, creature->creature.level)
                                   : 0.0F,
                               creature->creature.level};
    const std::string casterName = creature->creature.kind != nullptr ? creature->creature.kind->id : std::string("?");
    const std::uint32_t level = creature->creature.level;
    Entity& entity = m_world.make_spell(body, request.position);
    entity.progression.level = level;
    const EntityId id = entity.id;
    m_byCaster[body.caster].push_back(id);
    schedule(id, body.ends, DueKind::end);
    schedule(id, std::max(live.activeFrom, now), body.role == SpellRole::statusArea ? DueKind::areaStep : DueKind::strikeStep);
    m_live[id] = std::move(live);
    ++m_counts.made;
    ++m_counts.areasMade;
    m_counts.largest = std::max(m_counts.largest, m_live.size());
    log::write("world: spell: %s (%s) of creature %016llx (%s) at %.2f %.2f %.2f, entity %016llx (actor %s): active from %.2f s, the end in "
               "%lld ms%s",
               body.spellId.c_str(), spell_role_text(body.role), static_cast<unsigned long long>(body.caster), casterName.c_str(),
               static_cast<double>(request.position.x), static_cast<double>(request.position.y), static_cast<double>(request.position.z),
               static_cast<unsigned long long>(id), actor_text(entity.actor).c_str(), static_cast<double>(entity.spell.slice != nullptr
                                                                                                          ? m_live[id].volume->activeAfter
                                                                                                          : 0.0F),
               static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(entity.spell.ends - now).count()),
               entity.spell.endsOnCasterDeath ? ", ends at the death of its caster" : "");
    return &entity;
}

bool Spells::in_area(const Entity& spell, const Live& live, const Entity& character, std::array<float, 3> back) const {
    if (live.volume == nullptr) {
        return false;
    }
    const gamedata::CreatureData* creatures = m_world.creatures().data();
    WorldCapsule capsule =
        capsule_of(character, creatures != nullptr && creatures->player_capsule() ? &*creatures->player_capsule() : nullptr);
    for (state::WorldPosition* end : {&capsule.a, &capsule.b}) {
        end->x -= back[0];
        end->y -= back[1];
        end->z -= back[2];
    }
    const state::WorldPosition center{spell.position.x, spell.position.y, spell.position.z + live.volume->offset};
    if (live.role == SpellRole::statusArea && live.volume->shape == gamedata::VolumeShape::cylinder) {
        // An upright cylinder (the QueryShapeCylinder of the volume: the mushroom 1.5 m, 4 m high, its middle 0.5 m below the root): the
        // capsule of the character (upright) touches it in the plane and in the height.
        const float half = live.volume->height * 0.5F;
        const float low = std::min(capsule.a.z, capsule.b.z) - capsule.radius;
        const float high = std::max(capsule.a.z, capsule.b.z) + capsule.radius;
        return std::hypot(capsule.a.x - center.x, capsule.a.y - center.y) <= live.volume->shapeRadius + capsule.radius
               && high >= center.z - half && low <= center.z + half;
    }
    // A sphere: the sphere of the volume, or the Radius of the spell row for a volume with no shape (a strike).
    const float radius = live.role == SpellRole::strike ? live.strikeRadius : live.volume->radius;
    return capsule_gap(center, capsule) <= radius;
}

void Spells::area_step(Entity& spell, Live& live) {
    const WorldClock::time_point now = m_world.clock().now();
    ++m_counts.areaSteps;
    if (live.areaStatus == nullptr || live.volume == nullptr) {
        return;
    }
    if (live.activeTo && now >= *live.activeTo) {
        // The volume is not active any more: each character in it leaves it.
        std::vector<EntityId> members;
        for (const auto& [member, entry] : live.areaMembers) {
            members.push_back(member);
        }
        for (const EntityId member : members) {
            leave_area(spell, live, member, live.areaStatus->removeOnExit);
        }
        return;
    }
    // The characters in the volume (the position index near the area; a character that lives).
    const float reach = std::max(live.volume->shapeRadius, live.volume->radius) + live.volume->height + 3.0F;
    std::set<EntityId> inside;
    m_world.for_each_near(spell.position, reach, [&](Entity& entity) {
        ++m_counts.looked;
        if (entity.kind == EntityKind::player && !entity.life.dead && in_area(spell, live, entity)) {
            inside.insert(entity.id);
        }
    });
    const StatusSource source{StatusSourceKind::spell, spell.spell.spellId, 0, spell.id};
    WorldClock::time_point next = now + spell_rules::kAreaStep;
    for (const EntityId id : inside) {
        Entity* character = m_world.find(id);
        if (character == nullptr) {
            continue;
        }
        auto member = live.areaMembers.find(id);
        if (member == live.areaMembers.end()) {
            // An entry: the first application m_initialDelaySeconds later (client code 0x3FE5B40).
            member = live.areaMembers.emplace(id, Live::AreaMember{character->characterId, now + at_time(live.areaStatus->initialDelay), false})
                         .first;
            m_areasOf[character->characterId].insert(spell.id);
        }
        if (now >= member->second.next) {
            // ApplyStatusEffects (client code 0x3FE6500): each status of the component, then again m_reapplicationDelaySeconds later.
            for (const std::string& status : live.statuses) {
                StatusApply how;
                how.source = source;
                how.damage = live.damage;
                (void)m_world.statuses().apply(*character, status, how);
            }
            member->second.applied = true;
            ++m_counts.areaApplications;
            member->second.next = live.areaStatus->reapplyDelay > 0.0F ? member->second.next + at_time(live.areaStatus->reapplyDelay)
                                                                       : live.lifeEnds + std::chrono::hours(1);
            if (member->second.next <= now) {
                member->second.next = now + at_time(live.areaStatus->reapplyDelay);
            }
        }
        next = std::min(next, member->second.next);
    }
    // An exit: the statuses of the area go (m_removeOnExit; client code 0x3FF2830 cancels the next application too).
    std::vector<EntityId> left;
    for (const auto& [member, entry] : live.areaMembers) {
        if (!inside.contains(member)) {
            left.push_back(member);
        }
    }
    for (const EntityId member : left) {
        leave_area(spell, live, member, live.areaStatus->removeOnExit);
    }
    next = std::max(next, now + std::chrono::milliseconds(1));
    if (next < live.lifeEnds && (!live.activeTo || next <= *live.activeTo)) {
        schedule(spell.id, next, DueKind::areaStep);
    } else if (live.activeTo && *live.activeTo < live.lifeEnds) {
        schedule(spell.id, *live.activeTo, DueKind::areaStep);
    }
}

bool Spells::holds(EntityId area, const Entity& character) {
    const auto live = m_live.find(area);
    const Entity* spell = m_world.find(area);
    if (live == m_live.end() || spell == nullptr || live->second.role != SpellRole::statusArea || live->second.areaStatus == nullptr
        || !live->second.areaMembers.contains(character.id)) {
        return true; // Not a member of a status area (its exit or its end took the statuses already).
    }
    const WorldClock::time_point now = m_world.clock().now();
    if ((!live->second.activeTo || now < *live->second.activeTo) && !character.life.dead && in_area(*spell, live->second, character)) {
        return true;
    }
    ++m_counts.exitsBeforeTick;
    leave_area(*spell, live->second, character.id, live->second.areaStatus->removeOnExit);
    return false;
}

void Spells::leave_area(const Entity& spell, Live& live, EntityId member, bool takeStatus) {
    const auto found = live.areaMembers.find(member);
    if (found == live.areaMembers.end()) {
        return;
    }
    const std::string characterId = found->second.characterId;
    const bool applied = found->second.applied;
    live.areaMembers.erase(found);
    if (const auto areas = m_areasOf.find(characterId); areas != m_areasOf.end()) {
        areas->second.erase(spell.id);
        if (areas->second.empty()) {
            m_areasOf.erase(areas);
        }
    }
    if (!takeStatus || !applied) {
        return;
    }
    // The status of a row goes when this area gave it last (the source of the status): a second area that gives the row still gives it
    // again at its next application.
    const StatusSource source{StatusSourceKind::spell, spell.spell.spellId, 0, spell.id};
    unsigned ended = 0;
    for (const std::string& status : live.statuses) {
        ended += m_world.statuses().remove_row_of(characterId, status, source, StatusEnd::source);
    }
    ++m_counts.areaExits;
    (void)ended;
}

void Spells::strike_step(Entity& spell, Live& live, WorldClock::time_point due) {
    const WorldClock::time_point now = m_world.clock().now();
    ++m_counts.areaSteps;
    // The sample of this step is at its due time, not at the time of the world tick that processes it (code review of stage 3.4f-C, finding
    // AR-R3): a world step that crosses the whole window still makes the sample of the window.
    const WorldClock::time_point sample = std::max(due, live.activeFrom);
    if (live.areaCast == nullptr || live.volume == nullptr || spell.spell.ending || (live.activeTo && sample >= *live.activeTo)) {
        return;
    }
    if (now - sample > spell_rules::kStrikeCatchUp) {
        log::write("world: spell: the strike %016llx lost its sample of %.3f s: the world step came %lld ms late",
                   static_cast<unsigned long long>(spell.id), std::chrono::duration<double>(sample - spell.spell.started).count(),
                   static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(now - sample).count()));
    } else {
        const bool catchUp = live.activeTo && now >= *live.activeTo;
        m_counts.strikeCatchUps += catchUp ? 1U : 0U;
        strike_sample(spell, live, sample, now);
    }
    const WorldClock::time_point next = sample + spell_rules::kStrikeStep;
    if (live.activeTo && next < *live.activeTo && next < live.lifeEnds) {
        schedule(spell.id, std::max(next, now), DueKind::strikeStep);
    }
}

void Spells::strike_sample(Entity& spell, Live& live, WorldClock::time_point sample, WorldClock::time_point now) {
    Entity* creature = m_world.find(spell.spell.caster);
    if (creature == nullptr || creature->kind != EntityKind::creature) {
        return; // The caster left the world (the leave ends the spell before this step).
    }
    const float late = std::chrono::duration<float>(now - sample).count();
    const float reach = live.strikeRadius + projectile_rules::kDefaultHeight + 1.0F + late * 10.0F;
    // A wall between the center and the character stops the hit only for a spell row with CheckObstructions (the cell of the game data; the
    // two rows of this stage do not have it; the server code that reads the bit, row +0x0C bit 0x11, is not in the client: STATIC-CAST.md).
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    const gamedata::SpellRow* row = abilities != nullptr ? abilities->spell(spell.spell.spellId) : nullptr;
    const bool walls = row != nullptr && row->cells.number("CheckObstructions") != 0.0F;
    const state::WorldPosition eye{spell.position.x, spell.position.y, spell.position.z + 0.5F};
    const gamedata::CreatureData* creatures = m_world.creatures().data();
    std::vector<EntityId> targets;
    m_world.for_each_near(spell.position, reach, [&](Entity& entity) {
        ++m_counts.looked;
        if (entity.kind != EntityKind::player || entity.life.dead || live.struck.contains(entity.id)) {
            return;
        }
        // The place of the character at the time of the sample: its velocity back from now (0 for a sample in time).
        std::array<float, 3> back{};
        if (late > 0.0F) {
            const std::array<float, 3> velocity = m_world.velocity_of(entity);
            back = {velocity[0] * late, velocity[1] * late, velocity[2] * late};
        }
        if (!in_area(spell, live, entity, back)) {
            return;
        }
        if (walls) {
            WorldCapsule capsule =
                capsule_of(entity, creatures != nullptr && creatures->player_capsule() ? &*creatures->player_capsule() : nullptr);
            state::WorldPosition middle = capsule_center(capsule);
            middle = {middle.x - back[0], middle.y - back[1], middle.z - back[2]};
            if (!m_world.navigation().sees(eye, middle)) {
                return;
            }
        }
        targets.push_back(entity.id);
    });
    for (const EntityId id : targets) {
        Entity* target = m_world.find(id);
        Entity* source = m_world.find(spell.spell.caster);
        if (target == nullptr || source == nullptr) {
            continue;
        }
        // One hit of the damage row of the spell for each character in the window (the creature rule; the dodge decides it at the time of
        // the sample).
        live.struck.insert(id);
        ++m_counts.strikeHits;
        (void)m_world.health().creature_hit(*source, live.areaCast->damage, *target, sample);
    }
}

std::optional<float> Spells::absorbs(const state::WorldPosition& from, const state::WorldPosition& to, EntityId shooter) const {
    const Entity* source = m_world.find(shooter);
    if (source == nullptr || m_live.empty()) {
        return std::nullopt;
    }
    const float length = distance3(from, to);
    const state::WorldPosition middle{(from.x + to.x) * 0.5F, (from.y + to.y) * 0.5F, (from.z + to.z) * 0.5F};
    std::optional<float> first;
    m_world.for_each_near(middle, length * 0.5F + kShieldReach, [&](Entity& entity) {
        if (entity.kind != EntityKind::spell || entity.spell.role != SpellRole::shield) {
            return;
        }
        const auto live = m_live.find(entity.id);
        const Entity* owner = m_world.find(entity.spell.caster);
        if (live == m_live.end() || owner == nullptr || !m_world.hostile(*owner, *source) || !(live->second.radius > 0.0F)) {
            return;
        }
        // The first point of the segment in the sphere of the shield (its center 1 m above the feet of its owner: CastHeight 1 of the row).
        const state::WorldPosition center{entity.position.x, entity.position.y, entity.position.z + 1.0F};
        const float dx = to.x - from.x;
        const float dy = to.y - from.y;
        const float dz = to.z - from.z;
        const float fx = from.x - center.x;
        const float fy = from.y - center.y;
        const float fz = from.z - center.z;
        const float a = dx * dx + dy * dy + dz * dz;
        const float b = 2.0F * (fx * dx + fy * dy + fz * dz);
        const float c = fx * fx + fy * fy + fz * fz - live->second.radius * live->second.radius;
        float t = 0.0F;
        if (c > 0.0F) {
            const float d = b * b - 4.0F * a * c;
            if (a <= 0.0F || d < 0.0F) {
                return;
            }
            t = (-b - std::sqrt(d)) / (2.0F * a);
            if (t < 0.0F || t > 1.0F) {
                return;
            }
        }
        if (!first || t < *first) {
            first = t;
        }
    });
    return first;
}

void Spells::caster_left(EntityId caster) {
    (void)end_caster(caster, SpellEnd::casterLeft);
}

void Spells::caster_died(EntityId caster) {
    const auto found = m_byCaster.find(caster);
    if (found == m_byCaster.end()) {
        return;
    }
    const std::vector<EntityId> spells = found->second;
    for (const EntityId spell : spells) {
        const Entity* entity = m_world.find(spell);
        if (entity != nullptr && entity->spell.endsOnCasterDeath) {
            end(spell, SpellEnd::casterDied);
        }
    }
}

void Spells::target_left(EntityId target) {
    const auto found = m_byTarget.find(target);
    if (found == m_byTarget.end()) {
        return;
    }
    const std::vector<EntityId> spells = found->second;
    for (const EntityId spell : spells) {
        end(spell, SpellEnd::targetLeft);
    }
}

void Spells::target_died(EntityId target) {
    const auto found = m_byTarget.find(target);
    if (found == m_byTarget.end()) {
        return;
    }
    const std::vector<EntityId> spells = found->second;
    for (const EntityId spell : spells) {
        end(spell, SpellEnd::targetDied);
    }
}

void Spells::member_left(const Entity& character) {
    // The status areas of the creatures (stage 3.4f-C): the leave of the world is the exit of the area (m_removeOnExit).
    if (const auto areas = m_areasOf.find(character.characterId); areas != m_areasOf.end()) {
        const std::set<EntityId> spells = areas->second;
        for (const EntityId area : spells) {
            const auto live = m_live.find(area);
            const Entity* spell = m_world.find(area);
            if (live != m_live.end() && spell != nullptr) {
                leave_area(*spell, live->second, character.id,
                           live->second.areaStatus == nullptr || live->second.areaStatus->removeOnExit);
            }
        }
    }
    const auto rows = m_auraRows.find(character.characterId);
    if (rows == m_auraRows.end()) {
        return;
    }
    std::set<EntityId> auras;
    for (const auto& [status, row] : rows->second) {
        auras.insert(row.auras.begin(), row.auras.end());
    }
    for (const EntityId aura : auras) {
        const auto live = m_live.find(aura);
        const Entity* spell = m_world.find(aura);
        if (live == m_live.end() || spell == nullptr || !live->second.members.contains(character.id)) {
            continue;
        }
        remove_member(*spell, live->second, character.id, character.characterId);
        live->second.members.erase(character.id);
        ++m_counts.auraTaken;
    }
}

void Spells::entity_removed(const Entity& spell) {
    // The members of an aura that ends with no ClearOnEnd (the fortify of Clear Out; code review of stage 3.3b-M2, finding M2-R3): the
    // rows of the aura go, and the statuses keep their time. Without this a later aura saw the row of the ended one and gave nothing.
    if (const auto live = m_live.find(spell.id); live != m_live.end()) {
        for (const auto& [member, characterId] : live->second.members) {
            remove_member(spell, live->second, member, characterId, false);
        }
        live->second.members.clear();
    }
    if (const auto live = m_live.find(spell.id); live != m_live.end()) {
        for (const auto& [member, entry] : live->second.areaMembers) {
            if (const auto areas = m_areasOf.find(entry.characterId); areas != m_areasOf.end()) {
                areas->second.erase(spell.id);
                if (areas->second.empty()) {
                    m_areasOf.erase(areas);
                }
            }
        }
    }
    m_live.erase(spell.id);
    m_endCauses.erase(spell.id);
    if (const auto found = m_byCaster.find(spell.spell.caster); found != m_byCaster.end()) {
        std::erase(found->second, spell.id);
        if (found->second.empty()) {
            m_byCaster.erase(found);
        }
    }
    if (spell.spell.attached != 0) {
        if (const auto found = m_byTarget.find(spell.spell.attached); found != m_byTarget.end()) {
            std::erase(found->second, spell.id);
            if (found->second.empty()) {
                m_byTarget.erase(found);
            }
        }
    }
    if (spell.spell.cast != 0) {
        m_world.abilities().spell_ended(spell.spell.cast, spell.id);
    }
}

} // namespace soulwarden::backend::world

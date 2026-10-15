#include "world/spells.h"

#include <algorithm>
#include <cmath>

#include "core/logging/log.h"
#include "gamedata/progression_tables.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

/**
 * @return A delay of the data on the world clock. A delay of 0 or less is the start of the spell (the game data has -6 s and
 *         0.0001 s in some slices); a positive delay is 1 ms at least (the due list advances: finding F9 of the review of
 *         stage 3.3b-S1).
 */
std::chrono::milliseconds milliseconds_of(float seconds) {
    if (!(seconds > 0.0F)) {
        return std::chrono::milliseconds(0);
    }
    return std::chrono::milliseconds(std::max<long long>(static_cast<long long>(std::lround(seconds * 1000.0F)), 1));
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
    }
    return "unknown";
}

Spells::Spells(World& world, const gamedata::SpellData* data) : m_world(world), m_data(data) {}

std::optional<std::chrono::milliseconds> Spells::activation_time(std::string_view spellId, std::string_view row) const {
    const gamedata::SpellSlice* slice = has_data() ? m_data->slice_of(spellId) : nullptr;
    const std::optional<float> delay = slice != nullptr ? slice->activation_delay(row) : std::nullopt;
    return delay ? std::optional<std::chrono::milliseconds>(milliseconds_of(*delay)) : std::nullopt;
}

Entity* Spells::make(const SpellRequest& request) {
    const Entity* caster = m_world.find(request.caster);
    if (caster == nullptr || caster->kind != EntityKind::player || !valid_position(request.position)) {
        return nullptr;
    }
    const gamedata::SpellSlice* slice = has_data() ? m_data->slice_of(request.spellId) : nullptr;
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
    // The end: the trigger of the entity of the slice with a DestroyGDEComponent (game data: 3.5 s for the vortex). With no
    // data: the time of the last phase of the ability rule.
    const std::optional<float> end = slice != nullptr ? slice->end_delay() : std::nullopt;
    body.ends = now + (end ? milliseconds_of(*end) : request.fallbackEnd);
    body.endsOnCasterDeath = row != nullptr && row->cells.number("EndOnCasterDeath") != 0.0F;
    body.cast = request.cast;
    const std::string casterName = caster->name;
    Entity& entity = m_world.make_spell(body, request.position);
    Live live;
    live.cast = request.cast;
    live.phases = request.phases;
    live.done.assign(request.phases.size(), false);
    m_live[entity.id] = std::move(live);
    m_byCaster[body.caster].push_back(entity.id);
    for (std::size_t index = 0; index < request.phases.size(); ++index) {
        m_due.emplace(now + request.phases[index].at, Due{entity.id, static_cast<std::int32_t>(index)});
    }
    m_due.emplace(body.ends, Due{entity.id, kEnd});
    ++m_counts.made;
    m_counts.largest = std::max(m_counts.largest, m_live.size());
    log::write("world: spell: %s of %s at %.2f %.2f %.2f, entity %016llx (actor %s): %zu phase(s), the end in %lld ms%s",
               body.spellId.c_str(),
               casterName.c_str(),
               static_cast<double>(request.position.x),
               static_cast<double>(request.position.y),
               static_cast<double>(request.position.z),
               static_cast<unsigned long long>(entity.id),
               actor_text(entity.actor).c_str(),
               request.phases.size(),
               static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(body.ends - now).count()),
               slice != nullptr ? "" : " (no spell data: no record)");
    return &entity;
}

void Spells::tick() {
    const WorldClock::time_point now = m_world.clock().now();
    while (!m_due.empty() && m_due.begin()->first <= now) {
        const Due due = m_due.begin()->second;
        m_due.erase(m_due.begin());
        ++m_counts.dueTaken;
        const auto live = m_live.find(due.spell);
        if (live == m_live.end()) {
            continue; // The spell went away (a cancel): its entries are old.
        }
        if (due.phase == kEnd) {
            end(due.spell, SpellEnd::time);
            continue;
        }
        const auto index = static_cast<std::size_t>(due.phase);
        if (index >= live->second.phases.size() || live->second.done[index]) {
            continue;
        }
        live->second.done[index] = true;
        ++m_counts.phases;
        const Entity* spell = m_world.find(due.spell);
        if (spell == nullptr) {
            continue;
        }
        // The hits of the phase at the place of the spell: one time, while the spell is in the world.
        const std::uint64_t cast = live->second.cast;
        const std::uint32_t phase = live->second.phases[index].phase;
        const state::WorldPosition center = spell->position;
        m_world.abilities().spell_phase(cast, phase, center);
    }
}

bool Spells::end(EntityId id, SpellEnd cause) {
    const auto live = m_live.find(id);
    const Entity* spell = m_world.find(id);
    if (live == m_live.end() || spell == nullptr || spell->kind != EntityKind::spell) {
        return false;
    }
    const auto cancelled = static_cast<std::uint64_t>(std::count(live->second.done.begin(), live->second.done.end(), false));
    m_counts.phasesCancelled += cancelled;
    ++m_counts.ended[static_cast<std::size_t>(cause)];
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

void Spells::entity_removed(const Entity& spell) {
    m_live.erase(spell.id);
    if (const auto found = m_byCaster.find(spell.spell.caster); found != m_byCaster.end()) {
        std::erase(found->second, spell.id);
        if (found->second.empty()) {
            m_byCaster.erase(found);
        }
    }
    if (spell.spell.cast != 0) {
        m_world.abilities().spell_ended(spell.spell.cast, spell.id);
    }
}

} // namespace soulwarden::backend::world

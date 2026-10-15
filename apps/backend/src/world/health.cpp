#include "world/health.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "core/logging/log.h"
#include "world/world.h"

namespace soulwarden::backend::world {

HealthResult Health::apply(Entity& target, float damage) {
    HealthResult result;
    result.entity = &target;
    result.before = target.vitals.health;
    result.after = target.vitals.health;
    if (target.life.dead) {
        result.error = HealthError::dead;
        return result;
    }
    if (!std::isfinite(damage) || damage < 0.0F) {
        result.error = HealthError::badValue;
        return result;
    }
    target.vitals.health = std::max(target.vitals.health - damage, 0.0F);
    result.after = target.vitals.health;
    target.life.foughtAt = m_world.clock().now();
    if (target.vitals.health == 0.0F) {
        target.life.dead = true;
        target.life.diedAt = m_world.clock().now();
        ++target.life.deaths;
        result.died = true;
    }
    m_world.touch(target.id, ComponentKey::vitals);
    if (result.died) {
        // RemoveOnDeath (stage 3.3b-S1): the statuses of the row end at the death (no end status of a heart rune).
        m_world.statuses().died(target);
        // The spells of the character whose row has EndOnCasterDeath end (stage 3.3b-S2a).
        m_world.spells().caster_died(target.id);
        // The auras that follow the character end (code review of stage 3.3b-S2b, finding F3).
        m_world.spells().target_died(target.id);
    }
    return result;
}

void Health::creature_hit(Entity& creature, const gamedata::CreatureContact& contact, Entity& target) {
    const CreatureDamage damage = m_world.combat().creature_damage(creature, contact, target);
    const HealthResult result = apply(target, damage.dealt);
    if (result.error != HealthError::none) {
        return;
    }
    const std::string mark = log::session_mark(target.owner, target.characterId);
    const log::Context context(mark);
    log::write("world: hit: creature %016llx (%s, level %u) hurts %s with %s: %.1f damage of the type %s (damage of the "
               "creature %.1f, of the attack %.1f, level factor %.2f, armor %.1f, mitigation %.3f), health %.1f to %.1f%s",
               static_cast<unsigned long long>(creature.id),
               creature.creature.kind->id.c_str(),
               creature.creature.level,
               target.name.c_str(),
               contact.row.c_str(),
               static_cast<double>(damage.dealt),
               contact.type.c_str(),
               static_cast<double>(damage.base),
               static_cast<double>(damage.raw),
               static_cast<double>(damage.levelFactor),
               static_cast<double>(damage.armor),
               static_cast<double>(damage.mitigation),
               static_cast<double>(result.before),
               static_cast<double>(result.after),
               result.died ? ": dead" : "");
    // The effects that end after hits, and the heart rune charge of the hit (stage 1.3b).
    m_world.gear().on_hit_taken(target, contact);
    // A counter of an ability (stage 3.3b-S4, Riposte): the window took the hit (its absorption), and the counter starts.
    if (!result.died) {
        // The passive rules with OnHitTaken of the weapon in the hand (stage 3.3b-S5: Power Through Pain).
        m_world.passives().hit_taken(target);
        m_world.abilities().countered(target, creature, contact.reach + behavior_rules::kContactTolerance);
    }
    DamageTaken taken;
    taken.source = creature.id;
    taken.from = creature.position;
    taken.type = contact.wire;
    taken.amount = damage.dealt;
    taken.part = target.vitals.healthMax > 0.0F ? std::min(damage.dealt / target.vitals.healthMax, 1.0F) : 1.0F;
    m_world.damage_taken(target.owner, taken);
    if (result.died) {
        log::write("world: character %s (%s) is dead", target.characterId.c_str(), target.name.c_str());
        m_world.died(target.owner, kRespawnTime);
    }
}

void Health::entered(const Entity& character) {
    if (character.kind != EntityKind::player || !character.life.dead) {
        return;
    }
    using Seconds = std::chrono::duration<float>;
    const float passed = Seconds(m_world.clock().now() - character.life.diedAt).count();
    m_world.died(character.owner, std::clamp(kRespawnTime - passed, 0.0F, kRespawnTime));
}

HealthResult Health::hurt(const std::string& characterId, float damage) {
    Entity* target = m_world.find_character(characterId);
    if (target == nullptr) {
        HealthResult result;
        result.error = HealthError::notInWorld;
        return result;
    }
    const HealthResult result = apply(*target, damage);
    if (result.error != HealthError::none) {
        return result;
    }
    const std::string mark = log::session_mark(target->owner, target->characterId);
    const log::Context context(mark);
    log::write("world: command: %s takes %.1f damage, health %.1f to %.1f%s",
               target->name.c_str(),
               static_cast<double>(damage),
               static_cast<double>(result.before),
               static_cast<double>(result.after),
               result.died ? ": dead" : "");
    if (result.died) {
        log::write("world: character %s (%s) is dead", target->characterId.c_str(), target->name.c_str());
        m_world.died(target->owner, kRespawnTime);
    }
    return result;
}

HealthResult Health::heal(const std::string& characterId) {
    HealthResult result;
    Entity* target = m_world.find_character(characterId);
    if (target == nullptr) {
        result.error = HealthError::notInWorld;
        return result;
    }
    result.entity = target;
    result.before = target->vitals.health;
    result.revived = target->life.dead;
    target->life.dead = false;
    target->vitals.health = target->vitals.healthMax;
    result.after = target->vitals.health;
    m_world.touch(target->id, ComponentKey::vitals);
    if (result.revived) {
        // A character that lives again at its place: the creatures near it do not take it at once. The statuses of its gear
        // that the death removed come back (code review of stage 3.3b-S1, finding F5).
        m_world.creatures().behavior().shelter(*target, false);
        m_world.gear().refresh(*target);
    }
    const std::string mark = log::session_mark(target->owner, target->characterId);
    const log::Context context(mark);
    log::write("world: command: %s has its full health %.1f%s",
               target->name.c_str(),
               static_cast<double>(result.after),
               result.revived ? ", and lives again at its place" : "");
    return result;
}

float Health::heal_amount(Entity& character, float amount, const char* what) {
    if (character.kind != EntityKind::player || character.life.dead || !std::isfinite(amount) || amount <= 0.0F) {
        return 0.0F;
    }
    const float before = character.vitals.health;
    character.vitals.health = std::min(character.vitals.health + amount, character.vitals.healthMax);
    const float healed = character.vitals.health - before;
    if (healed > 0.0F) {
        m_world.touch(character.id, ComponentKey::vitals);
        const std::string mark = log::session_mark(character.owner, character.characterId);
        const log::Context context(mark);
        log::write("world: heal: %s gets %.1f health from %s, health %.1f to %.1f",
                   character.name.c_str(),
                   static_cast<double>(healed),
                   what,
                   static_cast<double>(before),
                   static_cast<double>(character.vitals.health));
    }
    return healed;
}

void Health::fought(Entity& character) {
    if (character.kind == EntityKind::player) {
        character.life.foughtAt = m_world.clock().now();
    }
}

void Health::tick() {
    const WorldClock::time_point now = m_world.clock().now();
    if (m_lastRest && now - *m_lastRest < kRestInterval) {
        return;
    }
    m_lastRest = now;
    // The cost is the number of the characters of the world, one time in each interval.
    std::vector<EntityId> rested;
    m_world.for_each_character([&](const Entity& character) {
        if (!character.life.dead && character.vitals.health < character.vitals.healthMax
            && ((!character.life.foughtAt || now - *character.life.foughtAt >= kRestTime) || character.gear.regeneration > 0.0F)) {
            rested.push_back(character.id);
        }
    });
    for (const EntityId id : rested) {
        Entity* character = m_world.find(id);
        if (character == nullptr) {
            continue;
        }
        // The rest rule, and the regeneration of the perks and the effects (stage 1.3b), each second of a whole interval.
        const bool resting = !character->life.foughtAt || now - *character->life.foughtAt >= kRestTime;
        const float part = (resting ? kRestPart : 0.0F) + character->gear.regeneration * std::chrono::duration<float>(kRestInterval).count();
        character->vitals.health = std::min(character->vitals.health + character->vitals.healthMax * part, character->vitals.healthMax);
        m_world.touch(character->id, ComponentKey::vitals);
        ++m_rests;
    }
}

RespawnPlace Health::respawn_place(const Entity& character) const {
    RespawnPlace place;
    place.position = character.position;
    const gamedata::TravelPlaces* places = m_world.options().travelPlaces;
    if (places == nullptr) {
        return place;
    }
    float nearest = std::numeric_limits<float>::max();
    for (const gamedata::TravelPlace& candidate : places->places()) {
        const float away = horizontal_distance(character.position, candidate.position);
        if (away < nearest) {
            nearest = away;
            place.position = candidate.position;
            place.name = candidate.name;
        }
    }
    return place;
}

HealthResult Health::respawn(SessionId session) {
    HealthResult result;
    Entity* target = m_world.entity_of(session);
    if (target == nullptr) {
        result.error = HealthError::notInWorld;
        return result;
    }
    result.entity = target;
    result.before = target->vitals.health;
    result.after = target->vitals.health;
    if (!target->life.dead) {
        result.error = HealthError::alive;
        return result;
    }
    const RespawnPlace place = respawn_place(*target);
    target->life.dead = false;
    target->vitals.health = target->vitals.healthMax;
    result.after = target->vitals.health;
    result.revived = true;
    m_world.touch(target->id, ComponentKey::vitals);
    // The statuses of the gear that the death removed come back (finding F5).
    m_world.gear().refresh(*target);
    {
        const std::string mark = log::session_mark(target->owner, target->characterId);
        const log::Context context(mark);
        log::write("world: respawn of character %s (%s): it lives again with the health %.1f at %s",
                   target->characterId.c_str(),
                   target->name.c_str(),
                   static_cast<double>(result.after),
                   place.name.empty() ? "the place of its death" : place.name.c_str());
    }
    // The change of the place is a travel: the client takes the position of a movement state
    // with a new teleport number. The client shows its own loading screen for a respawn.
    // A travel that the world refuses (a place that is not a place for a character, a row
    // that the database did not accept): the character lives again at the place of its death.
    const std::string characterId = target->characterId;
    result.travelled = m_world.travel(characterId, place.position, TravelScreen::off).error == TravelError::none
                       || m_world.travel(characterId, target->position, TravelScreen::off).error == TravelError::none;
    return result;
}

} // namespace soulwarden::backend::world

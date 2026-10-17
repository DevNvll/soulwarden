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
        // A dodge of the character ends at its death (stage 3.4f-B).
        if (target.kind == EntityKind::player) {
            m_world.stamina().interrupted(target.characterId, "the death of the character");
            // The aim of an ability and the shots of a volley that did not fly end at the death (code review of stage B1, finding B1-R3: a
            // shot item after a revival confirmed the old aim of Penetrating Shot).
            m_world.abilities().died(target);
        }
        // The spells of the character whose row has EndOnCasterDeath end (stage 3.3b-S2a).
        m_world.spells().caster_died(target.id);
        // The auras that follow the character end (code review of stage 3.3b-S2b, finding F3).
        m_world.spells().target_died(target.id);
        // The committed interaction of the character ends (stage 2.5a): its client gets 3746.
        if (target.kind == EntityKind::player) {
            m_world.interactions().died(target);
        }
    } else if (damage > 0.0F && target.kind == EntityKind::player) {
        // The damage rule of the committed interaction of the character (stage 2.5b, world/interactions.h).
        m_world.interactions().damaged(target);
    }
    return result;
}

CreatureHitOutcome Health::creature_hit(Entity& creature, const gamedata::CreatureContact& contact, Entity& target,
                                        std::optional<WorldClock::time_point> at) {
    const CreatureDamage damage = m_world.combat().creature_damage(creature, contact, target);
    // The dodge (stage 3.4f-B): a hit in the Invulnerability clip of the dodge fragment of the character does not hurt it (game data:
    // player.adb, 0 to 0.5 s; the time of the contact, the end not included).
    const WorldClock::time_point when = at.value_or(m_world.clock().now());
    if (target.kind == EntityKind::player && !target.life.dead && m_world.stamina().immune(target, when)) {
        m_world.stamina().evaded(target, creature, damage.dealt, contact.row.c_str(), when);
        return CreatureHitOutcome::evaded;
    }
    const HealthResult result = apply(target, damage.dealt);
    if (result.error != HealthError::none) {
        return CreatureHitOutcome::hurt;
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
        m_world.passives().hit_taken(target, damage.dealt);
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
    return CreatureHitOutcome::hurt;
}

void Health::status_damage(Entity& target, float amount, const gamedata::StatusRow& row, const DamageSource& source,
                           std::optional<WorldClock::time_point> at) {
    if (target.kind != EntityKind::player || target.life.dead || !std::isfinite(amount) || !(amount > 0.0F)) {
        return;
    }
    // The immune window of a dodge (stage 3.4f-B): a tick in it does no damage, but for a row with IgnoreInvulnerable (a decision of an
    // agent with the client code: the damage receiver 0x692E4A0 refuses an attack on an invulnerable target with no IgnoreInvulnerable;
    // the status row has its own IgnoreInvulnerable, loader 0x44B4430; the tick of the original server is not in the client).
    if (row.cells.number("IgnoreInvulnerable") == 0.0F && m_world.stamina().immune(target, at.value_or(m_world.clock().now()))) {
        const std::string mark = log::session_mark(target.owner, target.characterId);
        const log::Context context(mark);
        log::write("world: dodge: %s takes no damage of %s in the immune window of its dodge (%.1f damage)", target.name.c_str(),
                   row.id.c_str(), static_cast<double>(amount));
        return;
    }
    const HealthResult result = apply(target, amount);
    if (result.error != HealthError::none) {
        return;
    }
    // The damage type of the row (DamageType: Nature for AI_Poison_55) on the wire (the combat tables).
    const std::string_view typeName = row.cells.text("DamageType");
    std::uint8_t wire = 0;
    if (const gamedata::CombatTables* tables = m_world.combat().tables()) {
        for (const gamedata::DamageType& type : tables->data().damageTypes) {
            if (type.name == typeName) {
                wire = type.wire;
            }
        }
    }
    const Entity* attacker = source.attacker != 0 ? m_world.find(source.attacker) : nullptr;
    const std::string mark = log::session_mark(target.owner, target.characterId);
    const log::Context context(mark);
    log::write("world: status: %s takes %.1f damage of the type %s from %s (core damage %.1f of creature %016llx), health %.1f to %.1f%s",
               target.name.c_str(), static_cast<double>(amount), std::string(typeName).c_str(), row.id.c_str(),
               static_cast<double>(source.base), static_cast<unsigned long long>(source.attacker), static_cast<double>(result.before),
               static_cast<double>(result.after), result.died ? ": dead" : "");
    DamageTaken taken;
    taken.source = attacker != nullptr ? attacker->id : 0;
    taken.from = attacker != nullptr ? attacker->position : target.position;
    taken.type = wire;
    taken.amount = amount;
    taken.part = target.vitals.healthMax > 0.0F ? std::min(amount / target.vitals.healthMax, 1.0F) : 1.0F;
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
        // A character that lives again has its full stamina (stage 3.4f-B; the client fills its Stamina component at a revival).
        m_world.stamina().refilled(*target);
    }
    const std::string mark = log::session_mark(target->owner, target->characterId);
    const log::Context context(mark);
    log::write("world: command: %s has its full health %.1f%s",
               target->name.c_str(),
               static_cast<double>(result.after),
               result.revived ? ", and lives again at its place" : "");
    return result;
}

HealOutcome Health::heal(Entity& target, const HealRequest& request) {
    HealOutcome outcome;
    outcome.before = target.vitals.health;
    outcome.after = target.vitals.health;
    if (target.kind != EntityKind::player || target.life.dead || !std::isfinite(request.amount) || request.amount <= 0.0F) {
        ++m_healCounts.nothing;
        return outcome;
    }
    outcome.received = received(target, request.kind);
    const float health = std::min(target.vitals.health + request.amount * outcome.received, target.vitals.healthMax);
    if (!(health > target.vitals.health)) {
        // A target at its full health (or a received factor 0): no change, no number, and no notice (the client shows the change of its
        // health; live check L3).
        ++m_healCounts.nothing;
        return outcome;
    }
    target.vitals.health = health;
    outcome.after = health;
    outcome.gained = outcome.after - outcome.before;
    ++m_healCounts.heals;
    const Revision revision = m_world.touch(target.id, ComponentKey::vitals);
    if (request.number) {
        // The state of the owner after this revision has bit 1 of healthChangeFlags: one number for the gain (live check L3: the same
        // state again gives no second number).
        target.vitals.healShown = revision;
        // The heals that the owner got (older than the keep time) go; a heal that can still be pending for the owner stays (finding
        // M1-R8).
        const WorldClock::time_point now = m_world.clock().now();
        std::vector<Vitals::HealStep>& steps = target.vitals.healSteps;
        steps.erase(std::remove_if(steps.begin(), steps.end(), [&](const Vitals::HealStep& step) { return now - step.time > Vitals::kHealStepKeep; }),
                    steps.end());
        steps.push_back(Vitals::HealStep{revision, outcome.before, health, now});
        if (steps.size() > Vitals::kHealSteps) {
            steps.erase(steps.begin());
        }
        ++m_healCounts.numbers;
    }
    const Entity* healer = request.healer != 0 && request.healer != target.id ? m_world.find(request.healer) : nullptr;
    {
        const std::string mark = log::session_mark(target.owner, target.characterId);
        const log::Context context(mark);
        log::write("world: heal: %s gets %.1f health from %s%s%s (amount %.1f, received factor %.3f), health %.1f to %.1f",
                   target.name.c_str(),
                   static_cast<double>(outcome.gained),
                   request.what,
                   healer != nullptr ? " of " : "",
                   healer != nullptr ? healer->name.c_str() : "",
                   static_cast<double>(request.amount),
                   static_cast<double>(outcome.received),
                   static_cast<double>(outcome.before),
                   static_cast<double>(outcome.after));
    }
    if (healer != nullptr && healer->kind == EntityKind::player) {
        // The notice of the healer (message 4298, live check L3): only the session of the healer.
        m_world.healed_player(healer->owner, target.id, outcome.gained);
        outcome.notice = true;
        ++m_healCounts.notices;
    }
    return outcome;
}

float Health::heal_base(const Entity& healer, const std::string& itemId, std::uint32_t gearScore) const {
    const gamedata::CombatTables* tables = m_world.combat().tables();
    const gamedata::WeaponRow* weapon = tables != nullptr ? tables->weapon_of(itemId) : nullptr;
    if (weapon == nullptr) {
        return 0.0F;
    }
    const float scaling = weapon->scaling[attribute::kFocus];
    const float focus = scaling > 0.0F ? scaling * tables->attribute_damage(attribute::kFocus, healer.attributes.value(attribute::kFocus)) : 0.0F;
    // The level part of the damage rule (Combat::damage: the level damage of the base attributes file times the level above 1): a heal
    // scales with the level as damage scales (a decision of the user of 2026-10-15, through the orchestrator, after live check L9 of
    // stage 3.3b-S6). No part of a different attribute (the Intelligence of the life staff): a heal scales with Focus.
    const std::uint32_t level = std::max<std::uint32_t>(healer.progression.level, 1);
    const float levelPart = tables->constants().levelDamage * static_cast<float>(level - 1);
    return m_world.combat().weapon_base(*weapon, gearScore) * (1.0F + focus + levelPart);
}

float Health::outgoing(const Entity& healer, const Entity& target, const HealSource& source) const {
    const float part = healer.gear.healOutgoing + m_world.passives().heal_bonus(healer, target, source) + (m_world.gear().heal_factor(healer) - 1.0F);
    return std::max(1.0F + part, 0.0F);
}

float Health::received(const Entity& target, HealKind kind) const {
    float part = target.gear.healTaken;
    switch (kind) {
    case HealKind::consumable:
        part += target.gear.healConsumable;
        break;
    case HealKind::ability:
    case HealKind::regeneration:
        part += target.gear.healNonConsumable;
        break;
    case HealKind::lifeSteal:
        part += target.gear.healNonConsumable + target.gear.healLifeSteal;
        break;
    }
    return std::max(1.0F + part, 0.0F);
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
    // The full stamina of a respawn (stage 3.4f-B).
    m_world.stamina().refilled(*target);
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

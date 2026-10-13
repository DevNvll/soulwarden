#include "world/behavior.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/logging/log.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

constexpr float kPi = 3.14159265358979F;

float degrees(float radians) {
    return radians * 180.0F / kPi;
}

float distance(const state::WorldPosition& a, const state::WorldPosition& b) {
    return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

/** @return The difference of two directions, 0 to 180 degrees. */
float turn_between(float first, float second) {
    const float difference = std::fabs(std::fmod(first - second, 360.0F));
    return difference > 180.0F ? 360.0F - difference : difference;
}

unsigned long long id_number(EntityId id) {
    return static_cast<unsigned long long>(id);
}

std::chrono::milliseconds milliseconds(float seconds) {
    return std::chrono::milliseconds(static_cast<long long>(std::lround(seconds * 1000.0F)));
}

/** @return The leash of a slice in a fight: the largest distance of a creature from its home, in meters. */
float leash_of(const gamedata::CreatureBehavior& values) {
    return values.combatLeash > 0.0F ? values.combatLeash : behavior_rules::kDefaultLeash;
}

} // namespace

float yaw_to(const state::WorldPosition& from, const state::WorldPosition& to) {
    // The direction of a creature is the angle about the vertical axis, 0 for north (+y), and
    // it grows to the west (-x): the rotation of the movement state on the wire.
    const float angle = degrees(std::atan2(-(to.x - from.x), to.y - from.y));
    return angle < 0.0F ? angle + 360.0F : angle;
}

float angle_to(const state::WorldPosition& from, float yaw, const state::WorldPosition& to) {
    return turn_between(yaw_to(from, to), yaw);
}

Behavior::Behavior(World& world, Creatures& creatures) : m_world(world), m_creatures(creatures) {}

void Behavior::set_ignored(const std::string& characterId, bool ignored) {
    if (ignored) {
        m_ignored.insert(characterId);
    } else {
        m_ignored.erase(characterId);
    }
}

Entity* Behavior::fightable(EntityId id) {
    Entity* target = id != 0 ? m_world.find(id) : nullptr;
    if (target == nullptr || target->kind != EntityKind::player || target->life.dead || ignores(target->characterId)) {
        return nullptr;
    }
    return target;
}

void Behavior::made(Entity& creature) {
    CreatureMind& mind = creature.creature.mind;
    mind.home = creature.position;
    mind.homeYaw = creature.creature.yaw;
    mind.sentYaw = creature.creature.yaw;
    const gamedata::CreatureData* data = m_creatures.data();
    if (creature.creature.spawnPoint && data != nullptr) {
        // The file of the tool has the terrain height at each spawn point.
        mind.walks = gamedata::CreatureData::on_terrain(data->spawn_points()[*creature.creature.spawnPoint]);
    } else {
        // A creature of a command: the terrain at its place.
        const std::optional<float> ground = m_world.navigation().ground_height(creature.position);
        mind.walks = ground && std::fabs(creature.position.z - *ground) <= gamedata::kTerrainTolerance;
    }
}

void Behavior::died(Entity& creature) {
    CreatureMind& mind = creature.creature.mind;
    mind.mode = CreatureMode::rest;
    mind.target = 0;
    mind.attack.reset();
    m_active.erase(creature.id);
}

void Behavior::removed(const Entity& entity) {
    m_active.erase(entity.id);
}

void Behavior::take_target(Entity& creature, const Entity& target, const char* cause, float range) {
    CreatureMind& mind = creature.creature.mind;
    mind.mode = CreatureMode::fight;
    mind.target = target.id;
    m_active.insert(creature.id);
    m_counts.largestActive = std::max(m_counts.largestActive, m_active.size());
    log::write("world: creature %016llx (%s) has a target: %s (%s, %.1f m)",
               id_number(creature.id),
               creature.creature.kind->id.c_str(),
               target.name.c_str(),
               cause,
               static_cast<double>(range));
}

void Behavior::lose_target(Entity& creature, const char* cause) {
    CreatureMind& mind = creature.creature.mind;
    mind.target = 0;
    mind.mode = CreatureMode::home;
    ++m_counts.targetsLost;
    log::write("world: creature %016llx (%s) lost its target (%s): it goes to its home, %.1f m",
               id_number(creature.id),
               creature.creature.kind->id.c_str(),
               cause,
               static_cast<double>(horizontal_distance(creature.position, mind.home)));
}

void Behavior::hurt_by(Entity& creature, EntityId attacker) {
    if (!m_world.options().creatureBehavior || creature.creature.dead || !creature.creature.body->fights()) {
        return;
    }
    CreatureMind& mind = creature.creature.mind;
    const Entity* target = fightable(attacker);
    // A creature has one target: a hit of a different character does not change it.
    if (target == nullptr || fightable(mind.target) != nullptr) {
        return;
    }
    ++m_counts.targetsByHit;
    take_target(creature, *target, "it did hurt the creature", distance(creature.position, target->position));
}

void Behavior::tick() {
    if (!m_world.options().creatureBehavior || !m_creatures.has_data()) {
        return;
    }
    const WorldClock::time_point now = m_world.clock().now();
    // The contacts of the attacks that play, at their times: a tick is shorter than a step.
    while (!m_contacts.empty() && m_contacts.begin()->first <= now) {
        const PendingContact pending = m_contacts.begin()->second;
        m_contacts.erase(m_contacts.begin());
        contact(pending);
    }
    if (m_lastStep && now - *m_lastStep < behavior_rules::kStepInterval) {
        return;
    }
    using Seconds = std::chrono::duration<float>;
    const float passed = m_lastStep ? std::min(Seconds(now - *m_lastStep).count(), Seconds(behavior_rules::kLongestStep).count())
                                    : Seconds(behavior_rules::kStepInterval).count();
    m_lastStep = now;
    const LoadTimer timer(m_load);
    step(passed);
}

void Behavior::step(float seconds) {
    ++m_counts.steps;
    sense();
    // A step can remove a creature from the set: walk a copy of the ids.
    const std::vector<EntityId> active(m_active.begin(), m_active.end());
    for (const EntityId id : active) {
        Entity* creature = m_creatures.find(id);
        if (creature == nullptr || creature->creature.dead) {
            m_active.erase(id);
            continue;
        }
        ++m_counts.creatureSteps;
        if (creature->creature.mind.mode == CreatureMode::home) {
            go_home(*creature, seconds);
        } else {
            think(*creature, seconds);
        }
    }
}

void Behavior::sense() {
    const gamedata::CreatureData* data = m_creatures.data();
    const float range = data->largest_sight();
    if (!(range > 0.0F)) {
        return; // No slice of the data fights.
    }
    // The vision rule runs from the characters: its cost depends on the creatures near a
    // character, and a creature with no character near it needs no time.
    std::vector<const Entity*> characters;
    m_world.for_each_character([&](const Entity& character) {
        if (!character.life.dead && !ignores(character.characterId)) {
            characters.push_back(&character);
        }
    });
    for (const Entity* character : characters) {
        m_world.for_each_near(character->position, range, [&](Entity& creature) {
            if (creature.kind != EntityKind::creature || creature.creature.dead
                || creature.creature.mind.mode != CreatureMode::rest || !creature.creature.body->fights()) {
                return;
            }
            ++m_counts.sensed;
            const gamedata::CreatureBehavior& values = *creature.creature.body->behavior;
            const float away = distance(creature.position, character->position);
            if (away > values.sightDistance || std::fabs(creature.position.z - character->position.z) > behavior_rules::kSenseHeight) {
                return;
            }
            // The angle of the slice is the angle to each side of the direction of the creature.
            if (values.sightAngle < 180.0F && angle_to(creature.position, creature.creature.yaw, character->position) > values.sightAngle) {
                return;
            }
            ++m_counts.targetsBySight;
            take_target(creature, *character, "in its vision", away);
        });
    }
}

void Behavior::turn_to(Entity& creature, const state::WorldPosition& place) {
    if (horizontal_distance(creature.position, place) < 0.01F) {
        return; // No direction to a place at the place of the creature.
    }
    CreatureMind& mind = creature.creature.mind;
    creature.creature.yaw = yaw_to(creature.position, place);
    if (turn_between(creature.creature.yaw, mind.sentYaw) >= behavior_rules::kTurnAngle) {
        mind.sentYaw = creature.creature.yaw;
        m_creatures.moved(creature);
    }
}

void Behavior::think(Entity& creature, float seconds) {
    CreatureMind& mind = creature.creature.mind;
    const gamedata::CreatureSlice& body = *creature.creature.body;
    const WorldClock::time_point now = m_world.clock().now();
    if (mind.attack) {
        // An attack plays: the creature stays at its place and keeps its direction to its end.
        if (now < mind.attackEnds) {
            return;
        }
        mind.attack.reset();
        mind.ready = now + behavior_rules::kAttackPause;
    }
    Entity* target = fightable(mind.target);
    if (target == nullptr) {
        const Entity* gone = mind.target != 0 ? m_world.find(mind.target) : nullptr;
        lose_target(creature, gone == nullptr          ? "the target left the world"
                              : gone->life.dead        ? "the target is dead"
                                                       : "the creatures ignore the target");
        return;
    }
    const gamedata::CreatureBehavior& values = *body.behavior;
    const float fromHome = horizontal_distance(creature.position, mind.home);
    if (fromHome > leash_of(values)) {
        lose_target(creature, "the creature is at the end of its leash");
        return;
    }
    const float away = horizontal_distance(creature.position, target->position);
    const float farthest = mind.walks ? behavior_rules::kLoseDistance
                                      : (values.combatDistance > 0.0F ? values.combatDistance : behavior_rules::kDefaultCombatSight);
    if (away > farthest) {
        lose_target(creature, "the target is too far");
        return;
    }
    // The next attack of the slice, in the sequence of its attacks.
    const auto attackIndex = static_cast<std::uint32_t>(mind.attacks % body.attacks.size());
    const gamedata::CreatureAttack& attack = body.attacks[attackIndex];
    const float reach = attack.contacts.front().reach;
    // A creature that walks goes some distance into the reach of its attack. A creature that does
    // not leave its place attacks a target that is in the reach.
    const float stop = mind.walks ? std::max(reach - behavior_rules::kReachMargin, behavior_rules::kNearest) : reach;
    if (away <= stop + 0.05F && std::fabs(creature.position.z - target->position.z) <= behavior_rules::kContactHeight) {
        turn_to(creature, target->position);
        if (now >= mind.ready) {
            start_attack(creature, *target, attackIndex);
        }
        return;
    }
    if (!mind.walks) {
        turn_to(creature, target->position);
        return;
    }
    // One step to the place at which the target is in the reach. The navigation gives the
    // place and the height of the ground: in this stage the straight line on the terrain.
    const float way = std::min(values.runSpeed * seconds, away - stop);
    const std::optional<state::WorldPosition> next = m_world.navigation().next_step(creature.position, target->position, way);
    if (!next) {
        turn_to(creature, target->position);
        return;
    }
    creature.creature.yaw = yaw_to(creature.position, target->position);
    mind.sentYaw = creature.creature.yaw;
    m_creatures.move(creature, *next);
    ++m_counts.moves;
}

void Behavior::go_home(Entity& creature, float seconds) {
    CreatureMind& mind = creature.creature.mind;
    const WorldClock::time_point now = m_world.clock().now();
    if (mind.attack) {
        if (now < mind.attackEnds) {
            return; // The attack that plays ends first.
        }
        mind.attack.reset();
    }
    const float away = horizontal_distance(creature.position, mind.home);
    if (away <= behavior_rules::kHomeDistance || !mind.walks) {
        arrive(creature);
        return;
    }
    const float speed = creature.creature.body->behavior->runSpeed;
    const float way = speed * seconds;
    if (way >= away) {
        arrive(creature);
        return;
    }
    const std::optional<state::WorldPosition> next = m_world.navigation().next_step(creature.position, mind.home, way);
    if (!next) {
        // No ground on the way: the creature is at its home in one step.
        arrive(creature);
        return;
    }
    creature.creature.yaw = yaw_to(creature.position, mind.home);
    mind.sentYaw = creature.creature.yaw;
    m_creatures.move(creature, *next);
    ++m_counts.moves;
}

void Behavior::arrive(Entity& creature) {
    CreatureMind& mind = creature.creature.mind;
    // The exact place and direction of the spawn point: the height of a spawn point can be
    // some centimeters from the terrain height.
    creature.creature.yaw = mind.homeYaw;
    mind.sentYaw = mind.homeYaw;
    m_creatures.move(creature, mind.home);
    m_creatures.restore(creature);
    mind.mode = CreatureMode::rest;
    mind.target = 0;
    m_active.erase(creature.id);
    ++m_counts.returns;
    log::write("world: creature %016llx (%s) is at its home, health %.1f",
               id_number(creature.id),
               creature.creature.kind->id.c_str(),
               static_cast<double>(creature.vitals.health));
}

void Behavior::start_attack(Entity& creature, Entity& target, std::uint32_t attackIndex) {
    CreatureMind& mind = creature.creature.mind;
    const gamedata::CreatureSlice& body = *creature.creature.body;
    const gamedata::CreatureAttack& attack = body.attacks[attackIndex];
    const WorldClock::time_point now = m_world.clock().now();
    // The creature looks at its target for the whole attack.
    creature.creature.yaw = yaw_to(creature.position, target.position);
    mind.sentYaw = creature.creature.yaw;
    const std::chrono::milliseconds length = milliseconds(attack.length);
    if (!m_creatures.start_action(creature, attack.action, length)) {
        mind.ready = now + behavior_rules::kAttackPause;
        return;
    }
    mind.attack = attackIndex;
    mind.attackGeneration = creature.creature.action->generation;
    mind.attackEnds = now + length;
    ++mind.attacks;
    ++m_counts.attacks;
    for (std::uint32_t index = 0; index < attack.contacts.size(); ++index) {
        m_contacts.emplace(now + milliseconds(attack.contacts[index].time),
                           PendingContact{creature.id, mind.attackGeneration, attackIndex, index});
    }
    log::write("world: creature %016llx (%s) starts the attack %s at %s (%.1f m, %zu contact(s), %.2f s)",
               id_number(creature.id),
               creature.creature.kind->id.c_str(),
               body.actions[attack.action].name.c_str(),
               target.name.c_str(),
               static_cast<double>(horizontal_distance(creature.position, target.position)),
               attack.contacts.size(),
               static_cast<double>(attack.length));
}

void Behavior::contact(const PendingContact& pending) {
    Entity* creature = m_creatures.find(pending.creature);
    if (creature == nullptr || creature->creature.dead) {
        return;
    }
    const CreatureMind& mind = creature->creature.mind;
    // The attack must be the action that plays: a different action (a command) replaced it if not.
    if (!mind.attack || *mind.attack != pending.attack || mind.attackGeneration != pending.generation
        || !creature->creature.action || creature->creature.action->generation != pending.generation) {
        return;
    }
    const gamedata::CreatureSlice& body = *creature->creature.body;
    const gamedata::CreatureContact& data = body.attacks[pending.attack].contacts[pending.contact];
    const char* missed = nullptr;
    float away = 0.0F;
    Entity* target = fightable(mind.target);
    if (target == nullptr) {
        missed = "the target is not there";
    } else {
        away = horizontal_distance(creature->position, target->position);
        if (away > data.reach + behavior_rules::kContactTolerance
            || std::fabs(creature->position.z - target->position.z) > behavior_rules::kContactHeight) {
            missed = "the target is out of the reach";
        } else if (away > 0.3F && angle_to(creature->position, creature->creature.yaw, target->position) > behavior_rules::kContactAngle) {
            missed = "the target is not in front of the creature";
        }
    }
    if (missed != nullptr) {
        ++m_counts.contactsMissed;
        log::write("world: miss: the attack %s of creature %016llx (%s) did not hurt its target: %s (%.2f m, reach %.2f m)",
                   body.actions[body.attacks[pending.attack].action].name.c_str(),
                   id_number(creature->id),
                   creature->creature.kind->id.c_str(),
                   missed,
                   static_cast<double>(away),
                   static_cast<double>(data.reach));
        return;
    }
    ++m_counts.contactsHit;
    m_world.health().creature_hit(*creature, data, *target);
}

} // namespace soulwarden::backend::world

#include "world/behavior.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>

#include "core/logging/log.h"
#include "world/creatures.h"
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

/** @return True when a creature at `place` is at `goal`: this near in the plane, and on the floor of the goal. */
bool at_place(const state::WorldPosition& place, const state::WorldPosition& goal, float range) {
    return horizontal_distance(place, goal) <= range && std::fabs(place.z - goal.z) <= behavior_rules::kFloorTolerance;
}

std::chrono::milliseconds milliseconds(float seconds) {
    return std::chrono::milliseconds(static_cast<long long>(std::lround(seconds * 1000.0F)));
}

/** @return The leash of a slice in a fight: the largest distance of a creature from its anchor, in meters. */
float leash_of(const gamedata::CreatureBehavior& values) {
    return values.combatLeash > 0.0F ? values.combatLeash : behavior_rules::kDefaultLeash;
}

/** @return The speed of a walk with no target (a patrol, a wander), in meters in one second. */
float ambient_speed_of(const gamedata::CreatureBehavior& values) {
    if (values.ambientSpeed > 0.0F) {
        return values.ambientSpeed;
    }
    return values.walkSpeed > 0.0F ? values.walkSpeed : behavior_rules::kDefaultAmbientSpeed;
}

/** @return True for the name of a task of a waypoint that loops (the mark of the game files: "_LOOP" at its end). */
bool loops(const std::string& task) {
    static constexpr std::string_view kLoop = "_loop";
    if (task.size() < kLoop.size()) {
        return false;
    }
    for (std::size_t index = 0; index < kLoop.size(); ++index) {
        const char letter = task[task.size() - kLoop.size() + index];
        if ((letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter + 32) : letter) != kLoop[index]) {
            return false;
        }
    }
    return true;
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

bool Behavior::move_anchor(Entity& creature, const state::WorldPosition& anchor) {
    CreatureMind& mind = creature.creature.mind;
    if (mind.mode != CreatureMode::fight) {
        return false;
    }
    // The place where the creature took its target stays in its trail, as the second place.
    mind.anchor = anchor;
    mind.trail.insert(mind.trail.begin(), anchor);
    log::write("world: command: the anchor of creature %016llx (%s) is at %.2f %.2f %.2f (%zu places in its trail)",
               id_number(creature.id),
               creature.creature.kind->id.c_str(),
               static_cast<double>(anchor.x),
               static_cast<double>(anchor.y),
               static_cast<double>(anchor.z),
               mind.trail.size());
    return true;
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
    mind.anchor = creature.position;
    // The creatures of one place do not all start their roam at one time.
    const auto shortest = static_cast<float>(behavior_rules::kWanderWaitShortest.count());
    mind.roamWaitUntil = m_world.clock().now() + std::chrono::milliseconds(static_cast<long long>(m_world.random_unit() * shortest));
    // The navigation says if the creature stands on a floor: now, or when it has the data of the place.
    mind.walks = false;
    mind.groundKnown = false;
    (void)can_walk(creature);
}

bool Behavior::can_walk(Entity& creature) {
    CreatureMind& mind = creature.creature.mind;
    if (!mind.groundKnown) {
        const Footing footing = m_world.navigation().footing(creature.position);
        if (footing != Footing::unknown) {
            mind.walks = footing == Footing::walks;
            mind.groundKnown = true;
        }
    }
    return mind.groundKnown && mind.walks;
}

void Behavior::died(Entity& creature) {
    CreatureMind& mind = creature.creature.mind;
    const bool wasImmune = creature.creature.immune();
    mind.mode = CreatureMode::rest;
    mind.target = 0;
    mind.attack.reset();
    mind.trail.clear();
    mind.holdUntil.reset();
    if (wasImmune) {
        m_creatures.immune_changed(creature);
    }
    m_active.erase(creature.id);
    m_roaming.erase(creature.id);
    m_world.navigation().forget(creature.id);
}

void Behavior::removed(const Entity& entity) {
    m_active.erase(entity.id);
    m_roaming.erase(entity.id);
    m_world.navigation().forget(entity.id);
}

const gamedata::PatrolRoute* Behavior::route_of(const Entity& creature) const {
    const gamedata::CreatureData* data = m_creatures.data();
    if (data == nullptr || !creature.creature.spawnPoint || *creature.creature.spawnPoint >= data->spawn_points().size()) {
        return nullptr;
    }
    return data->route_of(data->spawn_points()[*creature.creature.spawnPoint]);
}

void Behavior::shelter(Entity& character, bool waitsForClient) {
    if (character.kind != EntityKind::player) {
        return;
    }
    const WorldOptions& options = m_world.options();
    if (options.shelterTime <= std::chrono::milliseconds::zero()) {
        return; // No shelter in this world (a test of a different rule).
    }
    const WorldClock::time_point now = m_world.clock().now();
    const bool waits = waitsForClient && options.shelterLongest > options.shelterTime;
    character.life.shelterWaits = waits;
    character.life.shelterUntil = now + (waits ? options.shelterLongest : options.shelterTime);
    // A creature that has the character as its target loses it: the character went to a different place.
    const std::vector<EntityId> active(m_active.begin(), m_active.end());
    for (const EntityId id : active) {
        Entity* creature = m_creatures.find(id);
        if (creature != nullptr && !creature->creature.dead && creature->creature.mind.mode == CreatureMode::fight
            && creature->creature.mind.target == character.id) {
            lose_target(*creature, "the target went to a different place");
        }
    }
}

void Behavior::client_arrived(Entity& character) {
    if (!character.life.shelterWaits) {
        return;
    }
    character.life.shelterWaits = false;
    character.life.shelterUntil = std::min(character.life.shelterUntil, m_world.clock().now() + m_world.options().shelterTime);
}

bool Behavior::sheltered(const Entity& character) const {
    return m_world.clock().now() < character.life.shelterUntil;
}

std::uint32_t Behavior::next_attack(const Creature& creature) {
    const gamedata::CreatureSlice& body = *creature.body;
    const std::uint32_t count = creature.mind.attacks;
    if (body.lightAttacks.empty() || body.heavyAttacks.empty()) {
        return static_cast<std::uint32_t>(count % body.attacks.size());
    }
    // A cycle: `kLightAttacks` light attacks, then one heavy attack.
    const std::uint32_t cycle = count / (behavior_rules::kLightAttacks + 1);
    const std::uint32_t place = count % (behavior_rules::kLightAttacks + 1);
    if (place < behavior_rules::kLightAttacks) {
        return body.lightAttacks[(cycle * behavior_rules::kLightAttacks + place) % body.lightAttacks.size()];
    }
    return body.heavyAttacks[cycle % body.heavyAttacks.size()];
}

const Entity* Behavior::taken(const Entity& creature, const state::WorldPosition& place) const {
    const Entity* found = nullptr;
    m_world.for_each_near(place, behavior_rules::kCrowdDistance, [&](Entity& other) {
        if (found == nullptr && other.kind == EntityKind::creature && other.id != creature.id && !other.creature.dead
            && std::fabs(other.position.z - place.z) <= behavior_rules::kContactHeight
            && horizontal_distance(other.position, place) < behavior_rules::kCrowdDistance) {
            found = &other;
        }
    });
    return found;
}

float Behavior::free_way(const Entity& creature, const Entity& target, float way) const {
    const float away = horizontal_distance(creature.position, target.position);
    if (away < 0.01F) {
        return way;
    }
    const float dx = (target.position.x - creature.position.x) / away;
    const float dy = (target.position.y - creature.position.y) / away;
    static constexpr float kPart = 0.25F;
    for (float length = way; length > 0.05F; length -= kPart) {
        if (taken(creature, {creature.position.x + dx * length, creature.position.y + dy * length, creature.position.z}) == nullptr) {
            return length;
        }
    }
    return 0.0F;
}

const Entity* Behavior::blocker(const Entity& creature, const Entity& target, float length) const {
    const float away = horizontal_distance(creature.position, target.position);
    if (away < 0.01F) {
        return nullptr;
    }
    const float dx = (target.position.x - creature.position.x) / away;
    const float dy = (target.position.y - creature.position.y) / away;
    // The end of the step that free_way looked at first.
    return taken(creature, {creature.position.x + dx * length, creature.position.y + dy * length, creature.position.z});
}

void Behavior::no_way(Entity& creature, const Entity& target) {
    CreatureMind& mind = creature.creature.mind;
    mind.noWayTarget = target.id;
    mind.noWayUntil = m_world.clock().now() + behavior_rules::kNoWayPause;
    ++m_counts.targetsNoWay;
    lose_target(creature, "no way to the target, after its wait at the place nearest to it");
}

void Behavior::take_target(Entity& creature, const Entity& target, const char* cause, float range) {
    CreatureMind& mind = creature.creature.mind;
    if (m_roaming.erase(creature.id) != 0) {
        // The walk of the roam ends: the way to the target has the run of the slice.
        stand(creature);
    }
    mind.mode = CreatureMode::fight;
    mind.target = target.id;
    // The anchor of the leash: the place where the creature took its target. The way back ends
    // there, and the patrol or the wander goes on from it.
    mind.anchor = creature.position;
    mind.trail.assign(1, creature.position);
    mind.retracing = false;
    mind.holdUntil.reset();
    mind.roamGoal.reset();
    m_active.insert(creature.id);
    // The character is in a fight from now: its health does not come back (world/health.h).
    if (Entity* character = m_world.find(target.id)) {
        m_world.health().fought(*character);
    }
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
    mind.blockedSteps = 0;
    mind.holdUntil.reset();
    mind.retracing = false;
    // The path to the target is of no use for the way back: without this the creature goes
    // on with it for some cells while the search of its way back runs.
    m_world.navigation().forget(creature.id);
    // From now to the anchor the creature takes no damage: each observer gets the mark.
    m_creatures.immune_changed(creature);
    ++m_counts.targetsLost;
    log::write("world: creature %016llx (%s) lost its target (%s): it goes back to its anchor, %.1f m, immune",
               id_number(creature.id),
               creature.creature.kind->id.c_str(),
               cause,
               static_cast<double>(horizontal_distance(creature.position, mind.anchor)));
}

void Behavior::hurt_by(Entity& creature, EntityId attacker) {
    if (!m_world.options().creatureBehavior || creature.creature.dead || !creature.creature.body->fights()
        || creature.creature.immune()) {
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
    // The roam of the creatures at rest near a character.
    const WorldClock::time_point now = m_world.clock().now();
    if (!m_lastRoamScan || now - *m_lastRoamScan >= behavior_rules::kRoamScanInterval) {
        m_lastRoamScan = now;
        select_roamers();
    }
    const std::vector<EntityId> roaming(m_roaming.begin(), m_roaming.end());
    for (const EntityId id : roaming) {
        Entity* creature = m_creatures.find(id);
        if (creature == nullptr || creature->creature.dead || creature->creature.mind.mode != CreatureMode::rest) {
            m_roaming.erase(id);
            continue;
        }
        ++m_counts.roamSteps;
        roam(*creature, seconds);
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
    const WorldClock::time_point now = m_world.clock().now();
    m_world.for_each_character([&](const Entity& character) {
        if (character.life.dead || ignores(character.characterId)) {
            return;
        }
        if (now < character.life.shelterUntil) {
            ++m_counts.sheltered; // The character came to its place a short time before.
            return;
        }
        characters.push_back(&character);
    });
    for (const Entity* character : characters) {
        m_world.for_each_near(character->position, range, [&](Entity& creature) {
            if (creature.kind != EntityKind::creature || creature.creature.dead
                || creature.creature.mind.mode != CreatureMode::rest || !creature.creature.body->fights()) {
                return;
            }
            if (creature.creature.mind.noWayTarget == character->id && now < creature.creature.mind.noWayUntil) {
                return; // The creature found no way to this character a short time before.
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
            // A wall between the two stops the vision (the navigation data; with no data nothing stops it).
            if (!m_world.navigation().sees(creature.position, character->position)) {
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

void Behavior::turn(Entity& creature, float yaw) {
    CreatureMind& mind = creature.creature.mind;
    creature.creature.yaw = yaw;
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
        const bool heavy = body.attacks[*mind.attack].heavy;
        mind.attack.reset();
        mind.ready = now + (heavy ? behavior_rules::kHeavyPause : behavior_rules::kAttackPause);
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
    // The leash is a distance from the anchor: the place where the creature took its target.
    const float fromAnchor = horizontal_distance(creature.position, mind.anchor);
    if (fromAnchor > leash_of(values)) {
        lose_target(creature, "the creature is at the end of its leash");
        return;
    }
    const float away = horizontal_distance(creature.position, target->position);
    const bool walks = can_walk(creature);
    const float farthest = walks ? behavior_rules::kLoseDistance
                                 : (values.combatDistance > 0.0F ? values.combatDistance : behavior_rules::kDefaultCombatSight);
    if (away > farthest) {
        lose_target(creature, "the target is too far");
        return;
    }
    // The next attack of the slice: its light attacks in their sequence, and its heavy attacks between them.
    const std::uint32_t attackIndex = next_attack(creature.creature);
    const gamedata::CreatureAttack& attack = body.attacks[attackIndex];
    const float reach = attack.contacts.front().reach;
    // A creature that walks goes some distance into the reach of its attack. A creature that does
    // not leave its place attacks a target that is in the reach.
    const float stop = walks ? std::max(reach - behavior_rules::kReachMargin, behavior_rules::kNearest) : reach;
    const bool level = std::fabs(creature.position.z - target->position.z) <= behavior_rules::kContactHeight;
    const bool near = away <= stop + 0.05F && level;
    // A wall between the creature and its target stops the attack too: the creature then goes on its path.
    const bool sees = (near || away - stop < values.runSpeed * seconds + behavior_rules::kCrowdRange || away <= reach) && level
                      && m_world.navigation().sees(creature.position, target->position);
    if (near && sees) {
        turn_to(creature, target->position);
        if (now >= mind.ready) {
            start_attack(creature, *target, attackIndex);
        }
        return;
    }
    if (!walks) {
        turn_to(creature, target->position);
        return;
    }
    // One step on the path to the target. The navigation gives the place and the height of its
    // floor. The step ends where the target is in the reach, when the line to the target is free.
    float way = values.runSpeed * seconds;
    if (sees && away - stop < way + behavior_rules::kCrowdRange) {
        // The last steps, on the free line to the target. The step ends where the target is in
        // the reach. A different creature at its end makes it shorter: the creature then
        // attacks from its place when the target is in the reach of the attack.
        const float wanted = std::min(way, std::max(away - stop, 0.05F));
        way = free_way(creature, *target, wanted);
        if (way < wanted) {
            ++m_counts.crowded;
            if (way <= 0.05F) {
                // The rule of the crowd: the creature waits for a creature with the same target,
                // and for a different creature (one with no target, a kind that does not fight)
                // for `kCrowdPatience` steps; then it goes past it (code review of stage 3.4d,
                // finding F7: two creatures waited for each other for all time).
                const Entity* other = blocker(creature, *target, wanted);
                if (other == nullptr || other->creature.mind.target == mind.target || mind.blockedSteps < behavior_rules::kCrowdPatience) {
                    ++mind.blockedSteps;
                    turn_to(creature, target->position);
                    if (away <= reach && now >= mind.ready) {
                        start_attack(creature, *target, attackIndex);
                    }
                    return;
                }
                way = wanted;
            }
        }
    }
    // The way to the target, or to the place nearest to it when it has no way (`nearest`).
    NavStep next = m_world.navigation().next_step(creature.id, creature.position, target->position, way, true);
    // A different creature at the end of the step: a shorter step, so that the creatures of a
    // queue stand near each other.
    for (float shorter = way * 0.5F; next.kind == StepKind::moved && shorter >= 0.2F && taken(creature, next.position) != nullptr;
         shorter *= 0.5F) {
        m_world.navigation().take_back(creature.id);
        next = m_world.navigation().next_step(creature.id, creature.position, target->position, shorter, true);
    }
    // The wait at the place nearest to a target with no way: its time starts when the creature
    // is at that place (the end of its path, or no step nearer), not at the first step of the
    // path to it (code review of stage 3.4e, finding F4). A step on a full path to the target
    // ends it.
    if (next.kind == StepKind::arrived || next.kind == StepKind::noWay) {
        if (!mind.holdUntil) {
            mind.holdUntil = now + behavior_rules::kNoWayHold;
            ++m_counts.holds;
            log::write("world: creature %016llx (%s) has no way to its target %s: it goes to the place nearest to it and waits "
                       "there with it (%.1f m)",
                       id_number(creature.id),
                       creature.creature.kind->id.c_str(),
                       target->name.c_str(),
                       static_cast<double>(away));
        } else if (now >= *mind.holdUntil) {
            no_way(creature, *target);
            return;
        }
    } else if (next.kind == StepKind::moved && !next.partial) {
        mind.holdUntil.reset();
    }
    switch (next.kind) {
    case StepKind::moved:
        if (const Entity* other = taken(creature, next.position);
            other != nullptr && (other->creature.mind.target == mind.target || mind.blockedSteps < behavior_rules::kCrowdPatience)) {
            // A different creature stands at the place of the step: this one waits at its place.
            m_world.navigation().take_back(creature.id);
            ++mind.blockedSteps;
            ++m_counts.crowded;
            turn_to(creature, target->position);
            if (sees && away <= reach && now >= mind.ready) {
                start_attack(creature, *target, attackIndex);
            }
            break;
        }
        mind.blockedSteps = 0;
        creature.creature.yaw = yaw_to(creature.position, next.position);
        mind.sentYaw = creature.creature.yaw;
        m_creatures.move(creature, next.position, Gait::run);
        record_trail(creature);
        ++m_counts.moves;
        break;
    case StepKind::waiting:
        // The navigation data of the place is not there yet, or the path search of the creature waits.
        turn_to(creature, target->position);
        break;
    case StepKind::arrived:
    case StepKind::noWay:
        // The end of the path, and the target is not in the reach: the target is at a place with
        // no way to it. The creature waits at its place (the place nearest to the target), turns
        // to it, and attacks it when it comes into the reach.
        turn_to(creature, target->position);
        if (away <= reach && level && now >= mind.ready && m_world.navigation().sees(creature.position, target->position)) {
            start_attack(creature, *target, attackIndex);
        }
        break;
    case StepKind::noGround:
        mind.walks = false;
        mind.groundKnown = true;
        turn_to(creature, target->position);
        break;
    }
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
    // At the anchor: near it in the plane and on its floor. A creature below or above its anchor
    // on a different floor goes there on its way, and it does not jump while a session observes
    // it (code review of stage 3.4e, finding F1).
    if (at_place(creature.position, mind.anchor, behavior_rules::kHomeDistance) || !can_walk(creature)) {
        arrive(creature);
        return;
    }
    if (mind.retracing) {
        retrace(creature, seconds);
        return;
    }
    const float speed = creature.creature.body->behavior->runSpeed;
    const NavStep next = m_world.navigation().next_step(creature.id, creature.position, mind.anchor, speed * seconds);
    switch (next.kind) {
    case StepKind::moved:
        creature.creature.yaw = yaw_to(creature.position, next.position);
        mind.sentYaw = creature.creature.yaw;
        m_creatures.move(creature, next.position, Gait::run);
        ++m_counts.moves;
        break;
    case StepKind::waiting:
        break;
    case StepKind::arrived:
        arrive(creature);
        break;
    case StepKind::noWay:
    case StepKind::noGround:
        // No path back: the creature walks the places of its way out back (the trail), and it
        // does not jump to its anchor while a session can observe it.
        mind.retracing = true;
        m_world.navigation().forget(creature.id);
        ++m_counts.retraces;
        log::write("world: creature %016llx (%s) has no path back to its anchor: it walks the %zu places of its way back",
                   id_number(creature.id),
                   creature.creature.kind->id.c_str(),
                   mind.trail.size());
        retrace(creature, seconds);
        break;
    }
}

void Behavior::retrace(Entity& creature, float seconds) {
    CreatureMind& mind = creature.creature.mind;
    std::vector<state::WorldPosition>& trail = mind.trail;
    while (!trail.empty() && at_place(creature.position, trail.back(), behavior_rules::kRoamArrival)) {
        trail.pop_back();
    }
    if (trail.empty()) {
        arrive(creature);
        return;
    }
    const float speed = creature.creature.body->behavior->runSpeed;
    const NavStep next = m_world.navigation().next_step(creature.id, creature.position, trail.back(), speed * seconds);
    switch (next.kind) {
    case StepKind::moved:
        creature.creature.yaw = yaw_to(creature.position, next.position);
        mind.sentYaw = creature.creature.yaw;
        m_creatures.move(creature, next.position, Gait::run);
        ++m_counts.moves;
        break;
    case StepKind::waiting:
        break;
    case StepKind::arrived:
        trail.pop_back();
        break;
    case StepKind::noWay:
    case StepKind::noGround:
        if (trail.size() > 1) {
            trail.pop_back(); // A place with no way to it: the next older one.
            m_world.navigation().forget(creature.id);
            break;
        }
        // The anchor itself has no way: the creature is at its anchor in one step only when no
        // session can observe it. If not, it stands and tries again.
        if (!observed(creature)) {
            ++m_counts.jumps;
            log::write("world: creature %016llx (%s) has no way to its anchor and no session observes it: it is at its anchor in "
                       "one step",
                       id_number(creature.id),
                       creature.creature.kind->id.c_str());
            arrive(creature);
        }
        break;
    }
}

bool Behavior::observed(const Entity& creature) const {
    bool seen = false;
    const float leave = m_world.interest_leave(creature);
    m_world.for_each_character([&](const Entity& character) {
        seen = seen || (character.owner != 0 && horizontal_distance(character.position, creature.position) <= leave);
    });
    return seen;
}

void Behavior::record_trail(Entity& creature) {
    std::vector<state::WorldPosition>& trail = creature.creature.mind.trail;
    const state::WorldPosition& place = creature.position;
    if (!trail.empty() && horizontal_distance(trail.back(), place) < behavior_rules::kTrailSpacing) {
        return;
    }
    // A way that comes back near an older place of the trail: the places after that one are of
    // no use for the way back (the trail is a way with no loop).
    for (std::size_t index = 0; index + 2 < trail.size(); ++index) {
        if (horizontal_distance(trail[index], place) < behavior_rules::kTrailSpacing * 0.5F
            && std::fabs(trail[index].z - place.z) < behavior_rules::kContactHeight) {
            trail.resize(index + 1);
            return;
        }
    }
    if (trail.size() >= behavior_rules::kTrailLength) {
        trail.erase(trail.begin() + 1); // The anchor stays the first place.
    }
    trail.push_back(place);
}

void Behavior::arrive(Entity& creature) {
    CreatureMind& mind = creature.creature.mind;
    // The exact place of the anchor. At the spawn point: its direction too (the height of a
    // spawn point can be some centimeters from the terrain height).
    const bool atHome = horizontal_distance(mind.anchor, mind.home) < behavior_rules::kHomeDistance
                        && std::fabs(mind.anchor.z - mind.home.z) < behavior_rules::kContactHeight;
    if (atHome) {
        creature.creature.yaw = mind.homeYaw;
        mind.sentYaw = mind.homeYaw;
    }
    m_creatures.move(creature, mind.anchor, Gait::place);
    // The end of the way: the client plays the idle action of the creature again.
    m_creatures.stop(creature);
    m_creatures.restore(creature);
    mind.mode = CreatureMode::rest;
    mind.target = 0;
    mind.trail.clear();
    mind.retracing = false;
    mind.holdUntil.reset();
    // The roam goes on from the anchor: the waypoint of the route that the creature went to. A
    // creature that stayed at a waypoint for ever (`settled`) goes to that waypoint again and
    // plays its task again: the fight replaced the task (code review of stage 3.4e, finding F8).
    mind.roamGoal.reset();
    mind.roamWaitUntil = m_world.clock().now();
    mind.settled = false;
    m_creatures.immune_changed(creature);
    m_active.erase(creature.id);
    m_world.navigation().forget(creature.id);
    ++m_counts.returns;
    log::write("world: creature %016llx (%s) is at %s, health %.1f",
               id_number(creature.id),
               creature.creature.kind->id.c_str(),
               atHome ? "its home (its anchor)" : "its anchor",
               static_cast<double>(creature.vitals.health));
}

// ---- the roam: patrol and wander --------------------------------------------------------------

bool Behavior::roams(Entity& creature) {
    const Creature& body = creature.creature;
    if (body.dead || body.mind.settled || !body.body->behavior || !can_walk(creature)) {
        return false;
    }
    if (route_of(creature) != nullptr) {
        return true;
    }
    const gamedata::CreatureBehavior& values = *body.body->behavior;
    return values.wander && values.wanderFarthest > 0.0F;
}

void Behavior::select_roamers() {
    // From the characters, with the position index: the cost depends on the creatures near a
    // character, not on the creatures of the world.
    std::set<EntityId> selected;
    const float range = m_world.options().interestLeave;
    std::vector<state::WorldPosition> places;
    m_world.for_each_character([&](const Entity& character) { places.push_back(character.position); });
    for (const state::WorldPosition& place : places) {
        m_world.for_each_near(place, range, [&](Entity& creature) {
            if (creature.kind != EntityKind::creature || creature.creature.mind.mode != CreatureMode::rest
                || selected.count(creature.id) != 0
                || horizontal_distance(place, creature.position) > m_world.interest_leave(creature) || !roams(creature)) {
                return;
            }
            selected.insert(creature.id);
        });
    }
    for (const EntityId id : m_roaming) {
        if (selected.count(id) == 0) {
            if (Entity* creature = m_creatures.find(id)) {
                stand(*creature);
            }
        }
    }
    m_roaming = std::move(selected);
    m_counts.largestRoaming = std::max(m_counts.largestRoaming, m_roaming.size());
}

void Behavior::stand(Entity& creature) {
    m_creatures.stop(creature);
    m_world.navigation().forget(creature.id);
}

void Behavior::roam(Entity& creature, float seconds) {
    CreatureMind& mind = creature.creature.mind;
    const WorldClock::time_point now = m_world.clock().now();
    if (now < mind.roamWaitUntil) {
        return;
    }
    if (!mind.roamGoal && !next_roam_goal(creature)) {
        return;
    }
    const state::WorldPosition goal = *mind.roamGoal;
    // At the goal: near it in the plane and on its floor. A waypoint on a different floor is no
    // arrival: the navigation finds the way to it, or says that it has none (code review of
    // stage 3.4e, finding F2).
    if (at_place(creature.position, goal, behavior_rules::kRoamArrival)) {
        at_roam_goal(creature);
        return;
    }
    const float speed = ambient_speed_of(*creature.creature.body->behavior);
    const NavStep next = m_world.navigation().next_step(creature.id, creature.position, goal, speed * seconds);
    switch (next.kind) {
    case StepKind::moved:
        creature.creature.yaw = yaw_to(creature.position, next.position);
        mind.sentYaw = creature.creature.yaw;
        m_creatures.move(creature, next.position, Gait::walk);
        ++m_counts.moves;
        break;
    case StepKind::waiting:
        break;
    case StepKind::arrived:
        at_roam_goal(creature);
        break;
    case StepKind::noWay:
    case StepKind::noGround:
        // No way to the goal: the creature stands, and it takes its next goal after a pause.
        ++m_counts.roamNoWay;
        stand(creature);
        if (const gamedata::PatrolRoute* route = route_of(creature)) {
            next_waypoint(mind, *route);
        }
        mind.roamGoal.reset();
        mind.roamWaitUntil = now + behavior_rules::kRoamNoWayPause;
        break;
    }
}

bool Behavior::next_roam_goal(Entity& creature) {
    CreatureMind& mind = creature.creature.mind;
    if (const gamedata::PatrolRoute* route = route_of(creature)) {
        mind.waypoint = std::min<std::uint32_t>(mind.waypoint, static_cast<std::uint32_t>(route->points.size() - 1));
        mind.roamGoal = route->points[mind.waypoint].position;
        return true;
    }
    // The wander: a random place of the wander range near the home, on a floor. The range is
    // not larger than the leash of the slice at rest.
    const gamedata::CreatureBehavior& values = *creature.creature.body->behavior;
    float farthest = values.wanderFarthest;
    if (values.restLeash > 0.0F) {
        farthest = std::min(farthest, values.restLeash);
    }
    const float nearest = std::min(values.wanderNearest, farthest);
    constexpr float kTurn = 6.28318530718F;
    for (unsigned attempt = 0; attempt < behavior_rules::kWanderTries; ++attempt) {
        const float angle = m_world.random_unit() * kTurn;
        const float length = nearest + m_world.random_unit() * (farthest - nearest);
        state::WorldPosition place{mind.home.x + std::sin(angle) * length, mind.home.y + std::cos(angle) * length, mind.home.z};
        const std::optional<float> ground = m_world.navigation().ground_height(place);
        if (!ground) {
            continue;
        }
        place.z = *ground;
        if (m_world.navigation().footing(place) == Footing::rooted) {
            continue;
        }
        mind.roamGoal = place;
        return true;
    }
    ++m_counts.roamNoWay;
    mind.roamWaitUntil = m_world.clock().now() + behavior_rules::kRoamNoWayPause;
    return false;
}

void Behavior::at_roam_goal(Entity& creature) {
    CreatureMind& mind = creature.creature.mind;
    const WorldClock::time_point now = m_world.clock().now();
    // The end of the walk: the client plays the idle action of the creature.
    stand(creature);
    mind.roamGoal.reset();
    const gamedata::PatrolRoute* route = route_of(creature);
    if (route == nullptr) {
        ++m_counts.wanderPlaces;
        const auto shortest = static_cast<float>(behavior_rules::kWanderWaitShortest.count());
        const auto longest = static_cast<float>(behavior_rules::kWanderWaitLongest.count());
        mind.roamWaitUntil =
            now + std::chrono::milliseconds(static_cast<long long>(shortest + m_world.random_unit() * (longest - shortest)));
        return;
    }
    ++m_counts.waypoints;
    const gamedata::PatrolWaypoint& waypoint = route->points[mind.waypoint];
    if (waypoint.align) {
        turn(creature, waypoint.yaw);
    }
    // The wait: a time of the range of the waypoint. A negative value (-1 in the game files) is
    // no wait, but a wait for ever at the waypoint of a route of one waypoint and at a waypoint
    // whose task loops (rules of this project).
    float seconds = 0.0F;
    if (waypoint.minWait >= 0.0F) {
        seconds = waypoint.minWait + m_world.random_unit() * (waypoint.maxWait - waypoint.minWait);
    } else if (route->points.size() == 1 || loops(waypoint.task)) {
        mind.settled = true;
    }
    // The task of the waypoint, when the action table of the slice has an action with its name.
    if (!waypoint.task.empty() && (seconds > 0.0F || mind.settled)) {
        if (const std::optional<std::uint32_t> action = creature.creature.body->find_action(waypoint.task)) {
            m_creatures.start_action(creature, *action,
                                     mind.settled ? std::nullopt : std::optional<std::chrono::milliseconds>(milliseconds(seconds)));
        }
    }
    if (mind.settled) {
        m_roaming.erase(creature.id);
        return;
    }
    mind.roamWaitUntil = now + milliseconds(seconds);
    next_waypoint(mind, *route);
}

void Behavior::next_waypoint(CreatureMind& mind, const gamedata::PatrolRoute& route) {
    const auto count = static_cast<std::uint32_t>(route.points.size());
    if (count <= 1) {
        mind.waypoint = 0;
        return;
    }
    switch (route.policy) {
    case gamedata::PatrolPolicy::bounce: {
        if (mind.routeStep != 1 && mind.routeStep != -1) {
            mind.routeStep = 1;
        }
        const auto next = static_cast<std::int64_t>(mind.waypoint) + mind.routeStep;
        if (next < 0 || next >= static_cast<std::int64_t>(count)) {
            mind.routeStep = static_cast<std::int8_t>(-mind.routeStep);
        }
        mind.waypoint = static_cast<std::uint32_t>(static_cast<std::int64_t>(mind.waypoint) + mind.routeStep);
        break;
    }
    case gamedata::PatrolPolicy::random: {
        // A different waypoint of the route.
        auto other = static_cast<std::uint32_t>(m_world.random_unit() * static_cast<float>(count - 1));
        other = std::min(other, count - 2);
        mind.waypoint = other >= mind.waypoint ? other + 1 : other;
        break;
    }
    case gamedata::PatrolPolicy::loop:
    case gamedata::PatrolPolicy::undefined:
        mind.waypoint = (mind.waypoint + 1) % count;
        break;
    }
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
    m_world.health().fought(target);
    if (mind.holdUntil) {
        mind.holdUntil = now + behavior_rules::kNoWayHold; // A fight at the place nearest to the target goes on.
    }
    mind.attack = attackIndex;
    mind.attackGeneration = creature.creature.action->generation;
    mind.attackEnds = now + length;
    ++mind.attacks;
    ++m_counts.attacks;
    for (std::uint32_t index = 0; index < attack.contacts.size(); ++index) {
        if (attack.contacts[index].repeat) {
            continue; // The second hit volume of one swing: one hit.
        }
        m_contacts.emplace(now + milliseconds(attack.contacts[index].time),
                           PendingContact{creature.id, target.id, mind.attackGeneration, attackIndex, index});
    }
    log::write("world: creature %016llx (%s) starts the attack %s at %s (%.1f m, %zu contact(s), %.2f s)",
               id_number(creature.id),
               creature.creature.kind->id.c_str(),
               body.actions[attack.action].name.c_str(),
               target.name.c_str(),
               static_cast<double>(horizontal_distance(creature.position, target.position)),
               static_cast<std::size_t>(attack.hits),
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
    // The contact hurts only the character that the attack started at: a creature that got a
    // different target in its attack does not hurt that one with the old attack.
    Entity* target = fightable(pending.target);
    if (target == nullptr) {
        missed = "the target is not there";
    } else {
        away = horizontal_distance(creature->position, target->position);
        if (away > data.reach + behavior_rules::kContactTolerance
            || std::fabs(creature->position.z - target->position.z) > behavior_rules::kContactHeight) {
            missed = "the target is out of the reach";
        } else if (away > 0.3F && angle_to(creature->position, creature->creature.yaw, target->position) > behavior_rules::kContactAngle) {
            missed = "the target is not in front of the creature";
        } else if (!m_world.navigation().sees(creature->position, target->position)) {
            // A wall stops a contact too: the target can go behind a wall while the attack plays
            // (code review of stage 3.4d, finding F1).
            missed = "a wall is between the creature and its target";
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

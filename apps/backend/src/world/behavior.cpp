#include "world/behavior.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <vector>

#include "core/logging/log.h"
#include "world/creatures.h"
#include "world/projectiles.h"
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

/** The named action of a node that the slice of the creature does not have (stage BT2): the leaf fails. -1: the node names no action. */
constexpr std::int32_t kMissingAction = -2;

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

Behavior::Behavior(World& world, Creatures& creatures) : m_world(world), m_creatures(creatures) {
    tree::LoadReport report;
    (void)reload_trees(m_world.options().behaviorDirectory, report);
}

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

void Behavior::moved_by_force(Entity& creature) {
    // The path stays (user test 2, UT2-4): the navigation follows it when the creature is near it (navigation_rules::kOffPath), else
    // it takes the free straight line at once or searches. The step after the push comes in the next step of the behavior.
    if (creature.creature.mind.mode == CreatureMode::fight) {
        record_trail(creature);
    }
}

Entity* Behavior::fightable(EntityId id) {
    Entity* target = id != 0 ? m_world.find(id) : nullptr;
    // A summon with health (stage 3.3b-S2b: the Pylon) is a target too, until its end chain.
    if (target != nullptr && target->kind == EntityKind::spell) {
        return target->spell.vitalsRow != nullptr && !target->spell.ending && target->vitals.health > 0.0F ? target : nullptr;
    }
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
    // The tree of the creature (stage BT2: its binding).
    bind(creature);
    // The creatures of one place do not all start their roam at one time: a random part of the shortest wait of the roam of its tree.
    const std::shared_ptr<const tree::CompiledTree>& own = creature.creature.mind.brain.tree;
    const float* roamValues = own && own->first(tree::NodeKind::roam) != nullptr ? own->first(tree::NodeKind::roam)
                              : m_default                                       ? m_default->first(tree::NodeKind::roam)
                                                                                : nullptr;
    const float shortest = roamValues != nullptr ? roam_values(roamValues).waitShortest : 0.0F;
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
    mind.shooting = false;
    mind.retreating = false;
    mind.closing = false;
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

std::uint32_t Behavior::next_attack(const Creature& creature, std::uint32_t lightBeforeHeavy) {
    const gamedata::CreatureSlice& body = *creature.body;
    const std::uint32_t count = creature.mind.attacks;
    if (body.lightAttacks.empty() || body.heavyAttacks.empty()) {
        return static_cast<std::uint32_t>(count % body.attacks.size());
    }
    // A cycle: `lightBeforeHeavy` light attacks, then one heavy attack.
    const std::uint32_t cycle = count / (lightBeforeHeavy + 1);
    const std::uint32_t place = count % (lightBeforeHeavy + 1);
    if (place < lightBeforeHeavy) {
        return body.lightAttacks[(cycle * lightBeforeHeavy + place) % body.lightAttacks.size()];
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
    // A shot whose release did not come does not fly (code review of stage 3.4f-A, finding CR-R2: a travel of the target).
    mind.shooting = false;
    mind.target = 0;
    mind.mode = CreatureMode::home;
    mind.blockedSteps = 0;
    mind.retreating = false;
    mind.closing = false;
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
    // The releases of the shots that play (stage 3.4f-A), at their times.
    while (!m_releases.empty() && m_releases.begin()->first <= now) {
        const PendingRelease pending = m_releases.begin()->second;
        m_releases.erase(m_releases.begin());
        release(pending);
    }
    // The casts of the area attacks that play (stage 3.4f-C), at their times.
    while (!m_casts.empty() && m_casts.begin()->first <= now) {
        const PendingCast pending = m_casts.begin()->second;
        m_casts.erase(m_casts.begin());
        cast(pending);
    }
    // The marks of the status actions that play (stage 3.4f-D), at their times.
    while (!m_statusMarks.empty() && m_statusMarks.begin()->first <= now) {
        const PendingStatus pending = m_statusMarks.begin()->second;
        m_statusMarks.erase(m_statusMarks.begin());
        status_mark(pending);
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
            // The way back is a rule of the engine: no node of the tree runs in this step.
            CreatureBrain& brain = creature->creature.mind.brain;
            brain.at = m_world.clock().now();
            brain.cause = StepCause::way_back;
            brain.decided = 0;
            std::fill(brain.last.begin(), brain.last.end(), std::uint8_t{0});
            go_home(*creature, seconds);
        } else {
            decide(*creature, seconds);
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
        run_tree(*creature, seconds);
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

bool Behavior::hit_stunned(Entity& creature, WorldClock::time_point now) {
    if (!creature.creature.hitStunUntil) {
        return false;
    }
    if (now < *creature.creature.hitStunUntil) {
        return true;
    }
    creature.creature.hitStunUntil.reset();
    return false;
}

StepCause Behavior::engine_holds(Entity& creature, WorldClock::time_point now) {
    CreatureMind& mind = creature.creature.mind;
    // A stun or a knockdown of an ability holds the creature: no movement and no attack (stage
    // 3.3a). It keeps its target. A forced movement (a pull, a push; stage 3.3b-S4) moves it: the
    // behavior waits for its end.
    if (creature.creature.held(now) || creature.creature.held_by_force()) {
        return StepCause::held;
    }
    // The hit stun of the last hit (user test 2, UT2-9: HitStun_AI of the damage row): the reaction plays; no step, no turn, no attack.
    if (hit_stunned(creature, now)) {
        return StepCause::hit_stun;
    }
    if (mind.attack || mind.shooting) {
        // An attack or a shot plays: the creature stays at its place and keeps its direction to its end.
        if (now < mind.attackEnds) {
            return StepCause::action;
        }
        const bool shot = mind.shooting;
        mind.attack.reset();
        mind.shooting = false;
        // The pause of the node that started the action (its `pause`, or the `heavy_pause` of a heavy melee attack; stage BT1).
        mind.ready = now + mind.actionPause;
        // After a shot its own ready time holds for the next shot (the repeat of its band), and the pause for a melee attack. After a
        // melee attack the next shot waits for its pause too.
        if (!shot) {
            mind.shotReady = std::max(mind.shotReady, mind.ready);
        }
    }
    return StepCause::tree;
}

void Behavior::decide(Entity& creature, float seconds) {
    CreatureBrain& brain = creature.creature.mind.brain;
    const WorldClock::time_point now = m_world.clock().now();
    brain.at = now;
    brain.decided = 0;
    brain.cause = engine_holds(creature, now);
    if (brain.cause != StepCause::tree) {
        std::fill(brain.last.begin(), brain.last.end(), std::uint8_t{0});
        return;
    }
    run_tree(creature, seconds);
}

bool Behavior::keep_target(Entity& creature, float loseDistance) {
    CreatureMind& mind = creature.creature.mind;
    const gamedata::CreatureSlice& body = *creature.creature.body;
    Entity* target = step_target(creature);
    if (target == nullptr) {
        const Entity* gone = mind.target != 0 ? m_world.find(mind.target) : nullptr;
        lose_target(creature, gone == nullptr                     ? "the target left the world"
                              : gone->kind == EntityKind::spell   ? "the summon ends"
                              : gone->life.dead                   ? "the target is dead"
                                                                  : "the creatures ignore the target");
        return false;
    }
    const gamedata::CreatureBehavior& values = *body.behavior;
    // The leash is a distance from the anchor: the place where the creature took its target.
    const float fromAnchor = horizontal_distance(creature.position, mind.anchor);
    if (fromAnchor > leash_of(values)) {
        lose_target(creature, "the creature is at the end of its leash");
        return false;
    }
    const float away = horizontal_distance(creature.position, target->position);
    const bool walks = can_walk(creature);
    const float farthest = walks ? loseDistance : (values.combatDistance > 0.0F ? values.combatDistance : behavior_rules::kDefaultCombatSight);
    if (away > farthest) {
        lose_target(creature, "the target is too far");
        return false;
    }
    return true;
}

void Behavior::melee(Entity& creature, Entity& target, float seconds, const MeleeValues& values) {
    CreatureMind& mind = creature.creature.mind;
    const gamedata::CreatureSlice& body = *creature.creature.body;
    const WorldClock::time_point now = m_world.clock().now();
    const gamedata::CreatureBehavior& data = *body.behavior;
    const float away = horizontal_distance(creature.position, target.position);
    const bool walks = can_walk(creature);
    // A root of an ability keeps the creature at its place: it turns and attacks in its reach.
    const bool rooted = creature.creature.rooted(now);
    // The next attack of the slice: its light attacks in their sequence, and its heavy attacks between them.
    const std::uint32_t attackIndex = next_attack(creature.creature, values.lightBeforeHeavy);
    const gamedata::CreatureAttack& attack = body.attacks[attackIndex];
    const float reach = attack.contacts.front().reach;
    // A creature that walks goes some distance into the reach of its attack. A creature that does
    // not leave its place attacks a target that is in the reach.
    const float stop = walks && !rooted ? std::max(reach - behavior_rules::kReachMargin, behavior_rules::kNearest) : reach;
    const bool level = std::fabs(creature.position.z - target.position.z) <= behavior_rules::kContactHeight;
    const bool near = away <= stop + 0.05F && level;
    // A wall between the creature and its target stops the attack too: the creature then goes on its path.
    const bool sees = (near || away - stop < data.runSpeed * seconds + behavior_rules::kCrowdRange || away <= reach) && level
                      && m_world.navigation().sees(creature.position, target.position);
    if (near && sees) {
        turn_to(creature, target.position);
        if (now >= mind.ready) {
            start_attack(creature, target, attackIndex, values);
        }
        return;
    }
    if (!walks || rooted) {
        turn_to(creature, target.position);
        return;
    }
    // One step on the path to the target. The navigation gives the place and the height of its
    // floor. The step ends where the target is in the reach, when the line to the target is free.
    // A slow of an ability lowers the speed (Creature::speed_factor).
    float way = data.runSpeed * seconds * creature.creature.speed_factor(now);
    if (sees && away - stop < way + behavior_rules::kCrowdRange) {
        // The last steps, on the free line to the target. The step ends where the target is in
        // the reach. A different creature at its end makes it shorter: the creature then
        // attacks from its place when the target is in the reach of the attack.
        const float wanted = std::min(way, std::max(away - stop, 0.05F));
        way = free_way(creature, target, wanted);
        if (way < wanted) {
            ++m_counts.crowded;
            if (way <= 0.05F) {
                // The rule of the crowd: the creature waits for a creature with the same target,
                // and for a different creature (one with no target, a kind that does not fight)
                // for `kCrowdPatience` steps; then it goes past it (code review of stage 3.4d,
                // finding F7: two creatures waited for each other for all time).
                const Entity* other = blocker(creature, target, wanted);
                if (other == nullptr || other->creature.mind.target == mind.target || mind.blockedSteps < behavior_rules::kCrowdPatience) {
                    ++mind.blockedSteps;
                    turn_to(creature, target.position);
                    if (away <= reach && now >= mind.ready) {
                        start_attack(creature, target, attackIndex, values);
                    }
                    return;
                }
                way = wanted;
            }
        }
    }
    // The way to the target, or to the place nearest to it when it has no way (`nearest`).
    NavStep next = m_world.navigation().next_step(creature.id, creature.position, target.position, way, true);
    // A different creature at the end of the step: a shorter step, so that the creatures of a
    // queue stand near each other.
    for (float shorter = way * 0.5F; next.kind == StepKind::moved && shorter >= 0.2F && taken(creature, next.position) != nullptr;
         shorter *= 0.5F) {
        m_world.navigation().take_back(creature.id);
        next = m_world.navigation().next_step(creature.id, creature.position, target.position, shorter, true);
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
                       target.name.c_str(),
                       static_cast<double>(away));
        } else if (now >= *mind.holdUntil) {
            no_way(creature, target);
            return;
        }
    } else if (next.kind == StepKind::moved) {
        // A step on a path ends a hold: on a full path the creature has a way again, and on a
        // partial path it goes to a new nearest place, where a new hold starts (code review of
        // stage 3.5a, finding F7: a hold of an old nearest place ended the wait at the new one).
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
            turn_to(creature, target.position);
            if (sees && away <= reach && now >= mind.ready) {
                start_attack(creature, target, attackIndex, values);
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
        turn_to(creature, target.position);
        break;
    case StepKind::arrived:
    case StepKind::noWay:
        // The end of the path, and the target is not in the reach: the target is at a place with
        // no way to it. The creature waits at its place (the place nearest to the target), turns
        // to it, and attacks it when it comes into the reach.
        turn_to(creature, target.position);
        if (away <= reach && level && now >= mind.ready && m_world.navigation().sees(creature.position, target.position)) {
            start_attack(creature, target, attackIndex, values);
        }
        break;
    case StepKind::noGround:
        mind.walks = false;
        mind.groundKnown = true;
        turn_to(creature, target.position);
        break;
    }
}

void Behavior::go_home(Entity& creature, float seconds) {
    CreatureMind& mind = creature.creature.mind;
    const WorldClock::time_point now = m_world.clock().now();
    if (mind.attack || mind.shooting) {
        if (now < mind.attackEnds) {
            return; // The attack or the shot that plays ends first.
        }
        mind.attack.reset();
        mind.shooting = false;
    }
    if (creature.creature.held_by_force()) {
        return; // A forced movement that started before the way back ends first (the movement system ends it).
    }
    if (hit_stunned(creature, now)) {
        return; // The hit stun of a hit before the way back holds (code review of user test 2, finding U2-R5).
    }
    // At the anchor: near it in the plane and on its floor. A creature below or above its anchor
    // on a different floor goes there on its way, and it does not jump while a session observes
    // it (code review of stage 3.4e, finding F1).
    if (at_place(creature.position, mind.anchor, behavior_rules::kHomeDistance)) {
        arrive(creature);
        return;
    }
    // A stun, a knockdown, or a root of an ability: the creature waits at its place. It is not at
    // its anchor (code review of stage 3.5a, finding F6).
    if (creature.creature.held(now) || creature.creature.rooted(now)) {
        return;
    }
    if (!can_walk(creature)) {
        // A creature that does not walk (no free floor at its place) and that is not at its anchor:
        // it is at its anchor in one step only when no session can observe it; if not, it waits
        // (code review of stage 3.5a, finding F6: a jump through a ceiling in front of a client).
        if (!observed(creature)) {
            ++m_counts.jumps;
            log::write("world: creature %016llx (%s) does not walk and no session observes it: it is at its anchor in one step",
                       id_number(creature.id),
                       creature.creature.kind->id.c_str());
            arrive(creature);
        }
        return;
    }
    if (mind.retracing) {
        retrace(creature, seconds);
        return;
    }
    const float speed = creature.creature.body->behavior->runSpeed * creature.creature.speed_factor(now);
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
    const float speed = creature.creature.body->behavior->runSpeed * creature.creature.speed_factor(m_world.clock().now());
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

bool Behavior::spawn_lead_over(const Entity& creature, WorldClock::time_point now) {
    return !creature.creature.spawnAction
           || now >= std::max(creature.creature.madeAt, creature.creature.recordedAt) + behavior_rules::kSpawnLead;
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

void Behavior::roam(Entity& creature, float seconds, const RoamValues& values) {
    CreatureMind& mind = creature.creature.mind;
    const WorldClock::time_point now = m_world.clock().now();
    // An effect of an ability that holds the creature or keeps it at its place stops its walk too, and the hit stun of a hit (a passive
    // creature takes no target: its reaction plays to its hit stun; code review of user test 2, finding U2-R5).
    if (now < mind.roamWaitUntil || creature.creature.held(now) || creature.creature.rooted(now) || creature.creature.held_by_force()
        || hit_stunned(creature, now)) {
        return;
    }
    if (!mind.roamGoal && !next_roam_goal(creature, values)) {
        return;
    }
    const state::WorldPosition goal = *mind.roamGoal;
    // At the goal: near it in the plane and on its floor. A waypoint on a different floor is no
    // arrival: the navigation finds the way to it, or says that it has none (code review of
    // stage 3.4e, finding F2).
    if (at_place(creature.position, goal, behavior_rules::kRoamArrival)) {
        at_roam_goal(creature, values);
        return;
    }
    const float speed = ambient_speed_of(*creature.creature.body->behavior) * creature.creature.speed_factor(now);
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
        at_roam_goal(creature, values);
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
        mind.roamWaitUntil = now + values.noWayPause;
        break;
    }
}

bool Behavior::next_roam_goal(Entity& creature, const RoamValues& roamValues) {
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
    mind.roamWaitUntil = m_world.clock().now() + roamValues.noWayPause;
    return false;
}

void Behavior::at_roam_goal(Entity& creature, const RoamValues& values) {
    CreatureMind& mind = creature.creature.mind;
    const WorldClock::time_point now = m_world.clock().now();
    // The end of the walk: the client plays the idle action of the creature.
    stand(creature);
    mind.roamGoal.reset();
    const gamedata::PatrolRoute* route = route_of(creature);
    if (route == nullptr) {
        ++m_counts.wanderPlaces;
        const float shortest = values.waitShortest;
        const float longest = values.waitLongest;
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

void Behavior::start_attack(Entity& creature, Entity& target, std::uint32_t attackIndex, const MeleeValues& values) {
    CreatureMind& mind = creature.creature.mind;
    const gamedata::CreatureSlice& body = *creature.creature.body;
    const gamedata::CreatureAttack& attack = body.attacks[attackIndex];
    const WorldClock::time_point now = m_world.clock().now();
    // The creature looks at its target for the whole attack.
    creature.creature.yaw = yaw_to(creature.position, target.position);
    mind.sentYaw = creature.creature.yaw;
    const std::chrono::milliseconds length = milliseconds(attack.length);
    if (!m_creatures.start_action(creature, attack.action, length)) {
        mind.ready = now + values.pause;
        return;
    }
    m_world.health().fought(target);
    if (mind.holdUntil) {
        mind.holdUntil = now + behavior_rules::kNoWayHold; // A fight at the place nearest to the target goes on.
    }
    mind.actionPause = attack.heavy ? values.heavyPause : values.pause;
    mind.attack = attackIndex;
    mind.attackGeneration = creature.creature.action->generation;
    mind.attackEnds = now + length;
    ++mind.attacks;
    ++m_counts.attacks;
    // Stage U3 (research Q1): the Homing clips of the attack move and turn the creature on the world clock (world/attack_motion.h).
    m_creatures.attack_motion().start(creature, target.id, attackIndex);
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
    if (target->kind == EntityKind::spell) {
        m_world.spells().creature_hit(*creature, data, *target); // A summon (stage 3.3b-S2b).
        return;
    }
    m_world.health().creature_hit(*creature, data, *target);
}

Behavior::MeleeValues Behavior::melee_values(const float* values) {
    return MeleeValues{static_cast<std::uint32_t>(values[tree::param::kLightBeforeHeavy]), milliseconds(values[tree::param::kMeleePause]),
                       milliseconds(values[tree::param::kHeavyPause])};
}

Behavior::ShotValues Behavior::shot_values(const gamedata::CreatureSlice& body, const float* values, std::uint32_t lightBeforeHeavy) {
    // The band of a caster for a slice whose basic shot is a cast (stage 3.4f-B), else the band of an archer.
    const float* band = values + (body.caster ? tree::param::kCaster : tree::param::kArcher);
    return ShotValues{ShotBand{band[tree::param::kBandFar], band[tree::param::kBandHold], band[tree::param::kBandNear],
                               band[tree::param::kBandMiddle], milliseconds(band[tree::param::kBandRepeat])},
                      milliseconds(values[tree::param::kRetreatPause]), milliseconds(values[tree::param::kShotPause]), lightBeforeHeavy};
}

Behavior::AreaValues Behavior::area_values(const float* values) {
    return AreaValues{milliseconds(values[tree::param::kAreaRepeat]), milliseconds(values[tree::param::kAreaOnlyRepeat]),
                      values[tree::param::kAreaHoldMargin], milliseconds(values[tree::param::kAreaPause])};
}

Behavior::StatusValues Behavior::status_values(const float* values) {
    return StatusValues{milliseconds(values[tree::param::kStatusRepeat]), milliseconds(values[tree::param::kStatusPause])};
}

Behavior::RoamValues Behavior::roam_values(const float* values) {
    // The waits in milliseconds as floats: the random wait of a wander is the same number as with the constants before stage BT2.
    return RoamValues{values[tree::param::kWaitShortest] * 1000.0F, values[tree::param::kWaitLongest] * 1000.0F,
                      milliseconds(values[tree::param::kNoWayPause])};
}

std::int32_t Behavior::named_action(const Entity& creature, const float* values, std::size_t place) {
    const float slot = values[place];
    if (slot < 0.0F) {
        return -1;
    }
    const std::vector<std::int32_t>& actions = creature.creature.mind.brain.actions;
    const auto index = static_cast<std::size_t>(slot);
    return index < actions.size() ? actions[index] : kMissingAction;
}

bool Behavior::think_shot(Entity& creature, Entity& target, std::uint32_t shot, float seconds, const ShotValues& shotValues) {
    CreatureMind& mind = creature.creature.mind;
    const gamedata::CreatureSlice& body = *creature.creature.body;
    const gamedata::CreatureBehavior& values = *body.behavior;
    const WorldClock::time_point now = m_world.clock().now();
    const ShotBand& band = shotValues.band;
    const float away = horizontal_distance(creature.position, target.position);
    const float rise = std::fabs(creature.position.z - target.position.z);
    // The melee attack wins in its reach (research creature-ranged.md Q3, the first option): the next melee attack of the slice.
    if (!body.attacks.empty()) {
        const gamedata::CreatureAttack& melee = body.attacks[next_attack(creature.creature, shotValues.lightBeforeHeavy)];
        if (away <= melee.contacts.front().reach + behavior_rules::kContactTolerance && rise <= behavior_rules::kContactHeight) {
            mind.retreating = false;
            return false;
        }
    }
    const bool walks = can_walk(creature) && !creature.creature.rooted(now);
    const bool sees = rise <= behavior_rules::kShotHeight && m_world.navigation().sees(creature.position, target.position);
    // A creature that walks into its band from farther walks on to the hold distance (`hold` of its band: some steps of the target
    // need no new walk; stage 3.4f-B, the rule of the research Q3 that the code of stage 3.4f-A did not keep: it stopped at the edge).
    const bool inBand = sees && away <= (mind.closing && walks ? band.hold : band.outer);
    // The walk back to the middle of the band: on a path, inside the leash, one time in the retreat pause of the node.
    if (inBand && walks && !mind.retreating && away < band.inner && now >= mind.retreatPauseUntil) {
        mind.retreating = true;
        m_world.navigation().forget(creature.id);
        ++m_counts.retreats;
        log::write("world: creature %016llx (%s) walks back from %s (%.1f m) to %.0f m for its shot",
                   id_number(creature.id), creature.creature.kind->id.c_str(), target.name.c_str(), static_cast<double>(away),
                   static_cast<double>(band.middle));
    }
    if (mind.retreating) {
        const float size = std::max(away, 0.01F);
        const state::WorldPosition goal{target.position.x + (creature.position.x - target.position.x) / size * band.middle,
                                        target.position.y + (creature.position.y - target.position.y) / size * band.middle,
                                        creature.position.z};
        const bool inLeash = horizontal_distance(goal, mind.anchor) <= leash_of(values);
        NavStep next;
        if (inLeash && away < band.middle - 0.5F && walks) {
            next = m_world.navigation().next_step(creature.id, creature.position, goal,
                                                  values.runSpeed * seconds * creature.creature.speed_factor(now), false);
        } else {
            next.kind = StepKind::arrived;
        }
        if (next.kind == StepKind::moved && taken(creature, next.position) == nullptr) {
            creature.creature.yaw = yaw_to(creature.position, next.position);
            mind.sentYaw = creature.creature.yaw;
            m_creatures.move(creature, next.position, Gait::run);
            record_trail(creature);
            ++m_counts.moves;
            return true;
        }
        if (next.kind == StepKind::waiting) {
            return true;
        }
        // At the middle, at the end of the leash, or no path back: the creature shoots from its place, and does not walk back again for
        // the retreat pause.
        mind.retreating = false;
        mind.retreatPauseUntil = now + shotValues.retreatPause;
        m_world.navigation().forget(creature.id);
    }
    if (inBand) {
        mind.closing = false;
        turn_to(creature, target.position);
        // A creature whose slice needs the spawn action waits until each session that saw it come shows the action that plays
        // (code review of stage 3.4f-A, finding CR-R1), also a session whose first record of an old creature is new (code review of
        // stage 3.4f-B, finding CS-R4).
        const bool shown = spawn_lead_over(creature, now);
        if (now >= mind.shotReady && shown) {
            start_shot(creature, target, shot, shotValues);
        }
        return true;
    }
    if (!walks) {
        turn_to(creature, target.position);
        return true;
    }
    // Farther than the band, or no free line: the walk to the target on its path, to the hold distance with a free line.
    mind.closing = true;
    walk_to(creature, target, band.hold, sees, away, seconds);
    return true;
}

void Behavior::walk_to(Entity& creature, Entity& target, float hold, bool sees, float away, float seconds) {
    CreatureMind& mind = creature.creature.mind;
    const gamedata::CreatureBehavior& values = *creature.creature.body->behavior;
    const WorldClock::time_point now = m_world.clock().now();
    const float way = values.runSpeed * seconds * creature.creature.speed_factor(now);
    NavStep next = m_world.navigation().next_step(creature.id, creature.position, target.position, way, true);
    if (next.kind == StepKind::moved && sees && away - way < hold) {
        // The step ends in the band: a shorter step to the hold distance.
        m_world.navigation().take_back(creature.id);
        next = m_world.navigation().next_step(creature.id, creature.position, target.position, std::max(away - hold, 0.05F), true);
    }
    if (next.kind == StepKind::arrived || next.kind == StepKind::noWay) {
        // The rule of a target with no way (the melee rule): the place nearest to it, a wait, then the way back.
        if (!mind.holdUntil) {
            mind.holdUntil = now + behavior_rules::kNoWayHold;
            ++m_counts.holds;
        } else if (now >= *mind.holdUntil) {
            no_way(creature, target);
            return;
        }
        turn_to(creature, target.position);
        return;
    }
    if (next.kind == StepKind::moved) {
        mind.holdUntil.reset();
        if (taken(creature, next.position) != nullptr) {
            m_world.navigation().take_back(creature.id);
            ++m_counts.crowded;
            turn_to(creature, target.position);
            return;
        }
        creature.creature.yaw = yaw_to(creature.position, next.position);
        mind.sentYaw = creature.creature.yaw;
        m_creatures.move(creature, next.position, Gait::run);
        record_trail(creature);
        ++m_counts.moves;
        return;
    }
    if (next.kind == StepKind::noGround) {
        mind.walks = false;
        mind.groundKnown = true;
    }
    turn_to(creature, target.position);
}

void Behavior::start_shot(Entity& creature, Entity& target, std::uint32_t shot, const ShotValues& values) {
    CreatureMind& mind = creature.creature.mind;
    const gamedata::CreatureSlice& body = *creature.creature.body;
    const gamedata::CreatureRangedAttack& attack = body.ranged[shot];
    const WorldClock::time_point now = m_world.clock().now();
    // The creature looks at its target for the whole action (the aim angle of the data limits the release, Behavior::aim).
    creature.creature.yaw = yaw_to(creature.position, target.position);
    mind.sentYaw = creature.creature.yaw;
    const std::chrono::milliseconds length = milliseconds(attack.length);
    if (!m_creatures.start_action(creature, attack.action, length)) {
        mind.shotReady = now + values.pause;
        return;
    }
    m_world.health().fought(target);
    if (mind.holdUntil) {
        mind.holdUntil = now + behavior_rules::kNoWayHold;
    }
    mind.shooting = true;
    mind.actionPause = values.pause;
    mind.attackGeneration = creature.creature.action->generation;
    mind.attackEnds = now + length;
    mind.shotReady = std::max(now + values.band.repeat, mind.attackEnds);
    ++mind.attacks;
    ++m_counts.shots;
    for (std::uint32_t index = 0; index < attack.releases.size(); ++index) {
        m_releases.emplace(now + milliseconds(attack.releases[index].time),
                           PendingRelease{creature.id, target.id, mind.attackGeneration, shot, index});
    }
    log::write("world: creature %016llx (%s) starts the shot %s at %s (%.1f m, %zu release(s) from %.2f s, %.2f s)",
               id_number(creature.id),
               creature.creature.kind->id.c_str(),
               body.actions[attack.action].name.c_str(),
               target.name.c_str(),
               static_cast<double>(horizontal_distance(creature.position, target.position)),
               attack.releases.size(),
               static_cast<double>(attack.releases.front().time),
               static_cast<double>(attack.length));
}

bool Behavior::command_shot(Entity& creature, EntityId target) {
    const gamedata::CreatureSlice& body = *creature.creature.body;
    Entity* character = fightable(target);
    // The values of a basic_shot node of its tree or of the default tree: a tree with no such node (a reload of a folder) refuses the command
    // before a change of the creature (code review of stage BT1, finding R4).
    const float* values = command_values(creature, tree::NodeKind::basic_shot);
    if (creature.creature.dead || !body.basicShot || character == nullptr || character->kind != EntityKind::player || values == nullptr
        || creature.creature.held(m_world.clock().now()) || creature.creature.held_by_force()) {
        return false;
    }
    // The creature takes the character as its target (the step of the behavior then ends the action of the shot, and the fight goes on
    // by the rules of the behavior).
    CreatureMind& mind = creature.creature.mind;
    if (mind.mode != CreatureMode::fight || mind.target != character->id) {
        take_target(creature, *character, "a command shot", horizontal_distance(creature.position, character->position));
    }
    start_shot(creature, *character, *body.basicShot, shot_values(body, values, cadence_of(creature)));
    return mind.shooting;
}

ShotAim Behavior::aim(const Entity& creature, const Entity& target, const gamedata::RangedRelease& release) const {
    using Vector = std::array<float, 3>;
    const auto length = [](const Vector& v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); };
    const auto unit = [&](const Vector& v) {
        const float size = length(v);
        return size > 1.0e-6F ? Vector{v[0] / size, v[1] / size, v[2] / size} : Vector{0.0F, 0.0F, 0.0F};
    };
    const float yaw = creature.creature.yaw * kPi / 180.0F;
    const Vector front{-std::sin(yaw), std::cos(yaw), 0.0F};
    ShotAim result;
    result.origin = {creature.position.x + front[0] * behavior_rules::kMuzzleForward, creature.position.y + front[1] * behavior_rules::kMuzzleForward,
                     creature.position.z + behavior_rules::kMuzzleHeight};
    // The point of the aim: the middle of the capsule of the target (the player slice, game data; a decision of this project).
    const gamedata::CreatureData* data = m_creatures.data();
    const gamedata::CreatureCapsule* capsule = data != nullptr && data->player_capsule() ? &*data->player_capsule() : nullptr;
    const float middle = capsule != nullptr ? capsule->radius + capsule->height * 0.5F : 0.9F;
    state::WorldPosition point{target.position.x, target.position.y, target.position.z + middle};
    const auto toward = [&](const state::WorldPosition& p) { return Vector{p.x - result.origin.x, p.y - result.origin.y, p.z - result.origin.z}; };
    const Vector direct = unit(toward(point));
    // The lead (AILeadTarget of the clip): a constant-velocity intercept with the aim speed of the clip (ProjectileSpeed), at most the
    // lead angle of the clip (AILeadTargetMaxAngle) from the direct line. The velocity of the target at the release only.
    if (release.lead && release.aimSpeed > 0.0F) {
        const Vector velocity = m_world.velocity_of(target);
        state::WorldPosition lead = point;
        for (int pass = 0; pass < 3; ++pass) {
            const float time = length(toward(lead)) / release.aimSpeed;
            lead = {point.x + velocity[0] * time, point.y + velocity[1] * time, point.z + velocity[2] * time};
        }
        const Vector wanted = unit(toward(lead));
        const float cosine = std::clamp(direct[0] * wanted[0] + direct[1] * wanted[1] + direct[2] * wanted[2], -1.0F, 1.0F);
        const float angle = std::acos(cosine);
        const float limit = release.leadAngle * kPi / 180.0F;
        if (angle > limit && angle > 1.0e-6F) {
            // The direction at the limit angle from the direct line, in the plane of the two.
            const Vector side = unit({wanted[0] - direct[0] * cosine, wanted[1] - direct[1] * cosine, wanted[2] - direct[2] * cosine});
            const Vector capped{direct[0] * std::cos(limit) + side[0] * std::sin(limit), direct[1] * std::cos(limit) + side[1] * std::sin(limit),
                                direct[2] * std::cos(limit) + side[2] * std::sin(limit)};
            const float distance = length(toward(lead));
            lead = {result.origin.x + capped[0] * distance, result.origin.y + capped[1] * distance, result.origin.z + capped[2] * distance};
        }
        point = lead;
    }
    result.point = point;
    // The fall of the projectile slice (game data: its acceleration after the delay distance of the attack; live check L2b: the client
    // flies it so): the aim is higher by the fall in the time of the flight.
    state::WorldPosition aimed = point;
    if (data != nullptr && release.projectile < data->projectiles().size()) {
        const gamedata::ProjectileSliceData& slice = data->projectiles()[release.projectile];
        const float distance = length(toward(point));
        const float time = std::max(distance - release.accelDelay, 0.0F) / slice.speed;
        aimed = {point.x - 0.5F * slice.accel[0] * time * time, point.y - 0.5F * slice.accel[1] * time * time,
                 point.z - 0.5F * slice.accel[2] * time * time};
    }
    Vector direction = unit(toward(aimed));
    // The aim angle of the clip (AIAimMaxAngle): at most this angle to each side of the front of the creature, in the plane.
    const float flat = std::hypot(direction[0], direction[1]);
    if (flat > 1.0e-6F && release.aimAngle < 180.0F) {
        const float side = std::atan2(front[0] * direction[1] - front[1] * direction[0], front[0] * direction[0] + front[1] * direction[1]);
        const float limit = release.aimAngle * kPi / 180.0F;
        if (std::fabs(side) > limit) {
            const float turned = std::copysign(limit, side);
            const float x = front[0] * std::cos(turned) - front[1] * std::sin(turned);
            const float y = front[0] * std::sin(turned) + front[1] * std::cos(turned);
            direction = {x * flat, y * flat, direction[2]};
        }
    }
    result.direction = unit(direction);
    return result;
}

void Behavior::release(const PendingRelease& pending) {
    Entity* creature = m_creatures.find(pending.creature);
    const char* cancelled = nullptr;
    if (creature == nullptr || creature->creature.dead) {
        cancelled = "the creature is dead or not in the world";
    } else if (!creature->creature.mind.shooting || creature->creature.mind.attackGeneration != pending.generation
               || !creature->creature.action || creature->creature.action->generation != pending.generation) {
        cancelled = "a different action replaced the shot";
    } else if (creature->creature.mind.mode != CreatureMode::fight || creature->creature.mind.target != pending.target) {
        // The creature lost the target of the shot before the release (a travel, the shelter, the leash): no shot (code review of stage
        // 3.4f-A, finding CR-R2).
        cancelled = "the creature lost the target of the shot";
    }
    Entity* target = cancelled == nullptr ? fightable(pending.target) : nullptr;
    if (cancelled == nullptr && (target == nullptr || target->kind != EntityKind::player)) {
        cancelled = "the target is not there";
    }
    if (cancelled != nullptr) {
        ++m_counts.releasesCancelled;
        log::write("world: no shot: the release of creature %016llx does not fly: %s", id_number(pending.creature), cancelled);
        return;
    }
    const gamedata::RangedRelease& data = creature->creature.body->ranged[pending.attack].releases[pending.release];
    const ShotAim shot = aim(*creature, *target, data);
    if (m_world.projectiles().launch_creature(CreatureShot{creature->id, &data, shot.origin, shot.direction, target->id})) {
        ++m_counts.releases;
    } else {
        ++m_counts.releasesCancelled;
    }
}

std::vector<std::uint32_t> Behavior::usable_areas(const gamedata::CreatureSlice& body) const {
    std::vector<std::uint32_t> usable;
    for (std::uint32_t index = 0; index < body.areas.size(); ++index) {
        const gamedata::CreatureAreaAttack& area = body.areas[index];
        // Each cast has the data of its shape and a place of the client rule (stage 3.4f-D: a spell at the caster has the Radius of its row as
        // its distance; before, a spell with CastDistance 0 was left out).
        if (std::all_of(area.casts.begin(), area.casts.end(), [&](const gamedata::AreaCast& cast) {
                return m_world.spells().area_usable(cast) && place_rule(cast) != CastPlace::unsupported;
            }) && area_range(area.casts.front()) > 0.0F) {
            usable.push_back(index);
        }
    }
    return usable;
}

bool Behavior::think_area(Entity& creature, Entity& target, float seconds, const AreaValues& values, std::int32_t named) {
    CreatureMind& mind = creature.creature.mind;
    const gamedata::CreatureSlice& body = *creature.creature.body;
    const std::vector<std::uint32_t> usable = usable_areas(body);
    if (usable.empty()) {
        return false; // No spell data of the shape of its casts: the other rules decide.
    }
    // The area attack that the node names (stage BT2): the other rules decide when the spell system cannot make its casts.
    if (named >= 0 && std::find(usable.begin(), usable.end(), static_cast<std::uint32_t>(named)) == usable.end()) {
        return false;
    }
    const WorldClock::time_point now = m_world.clock().now();
    // A creature whose only attack is an area attack (the Undead Shaman) keeps its distance by the area rules.
    const bool only = body.attacks.empty() && !body.basicShot;
    const std::uint32_t area = named >= 0 ? static_cast<std::uint32_t>(named) : usable[mind.nextArea % usable.size()];
    const float range = area_range(body.areas[area].casts.front());
    const float away = horizontal_distance(creature.position, target.position);
    const float rise = std::fabs(creature.position.z - target.position.z);
    const bool sees = rise <= behavior_rules::kShotHeight && m_world.navigation().sees(creature.position, target.position);
    const float hold = std::max(range - values.holdMargin, behavior_rules::kNearest);
    const bool inRange = sees && away <= (mind.closing && only ? hold : range);
    // The pause after the last attack: the pause of a melee attack (mind.ready), or the repeat of the basic shot (mind.shotReady) when it
    // ends first. Else the shot of a creature with a basic shot always comes first, and its area attack does not come again (live run
    // of stage 3.4f-C: a Dryad Shaman shot for 40 s).
    const bool rested = now >= mind.ready || (body.basicShot && now >= mind.shotReady);
    if (inRange) {
        if (now >= mind.areaReady && rested && spawn_lead_over(creature, now)) {
            mind.closing = false;
            start_area(creature, target, area, values);
            return mind.shooting;
        }
        if (only) {
            mind.closing = false;
            turn_to(creature, target.position);
            return true;
        }
        return false;
    }
    if (!only) {
        return false;
    }
    if (!can_walk(creature) || creature.creature.rooted(now)) {
        turn_to(creature, target.position);
        return true;
    }
    // Farther than the CastDistance, or no free line: the walk to the target on its path, to the hold margin inside the CastDistance.
    mind.closing = true;
    walk_to(creature, target, hold, sees, away, seconds);
    return true;
}

void Behavior::start_area(Entity& creature, Entity& target, std::uint32_t index, const AreaValues& values) {
    CreatureMind& mind = creature.creature.mind;
    const gamedata::CreatureSlice& body = *creature.creature.body;
    const gamedata::CreatureAreaAttack& area = body.areas[index];
    const WorldClock::time_point now = m_world.clock().now();
    creature.creature.yaw = yaw_to(creature.position, target.position);
    mind.sentYaw = creature.creature.yaw;
    const std::chrono::milliseconds length = milliseconds(area.length);
    if (!m_creatures.start_action(creature, area.action, length)) {
        mind.areaReady = now + values.pause;
        return;
    }
    m_world.health().fought(target);
    if (mind.holdUntil) {
        mind.holdUntil = now + behavior_rules::kNoWayHold;
    }
    // The action plays to its end as the action of a shot (no step, no turn, no other attack in it).
    mind.shooting = true;
    mind.actionPause = values.pause;
    mind.attackGeneration = creature.creature.action->generation;
    mind.attackEnds = now + length;
    const bool only = body.attacks.empty() && !body.basicShot;
    mind.areaReady = std::max(now + (only ? values.onlyRepeat : values.repeat), mind.attackEnds);
    ++mind.nextArea;
    ++mind.attacks;
    ++m_counts.areaAttacks;
    for (std::uint32_t cast = 0; cast < area.casts.size(); ++cast) {
        m_casts.emplace(now + milliseconds(area.casts[cast].time), PendingCast{creature.id, target.id, mind.attackGeneration, index, cast});
    }
    log::write("world: creature %016llx (%s) starts the area attack %s at %s (%.1f m, %zu cast(s) of %s from %.2f s, %.2f s)",
               id_number(creature.id),
               creature.creature.kind->id.c_str(),
               body.actions[area.action].name.c_str(),
               target.name.c_str(),
               static_cast<double>(horizontal_distance(creature.position, target.position)),
               area.casts.size(),
               area.casts.front().spell.c_str(),
               static_cast<double>(area.casts.front().time),
               static_cast<double>(area.length));
}

bool Behavior::command_area(Entity& creature, EntityId target) {
    const gamedata::CreatureSlice& body = *creature.creature.body;
    Entity* character = fightable(target);
    const std::vector<std::uint32_t> usable = usable_areas(body);
    const float* values = command_values(creature, tree::NodeKind::area_attack); // null: no such node (finding R4 of BT1)
    if (creature.creature.dead || usable.empty() || character == nullptr || character->kind != EntityKind::player || values == nullptr
        || creature.creature.held(m_world.clock().now()) || creature.creature.held_by_force()) {
        return false;
    }
    CreatureMind& mind = creature.creature.mind;
    if (mind.mode != CreatureMode::fight || mind.target != character->id) {
        take_target(creature, *character, "a command area attack", horizontal_distance(creature.position, character->position));
    }
    start_area(creature, *character, usable[mind.nextArea % usable.size()], area_values(values));
    return mind.shooting;
}

CastPlace Behavior::place_rule(const gamedata::AreaCast& cast) const {
    // Client code 0x679DBB0 (the AI branch of MagicComponent::CastSpell; STATIC-CAST.md of stage 3.4f-C, read again in stage 3.4f-D): the
    // ranged attack data comes from RangedAttackProfile and RangedAttackName of the row (0x65F3B70), when both are set. AIAimAtTarget false:
    // the place of the caster, or a shot of the ranged attack data. AIAimAtTarget with UseAllAvailableTargets: for each target, the aim point
    // (0x5C35450) only with ranged attack data; with none the place keeps its first value, the place of the caster. AIAimAtTarget alone: the
    // position of the current target.
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    const gamedata::SpellRow* row = abilities != nullptr ? abilities->spell(cast.spell) : nullptr;
    const bool ranged = row != nullptr && !row->cells.text("RangedAttackProfile").empty() && !row->cells.text("RangedAttackName").empty();
    if (!cast.aimAtTarget) {
        return ranged ? CastPlace::unsupported : CastPlace::caster;
    }
    if (cast.allTargets) {
        return ranged ? CastPlace::aim : CastPlace::caster;
    }
    return CastPlace::target;
}

float Behavior::area_range(const gamedata::AreaCast& cast) const {
    if (place_rule(cast) != CastPlace::caster) {
        return m_world.spells().cast_distance(cast);
    }
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    const gamedata::SpellRow* row = abilities != nullptr ? abilities->spell(cast.spell) : nullptr;
    return row != nullptr ? row->radius : 0.0F;
}

state::WorldPosition Behavior::area_place(const Entity& creature, const Entity& target, const gamedata::AreaCast& cast) const {
    const CastPlace rule = place_rule(cast);
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    const gamedata::SpellRow* row = abilities != nullptr ? abilities->spell(cast.spell) : nullptr;
    // The offset of the request (client code 0x676AAF0): CharRelPosOffset of the row, turned by the rotation of the caster (x to its right, y
    // to its front, z up). The spells of the creature data have none (0, 0, 0).
    const auto offset = [&](state::WorldPosition place) {
        if (row == nullptr) {
            return place;
        }
        const float right = row->cells.number("CharRelPosOffsetX");
        const float front = row->cells.number("CharRelPosOffsetY");
        const float yaw = creature.creature.yaw * kPi / 180.0F;
        place.x += std::cos(yaw) * right - std::sin(yaw) * front;
        place.y += std::sin(yaw) * right + std::cos(yaw) * front;
        place.z += row->cells.number("CharRelPosOffsetZ");
        return place;
    };
    if (rule == CastPlace::caster) {
        // At the caster, on the floor of the navigation data nearest to its height.
        state::WorldPosition place = offset(creature.position);
        if (const std::optional<float> ground = m_world.navigation().ground_height(place);
            ground && std::fabs(*ground - creature.position.z) < behavior_rules::kContactHeight) {
            place.z = *ground;
        }
        return place;
    }
    // The place of the target at the cast, with its lead (AILeadTarget with ProjectileSpeed: the velocity of the target times the time of
    // the aim speed over the distance; at most AILeadTargetMaxAngle from the line of the creature to the target, in the plane). The rule
    // `target` has no lead and no angle limit (0x665A300: the base position of the record).
    state::WorldPosition place = target.position;
    if (rule == CastPlace::aim && cast.lead && cast.aimSpeed > 0.0F) {
        const std::array<float, 3> velocity = m_world.velocity_of(target);
        const float time = horizontal_distance(creature.position, target.position) / cast.aimSpeed;
        state::WorldPosition lead{target.position.x + velocity[0] * time, target.position.y + velocity[1] * time, target.position.z};
        const float direct = std::atan2(target.position.y - creature.position.y, target.position.x - creature.position.x);
        const float wanted = std::atan2(lead.y - creature.position.y, lead.x - creature.position.x);
        float turn = std::remainder(wanted - direct, 2.0F * kPi);
        const float limit = cast.leadAngle * kPi / 180.0F;
        if (std::fabs(turn) > limit) {
            turn = std::copysign(limit, turn);
            const float size = horizontal_distance(creature.position, lead);
            lead = {creature.position.x + std::cos(direct + turn) * size, creature.position.y + std::sin(direct + turn) * size, lead.z};
        }
        place = lead;
    }
    // At most AIAimMaxAngle from the front of the creature (the creature looks at its target at the start of the action; client code
    // 0x5C09A20 limits the direction of the aim with the max angles of the clip, and the distance stays).
    if (rule == CastPlace::aim && cast.aimAngle < 180.0F && horizontal_distance(creature.position, place) > 0.01F) {
        const float yaw = creature.creature.yaw * kPi / 180.0F;
        const float front = std::atan2(std::cos(yaw), -std::sin(yaw));
        const float toward = std::atan2(place.y - creature.position.y, place.x - creature.position.x);
        const float side = std::remainder(toward - front, 2.0F * kPi);
        const float limit = cast.aimAngle * kPi / 180.0F;
        if (std::fabs(side) > limit) {
            const float size = horizontal_distance(creature.position, place);
            const float turned = front + std::copysign(limit, side);
            place = {creature.position.x + std::cos(turned) * size, creature.position.y + std::sin(turned) * size, place.z};
        }
    }
    place = offset(place);
    // At most the CastDistance of the spell row from the creature (horizontal; a decision of an agent: the clamp of the server is not in the
    // client, STATIC-CAST.md; the target can walk out of the distance in the windup).
    const float range = m_world.spells().cast_distance(cast);
    const float away = horizontal_distance(creature.position, place);
    if (range > 0.0F && away > range) {
        place = {creature.position.x + (place.x - creature.position.x) / away * range,
                 creature.position.y + (place.y - creature.position.y) / away * range, place.z};
    }
    // On the floor: the floor of the navigation data nearest to the height of the target at that place (the client snaps the picture to
    // the ground too: the SurfaceAlignmentComponent of the tell).
    if (const std::optional<float> ground = m_world.navigation().ground_height(place);
        ground && std::fabs(*ground - target.position.z) < behavior_rules::kContactHeight) {
        place.z = *ground;
    }
    return place;
}

void Behavior::cast(const PendingCast& pending) {
    Entity* creature = m_creatures.find(pending.creature);
    const char* cancelled = nullptr;
    if (creature == nullptr || creature->creature.dead) {
        cancelled = "the creature is dead or not in the world";
    } else if (!creature->creature.mind.shooting || creature->creature.mind.attackGeneration != pending.generation
               || !creature->creature.action || creature->creature.action->generation != pending.generation) {
        cancelled = "a different action replaced the area attack";
    } else if (creature->creature.mind.mode != CreatureMode::fight || creature->creature.mind.target != pending.target) {
        cancelled = "the creature lost the target of the area attack";
    }
    Entity* target = cancelled == nullptr ? fightable(pending.target) : nullptr;
    if (cancelled == nullptr && (target == nullptr || target->kind != EntityKind::player)) {
        cancelled = "the target is not there";
    }
    if (cancelled != nullptr) {
        ++m_counts.castsCancelled;
        log::write("world: no area: the cast of creature %016llx makes no area: %s", id_number(pending.creature), cancelled);
        return;
    }
    const gamedata::AreaCast& data = creature->creature.body->areas[pending.area].casts[pending.cast];
    const state::WorldPosition place = area_place(*creature, *target, data);
    if (m_world.spells().make_area(AreaRequest{creature->id, &data, place}) != nullptr) {
        ++m_counts.casts;
        m_counts.castsAtCaster += place_rule(data) == CastPlace::caster ? 1U : 0U;
    } else {
        ++m_counts.castsCancelled;
    }
}

bool Behavior::wants(const Entity& creature, const gamedata::CreatureStatusAction& action) const {
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    if (abilities == nullptr) {
        return false;
    }
    bool heal = false;
    for (const gamedata::StatusMark& mark : action.marks) {
        for (const std::string& id : mark.statuses) {
            const gamedata::StatusRow* row = abilities->status(id);
            if (row == nullptr) {
                return false; // A status that the data does not have: no action (its mark would give nothing).
            }
            // The creature has the status of the action: no new action (the heal runs, the buff holds).
            if (std::any_of(creature.creature.effects.begin(), creature.creature.effects.end(),
                            [&](const CreatureEffect& effect) { return effect.status == row->id; })) {
                return false;
            }
            heal = heal || row->cells.number("HealthModifierPercent") > 0.0F || row->cells.number("Health") > 0.0F;
        }
    }
    if (!heal) {
        return true; // A buff: the creature fights and does not have it.
    }
    // A heal: the health of the creature is at its low-health part (game data of the slice) or below it.
    const std::optional<float> low = creature.creature.body->lowHealth;
    return low && creature.vitals.healthMax > 0.0F && creature.vitals.health <= *low * creature.vitals.healthMax;
}

bool Behavior::think_status(Entity& creature, Entity& target, const StatusValues& values, std::int32_t only) {
    CreatureMind& mind = creature.creature.mind;
    const gamedata::CreatureSlice& body = *creature.creature.body;
    const WorldClock::time_point now = m_world.clock().now();
    const bool rested = now >= mind.ready || (body.basicShot && now >= mind.shotReady);
    if (now < mind.statusReady || !rested || !spawn_lead_over(creature, now)) {
        return false;
    }
    for (std::uint32_t index = 0; index < body.statusActions.size(); ++index) {
        // The status action that the node names (stage BT2), or each one.
        if ((only < 0 || index == static_cast<std::uint32_t>(only)) && wants(creature, body.statusActions[index])) {
            turn_to(creature, target.position);
            start_status_action(creature, index, values);
            return mind.shooting;
        }
    }
    return false;
}

void Behavior::start_status_action(Entity& creature, std::uint32_t index, const StatusValues& values) {
    CreatureMind& mind = creature.creature.mind;
    const gamedata::CreatureSlice& body = *creature.creature.body;
    const gamedata::CreatureStatusAction& action = body.statusActions[index];
    const WorldClock::time_point now = m_world.clock().now();
    const std::chrono::milliseconds length = milliseconds(action.length);
    if (!m_creatures.start_action(creature, action.action, length)) {
        mind.statusReady = now + values.pause;
        return;
    }
    // The action plays to its end as the action of a shot (no step, no turn, no other attack in it).
    mind.shooting = true;
    mind.actionPause = values.pause;
    mind.attack.reset();
    mind.attackGeneration = creature.creature.action->generation;
    mind.attackEnds = now + length;
    mind.statusReady = std::max(now + values.repeat, mind.attackEnds);
    ++m_counts.statusActions;
    for (std::uint32_t mark = 0; mark < action.marks.size(); ++mark) {
        m_statusMarks.emplace(now + milliseconds(action.marks[mark].time), PendingStatus{creature.id, mind.attackGeneration, index, mark});
    }
    log::write("world: creature %016llx (%s) starts the status action %s (%zu mark(s) from %.2f s, %.2f s; health %.1f of %.1f)",
               id_number(creature.id), creature.creature.kind->id.c_str(), body.actions[action.action].name.c_str(), action.marks.size(),
               static_cast<double>(action.marks.front().time), static_cast<double>(action.length), static_cast<double>(creature.vitals.health),
               static_cast<double>(creature.vitals.healthMax));
}

void Behavior::status_mark(const PendingStatus& pending) {
    Entity* creature = m_creatures.find(pending.creature);
    const char* cancelled = nullptr;
    if (creature == nullptr || creature->creature.dead) {
        cancelled = "the creature is dead or not in the world";
    } else if (!creature->creature.mind.shooting || creature->creature.mind.attackGeneration != pending.generation
               || !creature->creature.action || creature->creature.action->generation != pending.generation) {
        cancelled = "a different action replaced the status action";
    }
    if (cancelled != nullptr) {
        ++m_counts.statusMarksCancelled;
        log::write("world: no status: the mark of creature %016llx gives nothing: %s", id_number(pending.creature), cancelled);
        return;
    }
    const gamedata::StatusMark& mark = creature->creature.body->statusActions[pending.action].marks[pending.mark];
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    bool given = false;
    for (const std::string& id : mark.statuses) {
        // The action list of the slice puts the status on the creature itself (ModifyStatusEffects of the action; game data).
        const gamedata::StatusRow* row = abilities != nullptr ? abilities->status(id) : nullptr;
        given = (row != nullptr && m_creatures.give_status(*creature, *row, creature->id)) || given;
    }
    ++(given ? m_counts.statusMarks : m_counts.statusMarksCancelled);
}

bool Behavior::command_status_action(Entity& creature, std::string_view action) {
    const gamedata::CreatureSlice& body = *creature.creature.body;
    const float* values = command_values(creature, tree::NodeKind::status_action); // null: no such node (finding R4 of BT1)
    if (creature.creature.dead || values == nullptr || creature.creature.held(m_world.clock().now()) || creature.creature.held_by_force()) {
        return false;
    }
    for (std::uint32_t index = 0; index < body.statusActions.size(); ++index) {
        if (action.empty() || body.actions[body.statusActions[index].action].name == action) {
            start_status_action(creature, index, status_values(values));
            return creature.creature.mind.shooting;
        }
    }
    return false;
}

// ---- the trees (stage BT1) --------------------------------------------------------------------------------------------------------

bool Behavior::reload_trees(const std::string& folder, tree::LoadReport& report) {
    tree::TreeSet trees;
    const bool loaded = trees.load(folder, report);
    for (const std::string& refused : report.refused) {
        log::write("world: behavior tree refused: %s", refused.c_str());
    }
    if (!loaded) {
        // A set with no tree "default" (an embedded file that is not correct: a fault of the build): the trees of the world stay.
        log::write("world: the behavior trees have no tree \"default\" that compiles: the trees of the world do not change");
        if (!m_default) {
            m_trees = std::move(trees);
        }
        return m_default != nullptr;
    }
    m_trees = std::move(trees);
    m_default = m_trees.default_tree();
    ++m_counts.treeReloads;
    for (const std::string& loaded : report.loaded) {
        log::write("world: behavior tree %s", loaded.c_str());
    }
    // The binding file (stage BT2): its trees must be trees of the new set.
    load_bindings(folder, report);
    m_refusalsLogged.clear();
    // Each creature of the world gets the new tree: its state (the results of the nodes, the keys) starts again.
    std::vector<EntityId> creatures;
    for (const auto& [id, entity] : m_world.entities()) {
        if (entity.kind == EntityKind::creature) {
            creatures.push_back(id);
        }
    }
    for (const EntityId id : creatures) {
        if (Entity* creature = m_creatures.find(id)) {
            bind(*creature);
        }
    }
    return true;
}

void Behavior::load_bindings(const std::string& folder, tree::LoadReport& report) {
    tree::BindingSet set;
    std::string error;
    const auto refuse = [&](const std::string& source) {
        report.refused.push_back(source + ": " + error);
        log::write("world: behavior tree refused: %s: %s", source.c_str(), error.c_str());
    };
    for (const tree::EmbeddedFile& file : tree::embedded_tree_files()) {
        const std::string source = std::string("embedded:") + file.name;
        if (file.name == tree::kBindingFile && !tree::BindingSet::parse(file.text, source, m_trees, set, error)) {
            refuse(source); // A fault of the build: each creature has the tree default.
        }
    }
    // The binding file of the folder of a development run replaces the file of the program (the whole file).
    const std::filesystem::path path = std::filesystem::path(folder) / std::string(tree::kBindingFile);
    std::error_code failure;
    if (!folder.empty() && std::filesystem::is_regular_file(path, failure)) {
        std::string text;
        tree::BindingSet own;
        if (!tree::read_text_file(path, tree::kMaxFileBytes, text, error)
            || !tree::BindingSet::parse(text, std::string(tree::kBindingFile), m_trees, own, error)) {
            refuse(std::string(tree::kBindingFile));
        } else {
            set = std::move(own);
        }
    }
    m_bindings = std::move(set);
    report.bindings = m_bindings.source().empty() ? std::string("no binding file: each creature has the tree default")
                                                  : m_bindings.source() + ", " + std::to_string(m_bindings.bindings().size()) + " binding(s)";
    log::write("world: behavior tree bindings: %s", report.bindings.c_str());
}

bool Behavior::fits(const tree::CompiledTree& tree, const gamedata::CreatureSlice& body, std::vector<std::int32_t>& actions, std::string& cause) {
    actions.assign(tree.actions.size(), kMissingAction);
    bool all = true;
    for (std::size_t slot = 0; slot < tree.actions.size(); ++slot) {
        const tree::NamedAction& named = tree.actions[slot];
        const bool status = named.kind == tree::NodeKind::status_action;
        const std::size_t count = status ? body.statusActions.size() : body.areas.size();
        for (std::size_t index = 0; index < count; ++index) {
            const std::uint32_t action = status ? body.statusActions[index].action : body.areas[index].action;
            if (action < body.actions.size() && body.actions[action].name == named.name) {
                actions[slot] = static_cast<std::int32_t>(index);
                break;
            }
        }
        if (actions[slot] == kMissingAction && all) {
            all = false;
            const char* what = status ? "status action" : "area attack";
            cause = "the tree \"" + tree.name + "\" names the " + what + " \"" + named.name + "\" (node " + std::to_string(named.node)
                    + "): the slice " + body.path + " has no " + what + " of this name";
        }
    }
    return all;
}

void Behavior::attach(Entity& creature, const std::shared_ptr<const tree::CompiledTree>& tree, std::vector<std::int32_t> actions) {
    CreatureBrain& brain = creature.creature.mind.brain;
    brain.tree = tree;
    brain.last.assign(tree ? tree->nodes.size() : 0, 0);
    brain.board.clear();
    if (tree) {
        for (const tree::Key& key : tree->keys) {
            brain.board.push_back(key.initial);
        }
    }
    brain.actions = std::move(actions);
}

void Behavior::bind(Entity& creature) {
    CreatureBrain& brain = creature.creature.mind.brain;
    brain.refusal.clear();
    const gamedata::CreatureKind* kind = creature.creature.kind;
    const gamedata::CreatureSlice* body = creature.creature.body;
    if (!m_default || kind == nullptr || body == nullptr || body->stationary()) {
        // Stage QG5c1: a stationary target has no tree of its own and no action: it never senses, roams, or attacks.
        brain.binding = body != nullptr && body->stationary() ? "a stationary target" : m_default ? "no binding" : "no tree";
        attach(creature, m_default, std::vector<std::int32_t>(m_default ? m_default->actions.size() : 0, kMissingAction));
        return;
    }
    // The binding of the file (world/tree_binding.h): the narrowest key that matches; each tree of a binding is a tree of the set.
    const tree::Selection selection = m_bindings.select(*kind, *body);
    std::shared_ptr<const tree::CompiledTree> chosen = m_trees.find(selection.tree());
    brain.binding = selection.text();
    if (!chosen) {
        chosen = m_default;
    }
    std::vector<std::int32_t> actions;
    std::string cause;
    if (!fits(*chosen, *body, actions, cause)) {
        // The slice does not have an action that the tree names: the creature has the tree default (one log line for each tree and slice).
        ++m_counts.treeRefusals;
        brain.refusal = cause;
        if (m_refusalsLogged.emplace(chosen->name, body->path).second) {
            log::write("world: behavior tree refused for creature %016llx (%s, %s): %s; the creature has the tree default (one line for "
                       "each tree and slice)",
                       id_number(creature.id), kind->id.c_str(), brain.binding.c_str(), cause.c_str());
        }
        chosen = m_default;
        (void)fits(*chosen, *body, actions, cause);
    }
    attach(creature, chosen, std::move(actions));
}

Behavior::TreeCommand Behavior::set_tree(Entity& creature, std::string_view name, std::string& cause) {
    const std::shared_ptr<const tree::CompiledTree> chosen = m_trees.find(name);
    if (!chosen) {
        cause = "the set has no tree \"" + std::string(name) + "\"";
        return TreeCommand::unknownTree;
    }
    std::vector<std::int32_t> actions;
    if (!fits(*chosen, *creature.creature.body, actions, cause)) {
        ++m_counts.treeRefusals;
        log::write("world: command: behavior tree refused for creature %016llx (%s): %s; the creature keeps the tree %s",
                   id_number(creature.id), creature.creature.kind->id.c_str(), cause.c_str(),
                   creature.creature.mind.brain.tree ? creature.creature.mind.brain.tree->name.c_str() : "(none)");
        return TreeCommand::refused;
    }
    attach(creature, chosen, std::move(actions));
    CreatureBrain& brain = creature.creature.mind.brain;
    brain.binding = "a command";
    brain.refusal.clear();
    ++m_counts.treeCommands;
    log::write("world: command: creature %016llx (%s) has the tree %s", id_number(creature.id), creature.creature.kind->id.c_str(),
               chosen->name.c_str());
    return TreeCommand::done;
}

const float* Behavior::command_values(const Entity& creature, tree::NodeKind kind) const {
    // The first node of the kind; for a shot the first node with the band of the slice (stage BT2: default/shot and default/cast).
    const bool caster = creature.creature.body != nullptr && creature.creature.body->caster;
    const auto first = [&](const tree::CompiledTree& tree) -> const float* {
        if (kind != tree::NodeKind::basic_shot) {
            return tree.first(kind);
        }
        for (std::size_t index = 0; index < tree.nodes.size(); ++index) {
            const float* values = tree.values(index);
            if (tree.nodes[index].kind == kind
                && values[(caster ? tree::param::kCaster : tree::param::kArcher) + tree::param::kBandFar] != tree::param::kNoBand) {
                return values;
            }
        }
        return nullptr;
    };
    const std::shared_ptr<const tree::CompiledTree>& own = creature.creature.mind.brain.tree;
    if (own) {
        if (const float* values = first(*own)) {
            return values;
        }
    }
    return m_default ? first(*m_default) : nullptr;
}

std::uint32_t Behavior::cadence_of(const Entity& creature) const {
    const std::shared_ptr<const tree::CompiledTree>& own = creature.creature.mind.brain.tree;
    return own && own->lightBeforeHeavy != 0 ? own->lightBeforeHeavy : m_default->lightBeforeHeavy;
}

void Behavior::run_tree(Entity& creature, float seconds) {
    CreatureBrain& brain = creature.creature.mind.brain;
    brain.at = m_world.clock().now();
    brain.decided = 0;
    if (!brain.tree) {
        brain.cause = StepCause::no_tree;
        return;
    }
    brain.cause = StepCause::tree;
    std::fill(brain.last.begin(), brain.last.end(), std::uint8_t{0});
    ++m_counts.treeSteps;
    m_stepTarget = StepTarget{};
    (void)run_node(creature, *brain.tree, 0, seconds);
    m_stepTarget = StepTarget{};
}

Entity* Behavior::step_target(const Entity& creature) {
    const EntityId target = creature.creature.mind.target;
    if (m_stepTarget.creature != creature.id || m_stepTarget.target != target) {
        m_stepTarget = StepTarget{creature.id, target, fightable(target)};
    }
    return m_stepTarget.entity;
}

tree::Status Behavior::run_node(Entity& creature, const tree::CompiledTree& tree, std::size_t index, float seconds) {
    using tree::NodeKind;
    using tree::Status;
    const tree::Node& node = tree.nodes[index];
    const float* values = tree.values(index);
    CreatureMind& mind = creature.creature.mind;
    ++m_counts.treeNodes;
    Status status = Status::failure;
    const auto leaf = [&](bool done) { return done ? Status::success : Status::failure; };
    switch (node.kind) {
    case NodeKind::selector:
        for (std::size_t child = index + 1; child < node.end; child = tree.nodes[child].end) {
            status = run_node(creature, tree, child, seconds);
            if (status != Status::failure) {
                break;
            }
        }
        break;
    case NodeKind::sequence:
        status = Status::success;
        for (std::size_t child = index + 1; child < node.end; child = tree.nodes[child].end) {
            status = run_node(creature, tree, child, seconds);
            if (status != Status::success) {
                break;
            }
        }
        break;
    case NodeKind::invert:
        status = run_node(creature, tree, index + 1, seconds);
        status = status == Status::success ? Status::failure : status == Status::failure ? Status::success : status;
        break;
    case NodeKind::subtree:
        status = run_node(creature, tree, index + 1, seconds);
        break;
    case NodeKind::in_fight:
        status = leaf(mind.mode == CreatureMode::fight);
        break;
    case NodeKind::target_is: {
        const Entity* target = step_target(creature);
        status = leaf(target != nullptr
                      && (values[tree::param::kTargetPlayer] != 0.0F ? target->kind == EntityKind::player : target->kind == EntityKind::spell));
        break;
    }
    case NodeKind::check: {
        const float value = mind.brain.board[static_cast<std::size_t>(values[tree::param::kCheckKey])];
        const float other = values[tree::param::kCheckValue];
        switch (static_cast<tree::Compare>(static_cast<int>(values[tree::param::kCheckOp]))) {
        case tree::Compare::eq:
            status = leaf(value == other);
            break;
        case tree::Compare::ne:
            status = leaf(value != other);
            break;
        case tree::Compare::lt:
            status = leaf(value < other);
            break;
        case tree::Compare::le:
            status = leaf(value <= other);
            break;
        case tree::Compare::gt:
            status = leaf(value > other);
            break;
        case tree::Compare::ge:
            status = leaf(value >= other);
            break;
        }
        break;
    }
    case NodeKind::set:
        mind.brain.board[static_cast<std::size_t>(values[tree::param::kSetKey])] = values[tree::param::kSetValue];
        status = Status::success;
        break;
    case NodeKind::keep_target:
        status = leaf(mind.mode == CreatureMode::fight && keep_target(creature, values[tree::param::kLoseDistance]));
        break;
    case NodeKind::status_action: {
        // The heal at low health or the buff of the slice (stage 3.4f-D). The target is each target of a fight, also a summon (code review
        // of stage 3.4f-D, finding SU-R6): the action has no recipient but the creature.
        if (creature.creature.body->statusActions.empty()) {
            break;
        }
        // Stage BT2: the status action that the node names (no such action of the slice: the leaf fails).
        const std::int32_t only = named_action(creature, values, tree::param::kStatusAction);
        if (only == kMissingAction) {
            break;
        }
        Entity* target = step_target(creature);
        status = leaf(target != nullptr && think_status(creature, *target, status_values(values), only));
        break;
    }
    case NodeKind::area_attack: {
        // An area attack (stage 3.4f-C) at a character only: in the CastDistance of its spell, with a free line, when it is ready.
        if (creature.creature.body->areas.empty()) {
            break;
        }
        const std::int32_t named = named_action(creature, values, tree::param::kAreaAction);
        if (named == kMissingAction) {
            break;
        }
        Entity* target = step_target(creature);
        status = leaf(target != nullptr && target->kind == EntityKind::player
                      && think_area(creature, *target, seconds, area_values(values), named));
        break;
    }
    case NodeKind::basic_shot: {
        // The basic shot of an archer or a caster (stages 3.4f-A and 3.4f-B) at a character only: the band, the release, the walk back.
        // Its melee attack wins in its reach (think_shot gives false).
        const gamedata::CreatureSlice& body = *creature.creature.body;
        // Stage BT2: a node with no band of the slice (the subtree default/shot for a caster, default/cast for an archer) fails.
        if (!body.basicShot || values[(body.caster ? tree::param::kCaster : tree::param::kArcher) + tree::param::kBandFar] == tree::param::kNoBand) {
            break;
        }
        Entity* target = step_target(creature);
        status = leaf(target != nullptr && target->kind == EntityKind::player
                      && think_shot(creature, *target, *body.basicShot, seconds, shot_values(body, values, cadence_of(creature))));
        break;
    }
    case NodeKind::melee: {
        Entity* target = creature.creature.body->attacks.empty() ? nullptr : step_target(creature);
        if (target != nullptr) {
            melee(creature, *target, seconds, melee_values(values));
            status = Status::success;
        }
        break;
    }
    case NodeKind::turn_to_target:
        // A creature with only a shot and a target that it cannot shoot (a summon).
        if (const Entity* target = step_target(creature)) {
            turn_to(creature, target->position);
            status = Status::success;
        }
        break;
    case NodeKind::roam:
        if (mind.mode == CreatureMode::rest) {
            roam(creature, seconds, roam_values(values));
            status = Status::success;
        }
        break;
    }
    mind.brain.last[index] = static_cast<std::uint8_t>(status);
    // The leaf that decided the step: the last action leaf that succeeded, or keep_target that lost the target.
    if (node.kind >= NodeKind::keep_target && node.kind != NodeKind::set && (status == Status::success || node.kind == NodeKind::keep_target)) {
        mind.brain.decided = static_cast<std::uint8_t>(1 + static_cast<unsigned>(node.kind));
    }
    return status;
}


} // namespace soulwarden::backend::world

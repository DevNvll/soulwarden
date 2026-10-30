#include "world/creatures.h"

#include <algorithm>
#include <cmath>

#include "core/logging/log.h"
#include "world/data_rules.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

// The creature types of the reaction rule (the action Reaction of baseai_reaction.actionlist, game data; user test 2).
/** A light attack and an ability of a power below 1 play a reaction on these types. */
constexpr std::string_view kLightReactionTypes[] = {"Solo", "Solo-", "Solo_StarterBeach", "Solo-_StarterBeach"};
/** A heavy attack plays a reaction on these types. */
constexpr std::string_view kHeavyReactionTypes[] = {"Solo",       "Solo-",      "Solo+",        "Solo_StarterBeach", "Solo-_StarterBeach",
                                                    "Solo+_StarterBeach", "Named_Solo", "Named_Solo+"};
/** These types play no reaction while the reaction limit holds, and their first reaction starts it. */
constexpr std::string_view kLimitedReactionTypes[] = {"DungeonMiniBoss", "Dungeon+", "Elite+", "EliteMiniBoss", "CatacombsMiniBoss"};
/** These types play no reaction. */
constexpr std::string_view kBossReactionTypes[] = {"Raid10+", "Raid20+", "Boss", "Boss+", "Boss-", "EliteBoss"};
/** The chained reactions of a Solo type that start the reaction limit (Chained_Reaction_Count 3.0). */
constexpr std::uint32_t kReactionChain = 3;
/** The limit after the end of the reaction that started it (the status AI_Reaction_Limiter: BaseDuration 2.0). */
constexpr std::chrono::milliseconds kReactionLimit{2000};

/**
 * @return The reaction fragment of a power level of a hit for a slice with no reaction table (a file of a tool from before stage U3; the
 *         rule of user test 2 with the base table BaseAI_ReactionTableHarness): power 0 and 1 play r_R1_F, power 2 to 4 their own
 *         fragment, and a larger power r_R4_F. A slice with a table uses its rows (select_row).
 */
std::size_t reaction_level(std::uint32_t power) {
    return power <= 1 ? 1 : std::min<std::size_t>(power, 4);
}

constexpr float kDegrees = 57.2957795F;

/**
 * @return The angle of the source of a hit from the front of a creature (stage U3, research Q2; the angle of the damage context, client
 *         code 0x2C07B70), in degrees from -180 to 180: positive for a source to the right of the creature (the sign of damaged_right; the
 *         turn of AlignToDirection, client code 0x2B9F1C0, and live check LC-Q2-1). A source at the creature (nearer than 5 cm): 0, the
 *         front.
 */
float hit_angle(const Entity& entity, const state::WorldPosition& source) {
    const float dx = source.x - entity.position.x;
    const float dy = source.y - entity.position.y;
    if (std::hypot(dx, dy) < 0.05F) {
        return 0.0F;
    }
    // The yaw of a creature: degrees, 0 to the north, larger to the west (counterclockwise). Its front is (-sin, cos), its right (cos, sin).
    const float yaw = entity.creature.yaw / kDegrees;
    const float front = -std::sin(yaw) * dx + std::cos(yaw) * dy;
    const float right = std::cos(yaw) * dx + std::sin(yaw) * dy;
    return std::atan2(right, front) * kDegrees;
}

/** @return True when an angle holds a condition of a row: no range (each angle), or min - tolerance < angle < max + tolerance. */
bool angle_holds(const std::vector<gamedata::AngleRange>& ranges, float angle) {
    if (ranges.empty()) {
        return true;
    }
    return std::any_of(ranges.begin(), ranges.end(), [angle](const gamedata::AngleRange& range) {
        return angle > range.min - range.tolerance && angle < range.max + range.tolerance;
    });
}

/**
 * @return The first row of a reaction table that holds for a hit (client code 0x2B9F1C0; research Q2, item 4): its power is the power
 *         of the hit, or each power (-1), or the largest power of the table for a hit with a larger power; and its angle condition holds.
 *         Null: no row holds (the action keeps its own fragment).
 */
const gamedata::ReactionRow* select_row(const gamedata::ReactionTable& table, std::int32_t power, float angle) {
    const std::int32_t level = table.largestPower >= 0 && power > table.largestPower ? table.largestPower : power;
    for (const gamedata::ReactionRow& row : table.rows) {
        if ((row.power == -1 || row.power == level) && angle_holds(row.angles, angle)) {
            return &row;
        }
    }
    return nullptr;
}

/**
 * @return The time from the start of a reaction to its end with the clip HitStun (stage U3, research Q3; client code 0x2559640): the clip
 *         at `start` sets the play speed of the rest of the fragment to (length - start) / (hitStun / 30 - start), from 0.001 to 3.0 (3.0
 *         when the divisor is 0 or less), so that the fragment ends at the hit stun. The client does it only for its own character; the
 *         server of the game did it for a creature (an inference of the research: the "_AI" hit stun of the rows and the clips in the
 *         creature fragments; a decision of the orchestrator).
 */
float hit_stun_length(float length, float start, std::uint32_t stunFrames) {
    const float rest = length - start;
    if (!(rest > 0.0F)) {
        return length;
    }
    const float stun = static_cast<float>(stunFrames) / 30.0F;
    if (stun - start <= 0.0F) {
        return start + rest / 3.0F;
    }
    return std::clamp(stun, start + rest / 3.0F, start + rest / 0.001F);
}

/** @return The push scales of a reaction action for a creature (stage U3, research Q5): the conditional PromotedElite of a promoted elite. */
const gamedata::PushScale& push_scale(const gamedata::ReactionTable& table, const Creature& creature, bool limited) {
    if (limited) {
        const gamedata::PushScale& entry = table.push("ReactionLimiter_Active");
        if (entry.name == "ReactionLimiter_Active") {
            return entry;
        }
    }
    return creature.kind->promotedElite ? table.push("PromotedElite") : table.push("Default");
}

template <std::size_t N>
bool type_in(const std::string& type, const std::string_view (&list)[N]) {
    return std::find(std::begin(list), std::end(list), type) != std::end(list);
}

} // namespace

namespace {

/**
 * @return The slice of a kind for one creature. An alias with more than one slice gives each
 *         slice its part of the creatures (the percent values of the alias): the selector is a
 *         count, so that the sequence is the same in each run.
 */
const gamedata::CreatureSlice* select_body(const gamedata::CreatureData& data,
                                           const gamedata::CreatureKind& kind,
                                           std::uint32_t selector) {
    std::uint64_t total = 0;
    for (const gamedata::CreatureBody& body : kind.bodies) {
        total += body.percent;
    }
    if (total == 0) {
        return nullptr;
    }
    // A step that is not a divisor of a small total: each slice of two equal parts gets its turn.
    std::uint64_t place = (static_cast<std::uint64_t>(selector) * 2654435761ULL >> 7) % total;
    for (const gamedata::CreatureBody& body : kind.bodies) {
        if (place < body.percent) {
            return &data.slices()[body.slice];
        }
        place -= body.percent;
    }
    return &data.slices()[kind.bodies.front().slice];
}

} // namespace

void SpawnGrid::build(const std::vector<gamedata::SpawnPoint>& points) {
    m_cells.clear();
    m_sorted.clear();
    for (std::size_t index = 0; index < points.size(); ++index) {
        m_cells[key_of(cell(points[index].position.x), cell(points[index].position.y))].push_back(
            static_cast<std::uint32_t>(index));
    }
    m_sorted.reserve(m_cells.size());
    for (const auto& [key, cellPoints] : m_cells) {
        const auto x = static_cast<std::int64_t>(static_cast<std::int32_t>(key >> 32));
        const auto y = static_cast<std::int64_t>(static_cast<std::int32_t>(key & 0xFFFFFFFFULL));
        m_sorted.push_back({{x, y}, &cellPoints});
    }
    std::sort(m_sorted.begin(), m_sorted.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
}

Creatures::Creatures(World& world, const gamedata::CreatureData* data)
    : m_world(world), m_data(data), m_behavior(world, *this), m_forced(world, *this), m_attackMotion(world, *this) {
    load_points();
}

void Creatures::set_waiting(std::uint32_t point, bool waiting) {
    m_points[point].waiting = waiting;
    if (waiting) {
        m_waiting.insert(point);
    } else {
        m_waiting.erase(point);
    }
}

void Creatures::load_points() {
    // The backend program loads the creature data before it makes the world. A test can set the
    // data later: the table of the spawn points then has the size of the data at the first use.
    if (!has_data() || m_points.size() == m_data->spawn_points().size()) {
        return;
    }
    const std::vector<gamedata::SpawnPoint>& points = m_data->spawn_points();
    m_points.assign(points.size(), SpawnPointState{});
    m_waiting.clear();
    m_grid.build(points);
}

Entity* Creatures::find(EntityId id) {
    Entity* entity = m_world.find(id);
    return entity != nullptr && entity->kind == EntityKind::creature ? entity : nullptr;
}

Entity* Creatures::make(const gamedata::CreatureKind& kind,
                        std::uint32_t level,
                        const state::WorldPosition& position,
                        float yaw,
                        std::optional<std::uint32_t> spawnPoint,
                        std::uint32_t selector,
                        SpaceId space) {
    const gamedata::CreatureSlice* body = select_body(*m_data, kind, selector);
    const float health = m_data->health(kind, level);
    if (body == nullptr || !(health > 0.0F)) {
        return nullptr;
    }
    Creature creature;
    creature.kind = &kind;
    creature.body = body;
    creature.level = level;
    creature.yaw = yaw;
    creature.spawnPoint = spawnPoint;
    creature.madeAt = m_world.clock().now();
    // Stage DG1: the spawn points and the commands are main world content (the creatures of an instance level come with stage DG2). Stage
    // QG9a: a member of an encounter of an instance (world/encounters.h, the instance recipes) is in the space of its instance.
    Entity& entity = m_world.make_creature(creature, position, health, space);
    m_creatures.insert(entity.id);
    m_counts.largest = std::max(m_counts.largest, m_creatures.size());
    if (body->spawnAction) {
        // The client shows the body and the name plate of this slice only after its spawn
        // action. It is a value of the creature for its whole time in the world: the first
        // movement state of each session for the creature has it, with the time from its
        // start, and the client of that session then shows the creature at once. An attack, a
        // reaction, and a walk do not take it away.
        CreatureAction spawned;
        spawned.action = *body->spawnAction;
        spawned.fragment = static_cast<std::uint32_t>(body->actions[*body->spawnAction].fragmentIndex);
        spawned.generation = static_cast<std::uint16_t>(++entity.creature.actions);
        spawned.started = entity.creature.madeAt;
        spawned.tags = body->tags;
        entity.creature.spawnAction = spawned;
    }
    m_behavior.made(entity);
    return &entity;
}

CreatureResult Creatures::spawn(const std::string& kindId,
                                const state::WorldPosition& position,
                                float yaw,
                                std::optional<std::uint32_t> level) {
    CreatureResult result;
    if (!has_data()) {
        result.error = CreatureError::noData;
        return result;
    }
    load_points();
    const gamedata::CreatureKind* kind = m_data->find_kind(kindId);
    if (kind == nullptr) {
        result.error = CreatureError::unknownKind;
        return result;
    }
    if (!valid_position(position) || !std::isfinite(yaw)) {
        result.error = CreatureError::badPosition;
        return result;
    }
    const std::uint32_t creatureLevel = level.value_or(kind->level);
    if (!m_data->has_level(creatureLevel)) {
        result.error = CreatureError::badLevel;
        return result;
    }
    result.entity = make(*kind, creatureLevel, position, yaw, std::nullopt, m_commandSpawns++);
    if (result.entity == nullptr) {
        result.error = CreatureError::badLevel;
        return result;
    }
    ++m_counts.spawnedByCommand;
    return result;
}

Entity* Creatures::spawn_member(const gamedata::CreatureKind& kind, const state::WorldPosition& position, float yaw, std::uint32_t placement,
                                std::uint64_t generation, const gamedata::PatrolRoute* route, SpaceId space) {
    if (!has_data() || !valid_position(position) || !std::isfinite(yaw) || !m_data->has_level(kind.level)) {
        return nullptr;
    }
    load_points();
    Entity* entity = make(kind, kind.level, position, yaw, std::nullopt, m_commandSpawns++, space);
    if (entity == nullptr) {
        return nullptr;
    }
    entity->creature.encounter = std::pair{placement, generation};
    if (route != nullptr && !route->points.empty()) {
        // Stage QG5c5: the patrol path of a part of a group member (the rules of a patrol of a spawn point, world/behavior.h).
        entity->creature.memberRoute = std::make_shared<const gamedata::PatrolRoute>(*route);
    }
    ++m_counts.spawnedByEncounter;
    return entity;
}

bool Creatures::remove_member(EntityId id) {
    Entity* entity = find(id);
    if (entity == nullptr || !entity->creature.encounter) {
        return false;
    }
    ++m_counts.removedByEncounter;
    m_world.remove_creature(id);
    return true;
}

CreatureResult Creatures::remove(EntityId id) {
    CreatureResult result;
    Entity* entity = find(id);
    if (entity == nullptr) {
        result.error = CreatureError::unknownCreature;
        return result;
    }
    if (entity->creature.spawnPoint) {
        // A command took the creature away: the distance rule must not make it again, and no
        // respawn time of a death before it does (code review of stage 3.5a, finding F8). Only
        // the command `reset` makes the point free.
        set_waiting(*entity->creature.spawnPoint, true);
        m_points[*entity->creature.spawnPoint].respawnAt.reset();
    }
    ++m_counts.removedByCommand;
    m_world.remove_creature(id);
    return result;
}

CreatureResult Creatures::set_health(EntityId id, float health) {
    CreatureResult result;
    Entity* entity = find(id);
    if (entity == nullptr) {
        result.error = CreatureError::unknownCreature;
        return result;
    }
    if (entity->creature.dead) {
        result.error = CreatureError::dead;
        return result;
    }
    if (!std::isfinite(health) || health < 0.0F || health > entity->vitals.healthMax) {
        result.error = CreatureError::badHealth;
        return result;
    }
    if (entity->creature.immune() && health < entity->vitals.health) {
        // On its way back a creature takes no damage from any source: a hit, the health command,
        // and the kill command (code review of stage 3.4e, finding F7). A health that goes up
        // is not a damage.
        result.error = CreatureError::immune;
        return result;
    }
    entity->vitals.health = health;
    if (health == 0.0F) {
        entity->creature.dead = true;
        entity->creature.diedAt = m_world.clock().now();
        ++m_counts.deaths;
        if (entity->creature.spawnPoint) {
            // The respawn time of the spawner from the time of the death (world/rewards.h,
            // respawn_time): from then on the point makes its creature again when a player is near
            // it, and the body leaves the world first. The command `reset` makes it free at once.
            const std::uint32_t point = *entity->creature.spawnPoint;
            set_waiting(point, true);
            const std::chrono::milliseconds wait = respawn_time(m_data->spawn_points()[point], m_world.random_unit());
            m_points[point].respawnAt = entity->creature.diedAt + wait;
            log::write("world: spawn point %s makes its creature again in %.0f s, when a player is near it",
                       m_data->spawn_points()[point].id.c_str(),
                       std::chrono::duration<double>(wait).count());
        }
        log::write("world: creature %016llx (%s) is dead",
                   static_cast<unsigned long long>(entity->id),
                   entity->creature.kind->id.c_str());
        // A dead creature has no target, and the attack that it plays does no damage.
        m_behavior.died(*entity);
        // Its shots in the air end with no hit (stage 3.4f-A, a rule of this project: the death of the source ends its flights).
        m_world.projectiles().source_left(entity->id);
        m_forced.stop(*entity);
        // The auras that follow the creature end (code review of stage 3.3b-S2b, finding F3).
        m_world.spells().target_died(entity->id);
        // The ground areas of the creature whose spell row has EndOnCasterDeath end (stage 3.4f-C, game data: the lightning strike of the
        // Undead Shaman; the mushroom of the Dryad Shaman has no such cell and stays to its end).
        m_world.spells().caster_died(entity->id);
        // A body with a skinning row can be skinned (stage 3.6a: the GatherableController state of its slice, Healthy).
        m_world.gathering().creature_died(*entity);
        // Stage QG5c2: the death of a member of an encounter counts for its stage (a queued event: the encounter system removes no
        // entity in a hit).
        m_world.encounters().member_died(*entity);
        // The death action of the slice: the body falls and stays on the ground. The client
        // starts this action only when the server starts it.
        const gamedata::CreatureSlice& body = *entity->creature.body;
        if (body.deathAction) {
            start_action(*entity, *body.deathAction);
            if (!body.clearsReaction && entity->creature.hitByCharacter) {
                // Stage QG9c [agent; client 0x4871B60, 0x48731C0, 0x67A3BA0; live: the client stops at the kill of Medusa after a hit,
                // also by the kill command]: the client of a player that hit the creature keeps the reaction that it predicted pending
                // (the slice has no action with the ClearDamaged activity), and its repair of the action history inserts two states in
                // each frame while the time of the death fragment is below its length; the walk of the history then reads an empty entry
                // (0x4871DB6, 0x4871FDA). A new client entity of the body has no pending reaction (live: the same kill after a new interest
                // of the creature stops nothing). Each session that observes the creature makes it again (RepSession::update_interest);
                // its first record has the death action from its start.
                ++entity->creature.renewal;
                ++m_counts.renewals;
                m_world.creature_renewed();
                log::write("world: creature %016llx (%s) is made again on each client that observes it: its slice has no action that ends "
                           "the reaction that the client of a hit predicts",
                           static_cast<unsigned long long>(entity->id), entity->creature.kind->id.c_str());
            }
        } else if (entity->creature.action || entity->creature.moving) {
            // A slice with no death action: the action that plays (a reaction, an attack, the
            // locomotion of a way) is not the death action of the body. A session that observes
            // the body from now on gets no such action: its first state has the spawn action of
            // the slice, when the slice has one. A session that has the action gets the scope
            // fields with no record: the client keeps the scope fields of a creature, and it would
            // keep the run of a dead creature (code review of the corrections after user test 1,
            // finding F2).
            action_exits(entity->creature, m_world.clock().now());
            entity->creature.action.reset();
            m_acting.erase(entity->id);
            entity->creature.moving.reset();
            m_world.touch(entity->id, ComponentKey::movement);
        }
        // A dead creature does not walk (start_action ended the locomotion for a death action), and it has no idle record and no hit stun.
        entity->creature.moving.reset();
        entity->creature.idle.reset();
        entity->creature.hitStunUntil.reset();
        // Its effects end (RemoveOnDeath of the rows; stage 3.4f-D: its heal has no tick more, its buff ends).
        sweep(*entity);
    }
    m_world.touch(id, ComponentKey::vitals);
    result.entity = entity;
    return result;
}

HitReaction hit_reaction(const gamedata::DamageRow& row) {
    HitReaction reaction;
    if (!row.reaction) {
        return reaction; // A file of a tool from before user test 2: the reaction of stage 3.2a.
    }
    reaction.ruled = true;
    reaction.plays = !row.reaction->none;
    reaction.pushable = row.reaction->none;
    reaction.power = static_cast<std::uint32_t>(std::max(row.reaction->power_on_creature(), 0));
    reaction.stunFrames = static_cast<std::uint32_t>(std::max(row.reaction->stun_on_creature(), 0));
    reaction.attack = row.attackType == "Light"     ? HitReaction::Attack::light
                      : row.attackType == "Heavy"   ? HitReaction::Attack::heavy
                      : row.attackType == "Ability" ? HitReaction::Attack::ability
                                                    : HitReaction::Attack::other;
    return reaction;
}

bool Creatures::reaction_allowed(const Entity& entity, const HitReaction& reaction, WorldClock::time_point now) const {
    if (!reaction.ruled) {
        return true; // The reaction of stage 3.2a (a command, a file of an older tool).
    }
    // The activation condition of the action Reaction (baseai_reaction.actionlist, game data), the parts that the backend has: the attack
    // type of the row, its power level, the creature type of the kind, and the reaction limit. The grit of an attack, a stun, and a
    // knockdown are the conditions of the caller.
    const std::string& type = entity.creature.kind->creatureType;
    if (type_in(type, kBossReactionTypes)) {
        return false;
    }
    const bool limited = entity.creature.reactionLimitUntil && now < *entity.creature.reactionLimitUntil;
    if (limited && type_in(type, kLimitedReactionTypes)) {
        return false;
    }
    const std::uint32_t power = reaction.power.value_or(0);
    switch (reaction.attack) {
    case HitReaction::Attack::light:
        return !limited && type_in(type, kLightReactionTypes);
    case HitReaction::Attack::heavy:
        return !limited && type_in(type, kHeavyReactionTypes);
    case HitReaction::Attack::ability:
        // The type Ability of power 1 or more: each creature type that is not a boss; of a power below 1: the rule of a light attack (the
        // branch below power 1 of the action list names IfDamagedByAttackType Ability).
        return power >= 1 || (!limited && type_in(type, kLightReactionTypes));
    case HitReaction::Attack::other:
        // A different type that is not Light or Heavy (Magic, MagicSpell, ...): only the branch of power 1 or more (code review of user test
        // 2, finding U2-R2: a Magic row of power 0 plays no reaction and gives no hit stun).
        return power >= 1;
    }
    return false;
}

std::chrono::milliseconds Creatures::reaction_length(const Entity& entity, const HitReaction& reaction) const {
    const gamedata::CreatureSlice& body = *entity.creature.body;
    std::optional<std::size_t> level;
    if (reaction.power) {
        level = reaction_level(*reaction.power);
    } else if (body.hitAction) {
        // The fragment of the action Reaction (r_R2_F of each slice of the data): the power whose fragment it is.
        const std::int32_t fragment = body.actions[*body.hitAction].fragmentIndex;
        for (std::size_t power = 0; power < body.reactions.size() && !level; ++power) {
            if (body.reactions[power] >= 0 && body.reactions[power] == fragment) {
                level = power;
            }
        }
    }
    if (level && body.reactionLengths[*level] > 0.0F) {
        return std::chrono::milliseconds(std::max<long long>(std::lround(body.reactionLengths[*level] * 1000.0F), 1));
    }
    return kReactionTime;
}

CreatureResult Creatures::hurt(EntityId id, float damage, EntityId attacker, EntityId threat, const HitReaction& reaction,
                               HitPush* push) {
    HitPush pushed;
    CreatureResult result;
    Entity* entity = find(id);
    if (entity == nullptr) {
        result.error = CreatureError::unknownCreature;
        return result;
    }
    if (entity->creature.dead) {
        result.error = CreatureError::dead;
        return result;
    }
    if (!std::isfinite(damage) || damage < 0.0F) {
        result.error = CreatureError::badHealth;
        return result;
    }
    if (entity->creature.immune()) {
        // On its way back a creature takes no damage, no reaction, and no target by a hit.
        result.error = CreatureError::immune;
        return result;
    }
    // One path with the health command and the kill command: the health 0 is the death.
    const float dealt = std::min(damage, entity->vitals.health);
    if (attacker != 0) {
        // Stage QG9c: the client of the character predicts a reaction of the creature at its hit (the death rule of set_health).
        if (const Entity* source = m_world.find(attacker); source != nullptr && source->kind == EntityKind::player) {
            entity->creature.hitByCharacter = true;
        }
    }
    result = set_health(id, std::max(entity->vitals.health - damage, 0.0F));
    // The credit of the kill: the damage of each character (world/rewards.h). A death by a hit of
    // a character gives the rewards of the kill to the characters with the credit.
    Entity* character = attacker != 0 && result.error == CreatureError::none ? m_world.find(attacker) : nullptr;
    if (character != nullptr && character->kind == EntityKind::player) {
        m_world.rewards().hit(*entity, *character, dealt);
        if (entity->creature.dead) {
            m_world.rewards().killed(*entity);
        }
    }
    if (result.error == CreatureError::none && !entity->creature.dead) {
        // A stun that ends at the next hit (the stun of Shockwave) ends now (stage 3.3a), also on a stationary target (code review of stage
        // QG5c1, finding QG5C1R-R1: the status is a part of the target too).
        const auto breaks = std::find_if(entity->creature.effects.begin(), entity->creature.effects.end(),
                                         [](const CreatureEffect& effect) { return effect.stun && effect.endsOnHit; });
        if (breaks != entity->creature.effects.end()) {
            breaks->ends = m_world.clock().now();
            sweep(*entity);
        }
        // A stationary target (stage QG5c1: a totem, a nest) takes the damage only: no reaction, no push, and no target.
        if (!entity->creature.body->stationary()) {
            react(*entity, reaction, pushed);
            // The creature takes the attacker as its target, or the threat of the hit (stage 3.3b-S2b: the Pylon that shot it).
            if (threat != 0 || attacker != 0) {
                m_behavior.hurt_by(*entity, threat != 0 ? threat : attacker);
            }
        }
    }
    if (push != nullptr) {
        *push = pushed;
    }
    return result;
}

void Creatures::react(Entity& entity, const HitReaction& reaction, HitPush& push) {
    Creature& creature = entity.creature;
    const gamedata::CreatureSlice& body = *creature.body;
    const WorldClock::time_point now = m_world.clock().now();
    // The end of the reaction limit (the exit of the tag ReactionLimiter_Active, game data): the count of the chained reactions starts
    // again (ResetReactionCount OnExit of ReactionLimiter_Active, baseai_reaction.actionlist; stage U3).
    if (creature.reactionLimitUntil && now >= *creature.reactionLimitUntil && !creature.reactionLimitAction) {
        creature.reactionLimitUntil.reset();
        creature.reactionChain = 0;
        ++m_counts.chainResets;
    }
    const bool limited = creature.reactionLimitUntil.has_value();
    // The grit of an attack: a creature of the backend that plays an attack, a shot, a cast, or a status action has grit (the ActivateGrit
    // clips of the attacks, game data). A hit then plays no full reaction (the reaction would take the action of the attack away, and a
    // player that hits fast would stop each attack), but the additive reaction (stage U3). A creature that an effect holds keeps the action
    // of the effect (the stun, the knockdown).
    const bool grit = creature.mind.attack.has_value() || creature.mind.shooting;
    const bool held = creature.held(now);
    const bool canReact = body.hitAction && !grit && !held;
    const float angle = reaction.source ? hit_angle(entity, *reaction.source) : 0.0F;
    // User test 2 (UT2-9) and stage U3: the reaction of the damage row of the hit, by the rule of the action Reaction of the action lists
    // (game data, reaction_allowed): no reaction for a tick of a damage over time and for NoReaction; the row of the reaction table of the
    // slice for the power and the side of the hit (research Q2); its length with the hit stun (research Q3); a hit stun of HitStun_AI frames
    // (the behavior waits for it). The push of the impact of the row is an activity of the reaction (or of the additive reaction, or of the
    // action Pushable for a row with NoReaction): with none, the hit moves nothing.
    if (reaction.plays && canReact && reaction_allowed(entity, reaction, now)) {
        const ReactionPlan plan = plan_reaction(entity, reaction, angle);
        if (start_action(entity, *body.hitAction, plan.length)) {
            ++m_counts.reactions;
            push.length = plan.length;
            if (plan.fragment) {
                creature.action->fragment = *plan.fragment;
                ++m_counts.reactionsOfPower;
            }
            if (plan.turn) {
                // AlignToDirection of the row (client code 0x2B9F1C0): the creature turns so that the hit comes from AlignOffsetAngle.
                creature.yaw = std::fmod(creature.yaw + *plan.turn + 360.0F, 360.0F);
                creature.mind.sentYaw = creature.yaw;
                ++m_counts.reactionTurns;
            }
            if (body.reactionTable) {
                const gamedata::PushScale& scale = push_scale(*body.reactionTable, creature, false);
                push.scale = scale.scale;
                push.pushBack = scale.pushBack;
            }
            const auto stun = std::chrono::milliseconds(static_cast<long long>(reaction.stunFrames) * 1000 / 30);
            if (stun.count() > 0) {
                creature.hitStunUntil = std::max(creature.hitStunUntil.value_or(now), now + stun);
                ++m_counts.hitStuns;
            }
            // The chain (stage U3, a decision of the orchestrator: the reading of the data): each full reaction since the last reset counts,
            // also this one. The limit of the chain (the action Reaction, game data): the third reaction of a Solo type (of a heavy attack
            // for a Solo+ or a named type), or the first one of an elite type, holds the limit for the reaction and 2 s after it
            // (AI_Reaction_Limiter, duration 2.0).
            ++creature.reactionChain;
            const std::string& type = creature.kind->creatureType;
            const bool solo = type_in(type, kLightReactionTypes);
            const bool soloPlus = !solo && type_in(type, kHeavyReactionTypes);
            if (reaction.ruled
                && ((creature.reactionChain >= kReactionChain && (solo || (soloPlus && reaction.attack == HitReaction::Attack::heavy)))
                    || (creature.reactionChain >= 1 && type_in(type, kLimitedReactionTypes)))) {
                // The limit holds while the reaction plays and 2 s after its real exit (action_exits: code review of user test 2,
                // finding U2-R3); until that exit the end of the limit is the natural end of the reaction plus 2 s.
                creature.reactionLimitUntil = now + plan.length + kReactionLimit;
                creature.reactionLimitAction = creature.action->generation;
                ++m_counts.reactionLimits;
            }
            log::write("world: reaction: creature %016llx (%s) plays the reaction of %s (fragment %u, %s %.0f degrees) for %.2f s%s, a hit "
                       "stun of %.2f s, the chain %u%s",
                       static_cast<unsigned long long>(entity.id), creature.kind->id.c_str(),
                       reaction.power ? ("power " + std::to_string(*reaction.power)).c_str() : "the action Reaction", creature.action->fragment,
                       plan.side, static_cast<double>(angle), std::chrono::duration<double>(plan.length).count(),
                       plan.clip ? " (the HitStun clip)" : "", std::chrono::duration<double>(stun).count(), creature.reactionChain,
                       creature.reactionLimitUntil && *creature.reactionLimitUntil > now ? ", the reaction limit holds" : "");
        }
        return;
    }
    ++m_counts.hitsWithNoReaction;
    if (!reaction.ruled) {
        push.length = reaction_length(entity, reaction); // The rule of stage 3.3b-S4: each hit pushes (a command).
        return;
    }
    if (reaction.pushable) {
        if (!grit) {
            push.length = kPushableTime; // The action Pushable: a row with NoReaction, no grit (no ReactionMotion conditional).
        }
        return;
    }
    // The additive reaction (stage U3, research Q5; game data: the action Reaction_Additive of baseai_reaction.actionlist, a decision of the
    // orchestrator after live check LC-Q5-2): a hit with a reaction row that plays no full reaction by the rule (the type, the attack, the
    // limit), or that comes in an attack of the creature (grit) with a power above 0. Not for a NoReaction kind (AI_NoReactions), a dead
    // creature, or a creature that a stun or a knockdown holds (a project rule: the backend has no breakout).
    const std::uint32_t power = reaction.power.value_or(0);
    if (!body.additive || !reaction.plays || creature.kind->noReactions || held
        || (reaction_allowed(entity, reaction, now) && !(grit && power > 0))) {
        return;
    }
    const gamedata::AdditiveReaction& additive = *body.additive;
    const gamedata::ReactionRow* row = select_row(additive, static_cast<std::int32_t>(power), angle);
    const std::int32_t fragment = row != nullptr && row->fragment >= 0 ? row->fragment : body.actions[additive.action].fragmentIndex;
    const float length = row != nullptr && row->length > 0.0F ? row->length : -1.0F;
    // The action ends at IfFragmentDone with ExitOffsetSeconds (0.2 s): its push (ReactionMotion) runs to that end.
    const auto plays = std::chrono::milliseconds(
        std::max<long long>(std::lround((length > 0.0F ? std::max(length - additive.exit, 0.05F) : 0.5F) * 1000.0F), 50));
    CreatureAdditive record;
    record.scope = additive.scope;
    record.record.action = additive.action;
    record.record.fragment = static_cast<std::uint32_t>(fragment);
    record.record.generation = static_cast<std::uint16_t>(++creature.actions);
    record.record.started = now;
    record.record.ends = now + plays;
    record.record.tags = body.tags;
    record.record.changed = m_world.touch(entity.id, ComponentKey::movement);
    creature.additive = record;
    m_additives.insert(entity.id);
    ++m_counts.additiveReactions;
    // ResetReactionCount OnEnter of the additive reaction with grit active and no limiter (game data): a hit in an attack of the creature
    // starts the chain again.
    if (grit && !limited && creature.reactionChain != 0) {
        creature.reactionChain = 0;
        ++m_counts.chainResets;
    }
    // In the limit a melee hit gives AI_Reaction_Limiter again (EffectsToApplyOnEnter of the additive reaction for a hit that is not ranged,
    // not on an elite type; game data): the limit holds 2 s after the hit.
    if (limited && !reaction.ranged && !type_in(creature.kind->creatureType, kLimitedReactionTypes) && !creature.reactionLimitAction) {
        creature.reactionLimitUntil = std::max(*creature.reactionLimitUntil, now + kReactionLimit);
    }
    const gamedata::PushScale& scale = push_scale(additive, creature, limited);
    push.length = plays;
    push.scale = scale.scale;
    push.pushBack = scale.pushBack;
    push.additive = true;
    log::write("world: reaction: creature %016llx (%s) plays the additive reaction (fragment %d in scope %u, %.0f degrees) for %.2f s, push "
               "scale %.2f%s",
               static_cast<unsigned long long>(entity.id), creature.kind->id.c_str(), fragment, additive.scope, static_cast<double>(angle),
               std::chrono::duration<double>(plays).count(), static_cast<double>(scale.pushBack),
               grit ? " (a hit in its attack)" : limited ? " (the reaction limit holds)" : "");
}

Creatures::ReactionPlan Creatures::plan_reaction(const Entity& entity, const HitReaction& reaction, float angle) const {
    ReactionPlan plan;
    const gamedata::CreatureSlice& body = *entity.creature.body;
    plan.length = reaction_length(entity, reaction);
    if (!reaction.power) {
        return plan; // A command, or a row of an older tool: the fragment of the action Reaction.
    }
    if (!body.reactionTable) {
        // A file of a tool from before stage U3: the rule of user test 2 (the front fragment of the power).
        const std::size_t level = reaction_level(*reaction.power);
        if (body.reactions[level] >= 0) {
            plan.fragment = static_cast<std::uint32_t>(body.reactions[level]);
        }
        return plan;
    }
    const gamedata::ReactionRow* row = select_row(*body.reactionTable, static_cast<std::int32_t>(*reaction.power), angle);
    if (row == nullptr) {
        plan.side = "no row";
        return plan; // No row holds: the action keeps its own fragment (client code 0x2B9F1C0).
    }
    plan.side = row->angles.empty()                                                  ? "each side"
                : angle_holds(row->angles, 0.0F)                                     ? "front"
                : angle_holds(row->angles, 180.0F) || angle_holds(row->angles, -180.0F) ? "back"
                : angle > 0.0F                                                       ? "right"
                                                                                     : "left";
    if (row->fragment >= 0) {
        plan.fragment = static_cast<std::uint32_t>(row->fragment);
    }
    if (row->length > 0.0F) {
        const float length = row->hitStun >= 0.0F ? hit_stun_length(row->length, row->hitStun, reaction.stunFrames) : row->length;
        plan.clip = row->hitStun >= 0.0F;
        plan.length = std::chrono::milliseconds(std::max<long long>(std::lround(std::max(length - body.reactionTable->exit, 0.0F) * 1000.0F), 1));
    }
    if (row->align) {
        // The turn of AlignToDirection: AlignOffsetAngle - angle about the up axis (counterclockwise: the yaw of the creature).
        float turn = std::fmod(row->alignOffset - angle + 540.0F, 360.0F) - 180.0F;
        if (std::fabs(turn) > 0.5F) {
            plan.turn = turn;
        }
    }
    return plan;
}

CreatureResult Creatures::play(EntityId id, const std::string& action, std::optional<float> seconds,
                               std::optional<gamedata::FragmentTags> tags) {
    CreatureResult result;
    Entity* entity = find(id);
    if (entity == nullptr) {
        result.error = CreatureError::unknownCreature;
        return result;
    }
    if (entity->creature.dead) {
        result.error = CreatureError::dead;
        return result;
    }
    const std::optional<std::uint32_t> index = entity->creature.body->find_action(action);
    if (!index) {
        result.error = CreatureError::unknownAction;
        return result;
    }
    start_action(*entity, *index,
                 seconds ? std::optional<std::chrono::milliseconds>(
                               std::chrono::milliseconds(static_cast<long long>(std::max(*seconds, 0.0F) * 1000.0F)))
                         : std::nullopt);
    if (tags && entity->creature.action) {
        entity->creature.action->tags = *tags;
    }
    result.entity = entity;
    return result;
}

bool Creatures::start_action(Entity& entity, std::uint32_t action, std::optional<std::chrono::milliseconds> length) {
    const gamedata::CreatureSlice& body = *entity.creature.body;
    if (action >= body.actions.size() || body.actions[action].fragmentIndex < 0) {
        return false;
    }
    action_exits(entity.creature, m_world.clock().now());
    CreatureAction started;
    started.action = action;
    started.fragment = static_cast<std::uint32_t>(body.actions[action].fragmentIndex);
    // The count of the actions of the creature stays when an action ended: a new action must
    // not have the count of an action that a client got.
    started.generation = static_cast<std::uint16_t>(++entity.creature.actions);
    started.started = m_world.clock().now();
    started.tags = body.tags;
    if (length) {
        started.ends = started.started + *length;
        m_acting.insert(entity.id);
    } else {
        m_acting.erase(entity.id);
    }
    started.changed = m_world.touch(entity.id, ComponentKey::movement);
    entity.creature.action = started;
    // The action of the server replaces the locomotion: an attack and a reaction stop the walk. Its end gives a new idle record (rest).
    entity.creature.moving.reset();
    entity.creature.idle.reset();
    return true;
}

void Creatures::move(Entity& entity, const state::WorldPosition& position, Gait gait) {
    m_world.place_creature(entity, position);
    const Revision revision = m_world.touch(entity.id, ComponentKey::movement);
    if (gait == Gait::place) {
        return;
    }
    Creature& creature = entity.creature;
    // A step replaces the idle record: the end of the walk gives a new one (stop, rest).
    creature.idle.reset();
    // The step replaces an action of the server that plays (a reaction to a hit, the action of
    // a command): a creature that changes its place plays its locomotion, not a different
    // action (user test 1: a creature that moved in the pose of an attack). The behavior does
    // not move a creature while its attack plays. A slice with no locomotion action ends the
    // action too: its observers get the scope fields with no record (code review of the
    // corrections after user test 1, finding F7).
    if (creature.action) {
        action_exits(creature, m_world.clock().now());
        creature.action.reset();
        m_acting.erase(entity.id);
    }
    const std::optional<std::uint32_t> locomotion =
        gait == Gait::walk && creature.body->walkAction ? creature.body->walkAction : creature.body->moveAction;
    if (!locomotion) {
        creature.moving.reset();
        return;
    }
    if (creature.moving && creature.moving->action == *locomotion) {
        return; // One record for the whole way: the client plays the locomotion from it.
    }
    const WorldClock::time_point now = m_world.clock().now();
    CreatureAction moving;
    moving.action = *locomotion;
    moving.fragment = static_cast<std::uint32_t>(creature.body->actions[moving.action].fragmentIndex);
    moving.generation = static_cast<std::uint16_t>(++creature.actions);
    moving.started = now;
    moving.tags = creature.body->tags;
    moving.changed = revision;
    creature.moving = moving;
}

void Creatures::immune_changed(Entity& entity) {
    entity.creature.immuneChanged = m_world.touch(entity.id, ComponentKey::vitals);
}

void Creatures::stop(Entity& entity) {
    if (!entity.creature.moving) {
        return;
    }
    entity.creature.moving.reset();
    // The end of the walk (user test 2, UT2-2): the idle record, not the scope fields with no record.
    rest(entity);
}

void Creatures::rest(Entity& entity) {
    Creature& creature = entity.creature;
    const Revision revision = m_world.touch(entity.id, ComponentKey::movement);
    if (creature.dead || !creature.body->idleAction || creature.body->actions[*creature.body->idleAction].fragmentIndex < 0) {
        creature.idle.reset(); // No idle action: the scope fields with no record (the rule before user test 2).
        return;
    }
    // One record with a new start count for each end (the client starts the looping idle fragment from it), and the time of now.
    CreatureAction idle;
    idle.action = *creature.body->idleAction;
    idle.fragment = static_cast<std::uint32_t>(creature.body->actions[idle.action].fragmentIndex);
    idle.generation = static_cast<std::uint16_t>(++creature.actions);
    idle.started = m_world.clock().now();
    idle.tags = creature.body->tags;
    idle.changed = revision;
    creature.idle = idle;
    ++m_counts.idleRecords;
}

void Creatures::moved(Entity& entity) {
    m_world.touch(entity.id, ComponentKey::movement);
}

void Creatures::restore(Entity& entity) {
    // At its anchor with its full health: the damage of the fight gives no credit any more.
    m_world.rewards().restored(entity);
    if (entity.creature.dead || entity.vitals.health == entity.vitals.healthMax) {
        return;
    }
    entity.vitals.health = entity.vitals.healthMax;
    m_world.touch(entity.id, ComponentKey::vitals);
}

bool Creatures::apply_effect(Entity& entity, const CreatureEffect& effect) {
    Creature& creature = entity.creature;
    if (creature.dead || creature.immune()) {
        return false;
    }
    const WorldClock::time_point now = m_world.clock().now();
    // An entry of the same status whose end came, and that the sweep of the due list did not take yet (a world task or a hit of a client item
    // before the sweep), is not renewed (code review of stage B2, finding B2-R2: a hit after the end of 2 charges gave 3 charges). The sweep
    // of the creature first: the ticks due before the end come (review SU-R2), then the entry ends; the application starts a new entry with
    // its initial stacks and a new serial. Inside a sweep or a tick the old entry only ends (its due ticks stay in the due list of the sweep).
    const auto ended = [&]() {
        return std::find_if(creature.effects.begin(), creature.effects.end(),
                            [&](const CreatureEffect& existing) { return existing.status == effect.status && existing.ends <= now; });
    };
    if (ended() != creature.effects.end()) {
        if (m_effectDepth == 0) {
            sweep(entity);
            if (creature.dead || creature.immune()) {
                return false;
            }
        }
        if (const auto old = ended(); old != creature.effects.end()) {
            if (old->shown) {
                m_world.touch(entity.id, ComponentKey::status);
            }
            creature.effects.erase(old);
        }
    }
    const bool wasHeld = creature.held(now);
    const auto same = std::find_if(creature.effects.begin(), creature.effects.end(),
                                   [&](const CreatureEffect& existing) { return existing.status == effect.status; });
    WorldClock::time_point end = effect.ends;
    if (same != creature.effects.end()) {
        // The same status again (stage B2: the rule of a character, statuses.h): one more stack to StackMax (a value below 1 is 1: client
        // loader 0x44B65F5), the end by RefreshDuration and DurationMax (Statuses::renewed_end), and at StackMax with DontReplaceStack no
        // change. An effect with no row (the rend of a perk) keeps the later end. The ticks of a heal keep their times: the same row again
        // gives no second tick (a decision of an agent; the rule of a character). A tick that the old end stopped comes when the new end
        // is later than it (review SU-R1): a tick is in the due list only before the end.
        const gamedata::StatusRow* row = effect.row != nullptr ? effect.row : same->row;
        const WorldClock::time_point before = same->ends;
        if (row != nullptr) {
            const auto stackMax = static_cast<std::uint8_t>(std::clamp(row->cells.number("StackMax"), 1.0F, 127.0F));
            if (row->cells.flag("DontReplaceStack") && same->stacks >= stackMax) {
                return false;
            }
            const std::uint8_t stacks = std::min<std::uint8_t>(static_cast<std::uint8_t>(same->stacks + 1U), stackMax);
            if (stacks != same->stacks) {
                same->stacks = stacks;
                same->speed = 1.0F + row->moveSpeed * static_cast<float>(stacks);
                ++m_counts.stacksAdded;
                if (same->shown) {
                    m_world.touch(entity.id, ComponentKey::status);
                }
            }
            same->ends = Statuses::renewed_end(*row, same->ends, now, std::chrono::duration_cast<std::chrono::milliseconds>(effect.ends - now));
        } else {
            same->ends = std::max(same->ends, effect.ends);
        }
        same->source = effect.source;
        // The damage of a tick goes with its source (code review of stage B1, finding B1-R4): the refresh takes the damage snapshot of the new
        // source (its forced row with its own weapon, its type, its rune charge). The cadence of the ticks stays (no extra tick).
        same->tickDamage = effect.tickDamage;
        same->tickType = effect.tickType;
        same->tickCharge = effect.tickCharge;
        end = same->ends;
        if (same->nextTick && same->tickRate.count() > 0 && *same->nextTick >= before && *same->nextTick < same->ends) {
            due(*same->nextTick, entity.id, same->serial);
        }
    } else {
        CreatureEffect added = effect;
        added.serial = ++m_nextEffect;
        creature.effects.push_back(added);
        if (added.nextTick) {
            due(*added.nextTick, entity.id, added.serial);
        }
        // A new icon goes to each observer (stage B2: a renewal with the same stacks changes nothing on the wire: the compact map has no
        // end; a new stack sends the list above).
        if (effect.shown) {
            m_world.touch(entity.id, ComponentKey::status);
        }
    }
    due(end, entity.id, 0);
    if (effect.stun && !wasHeld) {
        // A stun stops the attack that plays (its contacts do nothing: the action is a different
        // one) and the walk. The creature plays the action "Stun" of its slice for the stun. A shot that plays does not fly (stage
        // 3.4f-A: its release finds a different action).
        creature.mind.attack.reset();
        creature.mind.shooting = false;
        if (creature.body->stunAction) {
            start_action(entity, *creature.body->stunAction);
        } else {
            creature.moving.reset();
            action_exits(creature, m_world.clock().now());
            creature.action.reset();
            m_acting.erase(entity.id);
            rest(entity);
        }
    }
    const auto kept = std::find_if(creature.effects.begin(), creature.effects.end(),
                                   [&](const CreatureEffect& existing) { return existing.status == effect.status; });
    log::write("world: effect: creature %016llx (%s) has %s (%u stack(s)) for %.1f s%s%s%s",
               static_cast<unsigned long long>(entity.id),
               creature.kind->id.c_str(),
               effect.status.c_str(),
               kept != creature.effects.end() ? static_cast<unsigned>(kept->stacks) : 1U,
               std::chrono::duration<double>(end - now).count(),
               effect.stun ? ": stun" : "",
               effect.root ? ": root" : "",
               effect.speed < 1.0F ? ": slow" : "");
    return true;
}

bool Creatures::knock(Entity& entity, unsigned power, std::chrono::milliseconds time) {
    Creature& creature = entity.creature;
    // A stationary target (stage QG5c1) has no reaction and no action: it takes no knockdown and no stagger.
    if (creature.dead || creature.immune() || creature.body->stationary()) {
        return false;
    }
    const WorldClock::time_point now = m_world.clock().now();
    creature.mind.attack.reset();
    creature.mind.shooting = false;
    // The hold does not depend on the animation data (code review of stage 3.3a, finding F5): a slice with no
    // reaction action is held too. A new knock does not end an earlier one that ends later (finding F6): the
    // creature keeps the later end, and the stronger reaction that plays stays until its own end.
    const WorldClock::time_point end = now + time;
    const bool longer = !creature.knockedUntil || end > *creature.knockedUntil || now >= *creature.knockedUntil;
    creature.knockedUntil = longer ? end : *creature.knockedUntil;
    due(*creature.knockedUntil, entity.id, 0);
    // The reaction of the power: the action "Reaction" with the fragment of that power of the
    // slice (r_R4_F for a knockdown), or with the fragment of its action. A slice with no such action plays no
    // reaction: its observers get the scope with no record of the attack that it stopped.
    if (longer && creature.body->hitAction && start_action(entity, *creature.body->hitAction, time)) {
        const std::size_t level = std::min<std::size_t>(power, creature.body->reactions.size() - 1);
        if (creature.body->reactions[level] >= 0) {
            creature.action->fragment = static_cast<std::uint32_t>(creature.body->reactions[level]);
        }
    } else if (longer && !creature.body->hitAction && creature.action) {
        action_exits(creature, m_world.clock().now());
        creature.action.reset();
        m_acting.erase(entity.id);
        rest(entity);
    }
    log::write("world: effect: creature %016llx (%s) has a reaction of power %u for %.1f s (it does not move and does not attack)",
               static_cast<unsigned long long>(entity.id),
               creature.kind->id.c_str(),
               power,
               std::chrono::duration<double>(time).count());
    return true;
}

void Creatures::end_stun(Entity& entity) {
    Creature& creature = entity.creature;
    if (creature.body->stunAction && creature.action && creature.action->action == *creature.body->stunAction) {
        // The client keeps the scope fields of a creature: the next movement state has the idle record (user test 2, UT2-2), and the
        // client plays the idle action (creature_scope).
        action_exits(creature, m_world.clock().now());
        creature.action.reset();
        m_acting.erase(entity.id);
        rest(entity);
        return;
    }
    m_world.touch(entity.id, ComponentKey::movement);
}

void Creatures::action_exits(Creature& creature, WorldClock::time_point now) {
    if (creature.reactionLimitAction && creature.action && creature.action->generation == *creature.reactionLimitAction) {
        // The status AI_Reaction_Limiter (2 s, EffectsToApplyOnExit of the action Reaction) starts at the exit of the reaction.
        creature.reactionLimitUntil = now + kReactionLimit;
        creature.reactionLimitAction.reset();
    }
}

void Creatures::due(WorldClock::time_point at, EntityId creature, std::uint64_t serial) {
    m_effectDue.emplace(at, EffectDue{creature, serial});
}

void Creatures::end_effects() {
    const WorldClock::time_point now = m_world.clock().now();
    while (!m_effectDue.empty() && m_effectDue.begin()->first <= now) {
        const WorldClock::time_point at = m_effectDue.begin()->first;
        const EffectDue entry = m_effectDue.begin()->second;
        m_effectDue.erase(m_effectDue.begin());
        ++m_counts.effectDueTaken;
        Entity* entity = find(entry.creature);
        if (entity == nullptr) {
            continue; // The creature left the world: its entries are old.
        }
        if (entry.serial == 0) {
            sweep(*entity);
        } else {
            effect_tick(*entity, entry.serial, at);
        }
    }
}

void Creatures::sweep(Entity& entity) {
    const WorldClock::time_point now = m_world.clock().now();
    Creature& creature = entity.creature;
    ++m_effectDepth;
    struct Leave {
        unsigned& depth;
        ~Leave() {
            --depth;
        }
    } leave{m_effectDepth};
    // The ticks that are due before the end of an effect come before the effect ends: a late world step can take an earlier entry of
    // the same creature first (review SU-R2). A death ends the ticks.
    std::vector<std::pair<std::uint64_t, WorldClock::time_point>> pending;
    for (const CreatureEffect& effect : creature.effects) {
        if (!creature.dead && effect.ends <= now && effect.nextTick && *effect.nextTick < effect.ends) {
            pending.emplace_back(effect.serial, *effect.nextTick);
        }
    }
    for (const auto& [serial, at] : pending) {
        effect_tick(entity, serial, at);
    }
    bool stunEnded = false;
    bool shownEnded = false;
    std::vector<std::string> onTickEnded;
    for (auto effect = creature.effects.begin(); effect != creature.effects.end();) {
        ++m_counts.effectsLooked;
        if (now < effect->ends && !creature.dead) {
            ++effect;
            continue;
        }
        stunEnded = stunEnded || effect->stun;
        shownEnded = shownEnded || effect->shown;
        // An effect with an OnTickStatusEffect and no KeepOnTickEffectOnEnd takes the effect of its ticks with it (game data, stage 3.4f-D).
        if (effect->row != nullptr && !effect->row->cells.text("OnTickStatusEffect").empty()
            && effect->row->cells.number("KeepOnTickEffectOnEnd") == 0.0F) {
            onTickEnded.emplace_back(effect->row->cells.text("OnTickStatusEffect"));
        }
        if (!creature.dead) {
            log::write("world: effect: %s of creature %016llx (%s) ends", effect->status.c_str(), static_cast<unsigned long long>(entity.id),
                       creature.kind->id.c_str());
        }
        effect = creature.effects.erase(effect);
    }
    for (const std::string& status : onTickEnded) {
        std::erase_if(creature.effects, [&](const CreatureEffect& effect) {
            const bool ends = effect.status == status;
            shownEnded = shownEnded || (ends && effect.shown);
            return ends;
        });
    }
    if (creature.knockedUntil && (now >= *creature.knockedUntil || creature.dead)) {
        creature.knockedUntil.reset();
    }
    if (shownEnded) {
        m_world.touch(entity.id, ComponentKey::status);
    }
    if (stunEnded && !creature.dead && !creature.held(now)) {
        end_stun(entity);
    }
}

void Creatures::effect_tick(Entity& entity, std::uint64_t serial, WorldClock::time_point at) {
    const WorldClock::time_point now = m_world.clock().now();
    ++m_effectDepth;
    struct Leave {
        unsigned& depth;
        ~Leave() {
            --depth;
        }
    } leave{m_effectDepth};
    const auto find_effect = [&]() {
        return std::find_if(entity.creature.effects.begin(), entity.creature.effects.end(),
                            [serial](const CreatureEffect& effect) { return effect.serial == serial; });
    };
    auto effect = find_effect();
    if (effect == entity.creature.effects.end() || !effect->nextTick || *effect->nextTick != at || effect->tickRate.count() <= 0) {
        return; // An old entry: the effect ended, or it has a newer tick.
    }
    WorldClock::time_point next = at;
    while (effect != entity.creature.effects.end() && next <= now && next < effect->ends && !entity.creature.dead) {
        ++m_counts.effectTicks;
        const float amount = effect->healFixed + effect->healPart * entity.vitals.healthMax;
        const std::string status = effect->status;
        const gamedata::StatusRow* row = effect->row;
        const EntityId source = effect->source;
        const std::chrono::milliseconds rate = effect->tickRate;
        if (amount > 0.0F) {
            (void)heal(entity, amount, status);
        }
        // The damage of a tick (stage B1: the burn of Flame Arrow) through the hit path of a damage over time, with the source as the
        // attacker; after this step of the due list (a hit can end the creature and change the entities of the world).
        if (effect->tickDamage > 0.0F && source != 0) {
            const EntityId target = entity.id;
            const float hurt = effect->tickDamage;
            const std::string type = effect->tickType;
            const float charge = effect->tickCharge;
            m_world.after(std::chrono::milliseconds(0), [source, target, hurt, type, charge, status](World& world) {
                world.gear().tick_damage(source, target, hurt, type, status.c_str(), charge);
            });
            ++m_counts.damageTicks;
        }
        // OnTickStatusEffect of the row at each tick (game data: AI_Inquisitor_RampantFlame of AI_InqCleric_Rekindle).
        if (row != nullptr && !row->cells.text("OnTickStatusEffect").empty()) {
            const gamedata::AbilityData* data = m_world.options().abilityData;
            if (const gamedata::StatusRow* onTick = data != nullptr ? data->status(row->cells.text("OnTickStatusEffect")) : nullptr) {
                (void)give_status(entity, *onTick, source);
            }
        }
        next += rate;
        effect = find_effect(); // A status of the tick changes the list of the effects.
    }
    if (effect != entity.creature.effects.end()) {
        if (entity.creature.dead) {
            effect->nextTick.reset();
        } else {
            // The next tick on the cadence stays also after the end (review SU-R1); it is due only before the end.
            effect->nextTick = next;
            if (next < effect->ends) {
                due(next, entity.id, serial);
            }
        }
    }
}

float Creatures::heal(Entity& entity, float amount, std::string_view what) {
    if (entity.kind != EntityKind::creature || entity.creature.dead || !std::isfinite(amount) || !(amount > 0.0F)) {
        return 0.0F;
    }
    const float before = entity.vitals.health;
    entity.vitals.health = std::min(entity.vitals.health + amount, entity.vitals.healthMax);
    const float gained = entity.vitals.health - before;
    ++m_counts.heals;
    m_counts.healed += gained;
    if (gained > 0.0F) {
        m_world.touch(entity.id, ComponentKey::vitals);
    }
    log::write("world: heal: creature %016llx (%s) gets %.1f health of %.*s, health %.1f to %.1f of %.1f",
               static_cast<unsigned long long>(entity.id), entity.creature.kind->id.c_str(), static_cast<double>(gained),
               static_cast<int>(what.size()), what.data(), static_cast<double>(before), static_cast<double>(entity.vitals.health),
               static_cast<double>(entity.vitals.healthMax));
    return gained;
}

bool Creatures::give_status(Entity& entity, const gamedata::StatusRow& row, EntityId source, std::optional<std::chrono::milliseconds> time,
                            const StatusDamage* damage) {
    Creature& creature = entity.creature;
    if (entity.kind != EntityKind::creature || creature.dead || creature.immune()) {
        ++m_counts.statusesRefused;
        return false;
    }
    const WorldClock::time_point now = m_world.clock().now();
    if (removal_blocks(entity, row)) {
        ++m_counts.statusesRefused;
        return false;
    }
    // Stage B2: ApplicationCooldown, one application of the row for each creature in that time (the rule of a character).
    if (!application_ready(entity, row, false)) {
        ++m_counts.applicationCooldowns;
        ++m_counts.statusesRefused;
        return false;
    }
    // A dispel (game data): RemoveStatusEffects and RemoveStatusEffectCategories of the row end those effects of the creature first (the
    // Null Chamber of the void gauntlet: the category Buff of the enemies; stage B2: the bolt of the Boltcaster ends the charges).
    (void)remove_statuses(entity, row.cells.text("RemoveStatusEffects"), row.cells.text("RemoveStatusEffectCategories"), row.id);
    CreatureEffect effect;
    effect.status = row.id;
    effect.row = &row;
    effect.index = row.index;
    effect.shown = row.nameplate;
    effect.stun = row.stunned;
    effect.root = row.root;
    const auto stackMax = static_cast<unsigned>(std::clamp(row.cells.number("StackMax"), 1.0F, 127.0F));
    effect.stacks = static_cast<std::uint8_t>(std::clamp(static_cast<unsigned>(std::max(row.cells.number("InitialStackSize"), 1.0F)), 1U, stackMax));
    effect.speed = 1.0F + row.moveSpeed * static_cast<float>(effect.stacks);
    effect.damage = row.damage;
    effect.typedDamage = std::any_of(row.cells.numbers.begin(), row.cells.numbers.end(),
                                     [](const auto& cell) { return cell.first.rfind("DMG", 0) == 0; });
    effect.endsOnHit = row.stopOnHit != 0;
    effect.source = source;
    const float rate = row.cells.number("TickRate");
    effect.healPart = row.cells.number("HealthModifierPercent") > 0.0F ? row.cells.number("HealthModifierPercent") : 0.0F;
    effect.healFixed = row.cells.number("Health") > 0.0F ? row.cells.number("Health") : 0.0F;
    if (damage != nullptr && damage->amount > 0.0F && !damage->type.empty()) {
        effect.tickDamage = damage->amount;
        effect.tickType = damage->type;
        effect.tickCharge = damage->charge;
    }
    auto lasting = time ? *time : std::chrono::milliseconds(std::lround(row.duration * 1000.0F));
    // Stage B4: the outgoing time factor of a character that gives the status (Battle Precision +0.2 for a Debilitate or a DoT, Dex_Bonus_150_1
    // +0.05 for a DoT; Passives::outgoing_factor), then DurationMax, as the time factor of a status on a character.
    if (lasting.count() > 0 && source != 0) {
        if (const Entity* giver = m_world.find(source); giver != nullptr && giver->kind == EntityKind::player) {
            if (const float factor = m_world.passives().outgoing_factor(*giver, entity, row); factor != 1.0F) {
                lasting = std::chrono::milliseconds(std::max<long long>(std::lround(static_cast<float>(lasting.count()) * factor), 1));
                if (const float most = row.cells.number("DurationMax"); most > 0.0F) {
                    lasting = std::min(lasting, std::chrono::milliseconds(std::lround(most * 1000.0F)));
                }
            }
        }
    }
    if (lasting.count() <= 0) {
        // No duration: one application now (the rule of a character): the heal, the dispel above, and (stage B2) one damage of the source
        // (the bolt of the Boltcaster: HealthModifierDamageBased -0.8 of the damage of the hit, Lightning) through the path of a tick.
        (void)application_ready(entity, row, true);
        ++m_counts.statusesGiven;
        const float amount = effect.healFixed + effect.healPart * entity.vitals.healthMax;
        if (amount > 0.0F) {
            (void)heal(entity, amount, row.id);
        }
        if (effect.tickDamage > 0.0F && source != 0) {
            const EntityId target = entity.id;
            const float hurt = effect.tickDamage;
            const std::string type = effect.tickType;
            const float charge = effect.tickCharge;
            const std::string status = row.id;
            m_world.after(std::chrono::milliseconds(0), [source, target, hurt, type, charge, status](World& world) {
                world.gear().tick_damage(source, target, hurt, type, status.c_str(), charge);
            });
            ++m_counts.instantDamage;
        }
        // Stage B2: the icon and the visual effect of a row with ShowInNameplates: the effect stays for its DurationMax (live check L2 of
        // stage B2: the compact entry of Status_Perk_Artifact_Set1_Bow_StaticDischarge, DurationMax 0.1, plays the lightning strike of its
        // FxScriptOn at the creature; with no entry the client shows nothing). No tick.
        if (const float most = row.cells.number("DurationMax"); row.nameplate && most > 0.0F) {
            effect.tickDamage = 0.0F;
            effect.tickType.clear();
            effect.ends = now + std::chrono::milliseconds(std::max<long long>(std::lround(most * 1000.0F), 1));
            (void)apply_effect(entity, effect);
        }
        return true;
    }
    effect.ends = now + lasting;
    if (rate > 0.0F && (effect.healPart > 0.0F || effect.healFixed > 0.0F || effect.tickDamage > 0.0F || !row.cells.text("OnTickStatusEffect").empty())) {
        // The ticks (the rule of a character, statuses.h): the first one at the next tick of the world, or one TickRate later with
        // DelayInitialTick; then each TickRate, while it is before the end.
        effect.tickRate = std::chrono::milliseconds(std::max<long long>(std::lround(rate * 1000.0F), 1));
        effect.nextTick = row.cells.number("DelayInitialTick") != 0.0F ? now + effect.tickRate : now;
    }
    const bool given = apply_effect(entity, effect);
    if (given) {
        (void)application_ready(entity, row, true);
    }
    ++(given ? m_counts.statusesGiven : m_counts.statusesRefused);
    return given;
}

bool Creatures::removal_blocks(const Entity& entity, const gamedata::StatusRow& row) {
    // Stage B2, the removal window: a running effect whose row removes statuses (RemoveStatusEffects, RemoveStatusEffectCategories) refuses
    // those rows while it runs. Game data: Status_Perk_Artifact_Set1_Bow_StaticCharge_Removal (0.2 s) is the partner of the bolt of the
    // Boltcaster and takes the charge that the hit of the bolt adds; Status_SetBonusInfix_Focused_CCEffectImmunity (1 s, the category CC)
    // has the same form. A reading of the data (a decision of an agent: the client has no add code of the server).
    const WorldClock::time_point now = m_world.clock().now();
    for (const CreatureEffect& running : entity.creature.effects) {
        if (running.row == nullptr || now >= running.ends || data_rules::same_text(running.status, row.id)) {
            continue;
        }
        const std::string_view rows = running.row->cells.text("RemoveStatusEffects");
        const std::string_view categories = running.row->cells.text("RemoveStatusEffectCategories");
        bool blocked = data_rules::list_has(rows, row.id);
        for (const std::string_view category : data_rules::parts_of(row.cells.text("EffectCategories"))) {
            blocked = blocked || data_rules::list_has(categories, category);
        }
        if (blocked) {
            ++m_counts.statusesBlocked;
            log::write("world: effect: creature %016llx (%s) takes no %s: %s removes it", static_cast<unsigned long long>(entity.id),
                       entity.creature.kind->id.c_str(), row.id.c_str(), running.status.c_str());
            return true;
        }
    }
    return false;
}

bool Creatures::application_ready(const Entity& entity, const gamedata::StatusRow& row, bool start) {
    const float wait = row.cells.number("ApplicationCooldown");
    if (!(wait > 0.0F)) {
        return true;
    }
    const WorldClock::time_point now = m_world.clock().now();
    if (m_applications.size() > 256) {
        for (auto entry = m_applications.begin(); entry != m_applications.end();) {
            entry = entry->second <= now ? m_applications.erase(entry) : std::next(entry);
        }
    }
    const auto key = std::make_pair(entity.id, data_rules::lower(row.id));
    if (const auto found = m_applications.find(key); found != m_applications.end() && now < found->second) {
        return false;
    }
    if (start) {
        m_applications[key] = now + std::chrono::milliseconds(std::max<long long>(std::lround(wait * 1000.0F), 1));
    }
    return true;
}

std::uint32_t Creatures::stacks_of(const Entity& entity, std::string_view status, EntityId source) const {
    const WorldClock::time_point now = m_world.clock().now();
    for (const CreatureEffect& effect : entity.creature.effects) {
        if (now < effect.ends && data_rules::same_text(effect.status, status) && (source == 0 || effect.source == source)) {
            return effect.stacks;
        }
    }
    return 0;
}

unsigned Creatures::remove_statuses(Entity& entity, std::string_view rows, std::string_view categories, std::string_view cause) {
    if (rows.empty() && categories.empty()) {
        return 0;
    }
    Creature& creature = entity.creature;
    const WorldClock::time_point now = m_world.clock().now();
    bool shownEnded = false;
    bool stunEnded = false;
    unsigned ended = 0;
    std::erase_if(creature.effects, [&](const CreatureEffect& effect) {
        bool ends = data_rules::list_has(rows, effect.status);
        if (!ends && effect.row != nullptr && !categories.empty()) {
            for (const std::string_view category : data_rules::parts_of(effect.row->cells.text("EffectCategories"))) {
                ends = ends || data_rules::list_has(categories, category);
            }
        }
        if (ends) {
            ++ended;
            ++m_counts.dispelled;
            shownEnded = shownEnded || effect.shown;
            stunEnded = stunEnded || effect.stun;
            log::write("world: effect: %s of creature %016llx (%s) ends: a removal of %.*s", effect.status.c_str(),
                       static_cast<unsigned long long>(entity.id), creature.kind->id.c_str(), static_cast<int>(cause.size()), cause.data());
        }
        return ends;
    });
    if (shownEnded) {
        m_world.touch(entity.id, ComponentKey::status);
    }
    if (stunEnded && !creature.held(now)) {
        end_stun(entity);
    }
    return ended;
}

void Creatures::end_actions() {
    const WorldClock::time_point now = m_world.clock().now();
    std::vector<EntityId> stunned;
    for (auto acting = m_acting.begin(); acting != m_acting.end();) {
        Entity* entity = find(*acting);
        if (entity != nullptr && entity->creature.action && entity->creature.action->ends
            && now < *entity->creature.action->ends) {
            ++acting;
            continue;
        }
        if (entity != nullptr && entity->creature.action && entity->creature.action->ends) {
            // User test 2 (UT2-2): the client does not end an action of an action list by itself (NoAutoBlendOut; client code 0x2A5D630,
            // 0x6D899DD): the body keeps the last pose of the action until a new record. Each observer gets the idle record of the slice
            // now (rest: a new start count), and a later movement state keeps a valid record (the scope fields with no record make the
            // client end its own idle at each tick, 0x2A2BC20). A session that observes the creature from now on gets the idle record.
            // The spawn action of a slice that needs it is not here: it is a value of the creature (Creature::spawnAction), and the end of
            // an action does not change it, also for a creature that died while its reaction played.
            action_exits(entity->creature, m_world.clock().now());
            entity->creature.action.reset();
            // A stun that still holds the creature after a shorter knockdown: its action "Stun" comes back
            // (code review of stage 3.3a, finding F7), with a mark of the movement state, after this loop.
            const Creature& creature = entity->creature;
            bool stunHolds = false;
            if (!creature.dead && creature.body->stunAction) {
                for (const CreatureEffect& effect : creature.effects) {
                    if (effect.stun && now < effect.ends) {
                        stunned.push_back(entity->id);
                        stunHolds = true;
                        break;
                    }
                }
            }
            // A slice with no idle action keeps the rule from before user test 2: no mark (the next state has the scope fields with no
            // record).
            if (!creature.dead && !stunHolds && creature.body->idleAction) {
                rest(*entity);
            }
        }
        acting = m_acting.erase(acting);
    }
    for (const EntityId id : stunned) {
        if (Entity* entity = find(id)) {
            start_action(*entity, *entity->creature.body->stunAction);
        }
    }
    // The end of the additive reactions (stage U3): each observer gets the movement state with no record of the additive scope (the
    // client resets the record of a scope with no bit in the mask, client code 0x2A2ACD0). A dead creature has no additive reaction.
    for (auto playing = m_additives.begin(); playing != m_additives.end();) {
        Entity* entity = find(*playing);
        if (entity != nullptr && entity->creature.additive && !entity->creature.dead && entity->creature.additive->record.ends
            && now < *entity->creature.additive->record.ends) {
            ++playing;
            continue;
        }
        if (entity != nullptr && entity->creature.additive) {
            entity->creature.additive.reset();
            m_world.touch(entity->id, ComponentKey::movement);
        }
        playing = m_additives.erase(playing);
    }
}

std::size_t Creatures::reset_spawn_points() {
    // Only the points that wait: the list of all spawn points of the world is long.
    const std::vector<std::uint32_t> waiting(m_waiting.begin(), m_waiting.end());
    const std::size_t count = waiting.size();
    for (const std::uint32_t index : waiting) {
        SpawnPointState& point = m_points[index];
        set_waiting(index, false);
        point.respawnAt.reset();
        // The body of the creature that died is in the way of the new creature.
        const Entity* body = point.entity != 0 ? find(point.entity) : nullptr;
        if (body != nullptr && body->creature.dead) {
            ++m_counts.removedByCommand;
            m_world.remove_creature(point.entity);
        }
    }
    // The next tick makes the creatures of the points near a player.
    m_lastScan.reset();
    return count;
}

bool Creatures::respawn_at(std::uint32_t spawnPoint, WorldClock::time_point time) {
    load_points();
    if (spawnPoint >= m_points.size()) {
        return false;
    }
    if (m_points[spawnPoint].waiting) {
        m_points[spawnPoint].respawnAt = time;
    }
    return true;
}

void Creatures::entity_removed(const Entity& entity) {
    if (entity.kind != EntityKind::creature) {
        return;
    }
    m_creatures.erase(entity.id);
    m_acting.erase(entity.id);
    m_additives.erase(entity.id);
    m_behavior.removed(entity);
    m_forced.removed(entity.id);
    m_attackMotion.removed(entity.id);
    if (entity.creature.spawnPoint && m_points[*entity.creature.spawnPoint].entity == entity.id) {
        m_points[*entity.creature.spawnPoint].entity = 0;
    }
}

void Creatures::tick() {
    if (!has_data()) {
        return;
    }
    load_points();
    // The end of the effects of abilities first: a creature whose stun ended moves in this tick.
    end_effects();
    // The forced movements (stage 3.3b-S4) before the behavior: a creature whose movement ended takes its next step in
    // this tick.
    m_forced.tick();
    // The homing of the attacks (stage U3) before the contacts of the behavior: a contact tests the place of the homing.
    m_attackMotion.tick();
    // The behavior first: a contact of an attack is due at the latest at the end of its action.
    m_behavior.tick();
    end_actions();
    const WorldClock::time_point now = m_world.clock().now();
    if (m_lastScan && now - *m_lastScan < kScanInterval) {
        return;
    }
    m_lastScan = now;
    scan();
}

void Creatures::scan() {
    const LoadTimer timer(m_scanLoad);
    const WorldClock::time_point now = m_world.clock().now();
    const WorldOptions& options = m_world.options();
    // The cost of a scan depends on the players and on what is near them: the list of all
    // spawn points of the world is long, and the scan does not walk it.
    std::vector<state::WorldPosition> players;
    // Stage DG1: the spawn points are points of the main world: a character in an instance makes no creature of them.
    m_world.for_each_character([&players](const Entity& entity) {
        if (entity.space == kMainSpace) {
            players.push_back(entity.position);
        }
    });
    ++m_counts.scans;
    m_counts.lastScanPoints = 0;
    m_counts.lastScanCreatures = 0;
    // One walk of the spawn points near each player, with the larger of the two distances: a
    // point whose creature is in the world keeps it while a player is `creatureSpawnLeave` or
    // less from the point, and a point with no creature makes it when a player is
    // `creatureSpawnEnter` or less from it. The distance is the distance of the point, not of
    // the creature: a creature that goes to a player and away from its point stays in the world.
    m_near.clear();
    const float keep = std::max(options.creatureSpawnLeave, options.creatureSpawnEnter);
    const std::vector<gamedata::SpawnPoint>& points = m_data->spawn_points();
    std::vector<std::uint32_t> entering;
    for (const state::WorldPosition& player : players) {
        m_grid.for_each_candidate(player, keep, [&](std::uint32_t index) {
            ++m_counts.lastScanPoints;
            const float away = horizontal_distance(player, points[index].position);
            if (away > keep) {
                return;
            }
            if (m_points[index].entity != 0) {
                m_near.insert(m_points[index].entity);
            }
            if (away <= options.creatureSpawnEnter) {
                entering.push_back(index);
            }
        });
    }
    // The creatures of the world are the creatures of the points near a player, and the
    // creatures of commands. The entity table changes in the loop: walk a copy of the ids.
    // The phase of the world time (stage 2.2): a point with a time constraint makes its creature only in its phases (the spawner data,
    // format 4), and the creature of a point whose constraint asks for it leaves the world at the end of its phases.
    const std::uint8_t phase = phase_bit(m_world.world_time().phase());
    const std::vector<EntityId> creatures(m_creatures.begin(), m_creatures.end());
    for (const EntityId id : creatures) {
        const Entity* entity = find(id);
        ++m_counts.lastScanCreatures;
        if (entity == nullptr || !entity->creature.spawnPoint) {
            continue; // A creature of a command stays until a command removes it.
        }
        if (m_near.count(id) == 0) {
            ++m_counts.removedByDistance;
            m_world.remove_creature(id);
            continue;
        }
        const std::uint32_t index = *entity->creature.spawnPoint;
        const gamedata::SpawnPoint& point = points[index];
        if ((point.phases & phase) == 0 && point.leavesAtPhaseEnd) {
            // The end of the phases of the point (m_despawnOnTimeEnd of the data): the creature leaves the world. It is not a kill: no
            // reward, and the point keeps its state (the respawn time of a death, the hold of a command). The point makes its creature
            // again in its next phase; a point with m_cooldownAfterDespawn first waits its respawn time (a point that waits keeps
            // its own time).
            ++m_counts.removedByPhase;
            log::write("world: creature %016llx (%s) of spawn point %s leaves the world: the phase %s is not a phase of its point",
                       static_cast<unsigned long long>(id),
                       entity->creature.kind->id.c_str(),
                       point.id.c_str(),
                       day_phase_text(m_world.world_time().phase()));
            m_world.remove_creature(id);
            if (point.cooldownAfterPhaseEnd && !m_points[index].waiting) {
                start_cooldown(index, "the end of its phases");
            }
        }
    }
    m_near.clear();
    for (const std::uint32_t index : entering) {
        const gamedata::SpawnPoint& point = points[index];
        SpawnPointState& state = m_points[index];
        if ((point.phases & phase) == 0) {
            // Not a phase of the point: no creature, and no change of the point (a respawn time stays, and comes in a later phase). A
            // point with m_cooldownAfterConstraintFail that does not wait starts its respawn time.
            ++m_counts.outOfPhase;
            if (point.cooldownOutOfPhase && !state.waiting) {
                start_cooldown(index, "a look out of its phases");
            }
            continue;
        }
        if (state.waiting) {
            if (!state.respawnAt || now < *state.respawnAt) {
                continue;
            }
            // The respawn time came: the point is free again. The body of the creature that
            // died can be in the world still (a player stayed near it): it is in the way of
            // the new creature, and it leaves the world first.
            set_waiting(index, false);
            state.respawnAt.reset();
            const Entity* body = state.entity != 0 ? find(state.entity) : nullptr;
            if (body != nullptr && body->creature.dead) {
                ++m_counts.removedByRespawn;
                m_world.remove_creature(body->id);
            }
        }
        if (state.entity != 0) {
            continue;
        }
        const Entity* entity = make(m_data->kinds()[point.kind], point.level, point.position, point.yaw, index,
                                    index + state.spawns);
        if (entity == nullptr) {
            // The level table has no row for the level of the point: no creature, and no new try.
            set_waiting(index, true);
            log::write("world: spawn point %s makes no creature: the level table has no level %u",
                       point.id.c_str(),
                       point.level);
            continue;
        }
        state.entity = entity->id;
        ++state.spawns;
        ++m_counts.spawnedByPoint;
    }
    m_counts.pointsLooked += m_counts.lastScanPoints;
    m_counts.creaturesLooked += m_counts.lastScanCreatures;
}

void Creatures::start_cooldown(std::uint32_t point, const char* cause) {
    const std::chrono::milliseconds wait = respawn_time(m_data->spawn_points()[point], m_world.random_unit());
    set_waiting(point, true);
    m_points[point].respawnAt = m_world.clock().now() + wait;
    log::write("world: spawn point %s waits %.0f s (%s, a cooldown flag of its spawner)",
               m_data->spawn_points()[point].id.c_str(),
               std::chrono::duration<double>(wait).count(),
               cause);
}

void Creatures::first_record(EntityId creature) {
    Entity* entity = m_world.find(creature);
    if (entity != nullptr && entity->kind == EntityKind::creature && entity->creature.spawnAction) {
        entity->creature.recordedAt = m_world.clock().now();
    }
}

} // namespace soulwarden::backend::world

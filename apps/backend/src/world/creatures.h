#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gamedata/creature_data.h"
#include "world/behavior.h"
#include "world/entity.h"
#include "world/attack_motion.h"
#include "world/forced_motion.h"
#include "world/hit_reaction.h"
#include "world/ids.h"
#include "world/load_meter.h"
#include "world/position_index.h"
#include "world/world_clock.h"

// The creature system: the creatures of the world. A creature is an entity with no character
// and no session (world/entity.h). Only this system makes a creature, changes its health, and
// removes it. It changes the entity and marks the component state (World::touch): each session
// that observes the creature then sends the change (roadmap, rule MP-2).
//
// The spawn points (a rule of this project, protocol notes 4.14). The creature data has the
// places where the game has a creature. The world does not hold the creature of each place at
// all times: it makes the creature of a spawn point when a player is `spawnEnter` or less from
// the point, and it removes the creature when no player is `spawnLeave` or less from the
// point. A spawn point has one creature at most. The two distances are distances of the spawn
// point, not of the creature: a creature that goes to a player (world/behavior.h) stays in the
// world while a player is near its point.
//
// The cost of the rule depends on the players and on what is near them, not on the size of the
// world (the creature data of the whole world has 33,000 spawn points and more). One scan
// looks only at the spawn points in the grid cells that the circle of a player touches
// (SpawnGrid), and at the creatures that are in the world, which are the creatures of the
// points near a player. It never walks the list of all spawn points. The counts of a scan are in
// CreatureCounts, and a test case fails for a scan that looks at each point.
//
// A creature of a spawn point that died, or that a command removed, does not come back by the
// distance rule: the spawn point waits. A point whose creature died gets its respawn time from
// the data of its spawner (world/rewards.h, respawn_time): a time of the world clock
// (`respawnAt`) at which it is free again. A point whose creature a command removed waits for the
// command `reset` (reset_spawn_points), which makes each point that waits free at once. The body
// of the dead creature can be in the world at the respawn time (a player stayed near it): the
// point removes it before it makes its new creature. The respawn time costs nothing in a scan: a
// scan looks only at the points near a player.
//
// World thread only.

namespace soulwarden::backend::world {

class World;

enum class CreatureError {
    none,
    /** The backend has no creature data (the file of tools/gamedata/creatures.sh). */
    noData,
    unknownKind,
    /** The world has no creature with this id. */
    unknownCreature,
    /** The position is not a position of the world. */
    badPosition,
    /** The level table of the game has no row for the level. */
    badLevel,
    /** The health is not a number from 0 to the maximum health of the creature. */
    badHealth,
    /** The creature is dead: its health does not change. */
    dead,
    /** The action table of the slice of the creature has no action with this name that has a fragment. */
    unknownAction,
    /** The creature is on its way back (the reset): it takes no damage. */
    immune,
};

/** The locomotion of a change of the place of a creature (Creatures::move). */
enum class Gait : std::uint8_t {
    /** A place with no walk: the arrival at the end of the way back. */
    place,
    /** A step of a way in a fight or of the way back: the action Move_Combat of the slice (CreatureSlice::moveAction). */
    run,
    /**
     * A step of a walk with no target (a patrol, a wander): the action Move of the slice
     * (CreatureSlice::walkAction), or Move_Combat for a slice with no such action.
     */
    walk,
};

struct CreatureResult {
    CreatureError error{CreatureError::none};
    /** The creature of the command. Null when `error` is not `none`, and after a removal. */
    Entity* entity{nullptr};
};

/** The condition of one spawn point. */
struct SpawnPointState {
    /** The creature of the point that is in the world. 0: none. */
    EntityId entity{0};
    /** True: the creature of the point died or a command removed it. The point makes no creature. */
    bool waiting{false};
    /**
     * The time at which a point that waits is free again: the death of its creature plus the
     * respawn time of its spawner. No value: only a command makes it free (a removal by a command).
     */
    std::optional<WorldClock::time_point> respawnAt;
    /** Number of creatures that the point made. It selects the slice of an alias with more than one. */
    std::uint32_t spawns{0};
};

/**
 * The spawn points by their place: a grid of square cells in the horizontal plane, with the
 * indexes of the spawn points of each cell. The spawn points do not change after the load. A
 * lookup costs the cells that a circle touches, for each size of the world.
 */
class SpawnGrid {
public:
    /** Makes the grid for the spawn points of the creature data. The id of a point is its index. */
    void build(const std::vector<gamedata::SpawnPoint>& points);

    /**
     * Calls `visit(index)` for each spawn point in a cell that the circle touches: the columns
     * from west to east, the cells of a column from south to north, and the points of a cell
     * from the lowest index. A point of the result can be farther than `range`: the caller
     * compares the exact distance.
     */
    template <class Visit>
    void for_each_candidate(const state::WorldPosition& center, float range, Visit&& visit) const {
        const std::int64_t lowX = cell(center.x - range);
        const std::int64_t highX = cell(center.x + range);
        const std::int64_t lowY = cell(center.y - range);
        const std::int64_t highY = cell(center.y + range);
        if ((highX - lowX + 1) * (highY - lowY + 1) > static_cast<std::int64_t>(m_sorted.size())) {
            // A very large circle: each cell of the grid one time, in the same sequence.
            for (const auto& [key, points] : m_sorted) {
                if (key.first >= lowX && key.first <= highX && key.second >= lowY && key.second <= highY) {
                    for (const std::uint32_t point : *points) {
                        visit(point);
                    }
                }
            }
            return;
        }
        for (std::int64_t x = lowX; x <= highX; ++x) {
            for (std::int64_t y = lowY; y <= highY; ++y) {
                const auto found = m_cells.find(key_of(x, y));
                if (found != m_cells.end()) {
                    for (const std::uint32_t point : found->second) {
                        visit(point);
                    }
                }
            }
        }
    }

    /** @return The number of cells that have a spawn point. */
    [[nodiscard]] std::size_t cell_count() const {
        return m_cells.size();
    }

private:
    /** Edge of one cell, in meters. */
    static constexpr float kCellSize = 64.0F;

    [[nodiscard]] static std::int64_t cell(float value) {
        return static_cast<std::int64_t>(std::floor(value / kCellSize));
    }
    [[nodiscard]] static std::uint64_t key_of(std::int64_t x, std::int64_t y) {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) | static_cast<std::uint32_t>(y);
    }

    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> m_cells;
    /** The cells in the sequence of a lookup, for a circle that is larger than the grid. */
    std::vector<std::pair<std::pair<std::int64_t, std::int64_t>, const std::vector<std::uint32_t>*>> m_sorted;
};

/** Counts of the creature system, for the log and for the tests. */
struct CreatureCounts {
    /** Creatures that the spawn points made, and that a command made. */
    std::uint64_t spawnedByPoint{0};
    std::uint64_t spawnedByCommand{0};
    /** Stage QG5c2: the members of the encounters that the creature system made and removed. */
    std::uint64_t spawnedByEncounter{0};
    std::uint64_t removedByEncounter{0};
    /** Creatures that left the world: no player was near, or a command removed them. */
    std::uint64_t removedByDistance{0};
    std::uint64_t removedByCommand{0};
    /** Bodies of dead creatures that left the world at the respawn time of their spawn point. */
    std::uint64_t removedByRespawn{0};
    /** Stage QG9c: the deaths whose body each session makes again (CreatureSlice::clearsReaction, Creature::renewal). */
    std::uint64_t renewals{0};
    /**
     * Stage 2.2: the creatures that left the world at the end of the phases of their spawn point (no kill, no reward), and the points
     * near a player that made no creature in a phase that is not one of their phases.
     */
    std::uint64_t removedByPhase{0};
    std::uint64_t outOfPhase{0};
    std::uint64_t deaths{0};
    /**
     * User test 2: the reactions to a hit that started (with the fragment of a power of the row), the hits with no reaction (a damage over
     * time, NoReaction, an attack that plays, a hold), the hit stuns, and the idle records of the ends of an action, a walk, or a stun.
     */
    std::uint64_t reactions{0};
    std::uint64_t reactionsOfPower{0};
    std::uint64_t hitsWithNoReaction{0};
    std::uint64_t hitStuns{0};
    std::uint64_t reactionLimits{0};
    std::uint64_t idleRecords{0};
    /**
     * Stage U3: the turns of AlignToDirection, the additive reactions, the resets of the chain (the end of the limit, a hit in an attack),
     * and the homing of the attacks (world/attack_motion.h has its own counts).
     */
    std::uint64_t reactionTurns{0};
    std::uint64_t additiveReactions{0};
    std::uint64_t chainResets{0};
    /** The largest number of creatures in the world at one time. */
    std::size_t largest{0};
    /** Number of scans of the spawn point rule. */
    std::uint64_t scans{0};
    /**
     * Number of spawn points and of creatures that the scans looked at, and the same two
     * numbers for the last scan: the work of a scan, which depends on the players and on what
     * is near them.
     */
    std::uint64_t pointsLooked{0};
    std::uint64_t creaturesLooked{0};
    std::size_t lastScanPoints{0};
    std::size_t lastScanCreatures{0};
    /**
     * Stage 3.4f-D, the effects of the creatures: the entries of the due list that the world took (an end, a tick), the effects that a
     * look at the due creatures saw, the ticks of a heal, the heals (and the health that they gave), the effects that a dispel ended, the
     * statuses that came on a creature, and the rows that a creature did not take (no duration and no instant part).
     */
    std::uint64_t effectDueTaken{0};
    std::uint64_t effectsLooked{0};
    std::uint64_t effectTicks{0};
    /** Stage B1: the ticks of a damage over time with its icon (the burn of Flame Arrow). */
    std::uint64_t damageTicks{0};
    std::uint64_t heals{0};
    double healed{0.0};
    std::uint64_t dispelled{0};
    std::uint64_t statusesGiven{0};
    std::uint64_t statusesRefused{0};
    /**
     * Stage B2: the new stacks of an effect (a new application of a row with StackMax above its stacks), the applications that the
     * ApplicationCooldown of a row on the creature refused, the applications that a removal window refused (a running effect whose row
     * removes the row or its category: the removal of the charges of the Boltcaster), and the one-time damage of a row with no duration
     * (the bolt of the Boltcaster).
     */
    std::uint64_t stacksAdded{0};
    std::uint64_t applicationCooldowns{0};
    std::uint64_t statusesBlocked{0};
    std::uint64_t instantDamage{0};
};

/**
 * The damage of each tick of a status on a creature (stage B1: the burn of Flame Arrow, whose rule gives the damage base of a row,
 * ForceStatusEffectDamageTableRow): the amount of one tick, its damage type, and the rune charge of the source at a tick.
 */
struct StatusDamage {
    float amount{0.0F};
    std::string type;
    float charge{0.0F};
};

class Creatures {
public:
    /** @param data The creature data (game data). Null or not loaded: the world has no creature. */
    Creatures(World& world, const gamedata::CreatureData* data);

    [[nodiscard]] bool has_data() const {
        return m_data != nullptr && m_data->loaded();
    }
    [[nodiscard]] const gamedata::CreatureData* data() const {
        return m_data;
    }

    /**
     * Makes a creature of a kind at a position (a command). It has no spawn point: the distance
     * rule does not remove it.
     * @param kindId The id of a kind of the creature data.
     * @param level The level of the creature. No value: the level of the kind.
     * @param yaw Direction of the creature, in degrees.
     */
    CreatureResult spawn(const std::string& kindId,
                         const state::WorldPosition& position,
                         float yaw,
                         std::optional<std::uint32_t> level = std::nullopt);

    /**
     * Removes a creature from the world (a command). Its spawn point, when it has one, waits:
     * the distance rule does not make the creature again.
     */
    CreatureResult remove(EntityId id);

    /**
     * Stage QG5c2 (world/encounters.h): makes a member of an encounter: a creature of a kind of the creature data (a member kind: no spawn
     * point) at the level of the kind, bound to the placement and the generation of its instance (Creature::encounter). Only the encounter
     * system calls it. The distance rule does not remove it, and no spawn point waits for it; its death goes to the encounter system.
     * @return The entity, or null (a position that is not of the world, no level row).
     */
    Entity* spawn_member(const gamedata::CreatureKind& kind, const state::WorldPosition& position, float yaw, std::uint32_t placement,
                         std::uint64_t generation, const gamedata::PatrolRoute* route = nullptr, SpaceId space = kMainSpace);
    /** Stage QG5c2: removes a member (the end of its encounter, KillSpawnsAction): no reward, no spawn point. @return False: no member. */
    bool remove_member(EntityId id);

    /**
     * Sets the health of a creature. The health 0 is the death of the creature: it gets the
     * dead mark, and its spawn point waits. The entity stays in the world (the body). A creature
     * on its way back is immune: a lower health gives the error `immune` and changes nothing
     * (the same rule as a hit, for the health command and the kill command).
     */
    CreatureResult set_health(EntityId id, float health);

    /** Sets the health of a creature to 0. */
    CreatureResult kill(EntityId id) {
        return set_health(id, 0.0F);
    }

    /**
     * A creature takes damage (the combat system, world/combat.h): its health goes down by the
     * damage, through set_health. A creature that lives after it plays the reaction action of
     * its slice, when it plays no attack. A damage that is not less than its health is its
     * death. A creature that lives gets the character that did the damage as its target, when
     * it has none (world/behavior.h). A creature on its way back takes no damage (the error
     * `immune`, Creature::immune).
     * @param damage The damage: a finite number that is not negative.
     * @param attacker The entity of the character that did the damage. 0: a command.
     * @param threat The entity that the creature takes as its target in the place of the attacker (stage 3.3b-S2b: the Pylon
     *        whose shot did the damage). 0: the attacker.
     * @param reaction The reaction of the damage row of the hit (user test 2; stage U3: its source and its side).
     * @param push Gets the push of the impact of the hit (HitResult::push): the longest time (the reaction that started, the additive
     *        reaction, or the action Pushable; no value: no push) and the push scales of that action (stage U3).
     */
    CreatureResult hurt(EntityId id, float damage, EntityId attacker = 0, EntityId threat = 0, const HitReaction& reaction = {},
                        HitPush* push = nullptr);
    /**
     * @return The length of the reaction of a hit on a creature (user test 2, UT2-4 and UT2-7): the length of the reaction fragment of its
     *         power in the slice (CreatureSlice::reactionLengths), else kReactionTime. The push of the hit lasts this time at most.
     */
    [[nodiscard]] std::chrono::milliseconds reaction_length(const Entity& entity, const HitReaction& reaction) const;
    /**
     * @return True when the reaction rule of the action lists lets a hit play a reaction on a creature (user test 2, game data: the action
     *         Reaction of baseai_reaction.actionlist): the attack type of the row, its power level, the creature type of the kind, and the
     *         reaction limit (Creature::reactionLimitUntil). True for a hit with no rule (HitReaction::ruled false).
     */
    [[nodiscard]] bool reaction_allowed(const Entity& entity, const HitReaction& reaction, WorldClock::time_point now) const;

    /**
     * Gives a creature a status effect of an ability (stage 3.3a, world/abilities.h) until its
     * end on the world clock. A stun stops the movement and the attack of the creature (its
     * attack that plays stops, and it plays the action "Stun" of its slice), a root stops its
     * movement, a slow lowers its speed, and a weaken lowers its damage (world/behavior.h,
     * Combat::creature_damage). The same status again gives the effect the later end. Each
     * observer gets the icon of an effect that shows at the name plate (the status state). A
     * dead creature and a creature on its way back take no effect.
     * @return False: the creature took no effect.
     */
    bool apply_effect(Entity& entity, const CreatureEffect& effect);
    /**
     * A status row of the game data comes on a creature (stage 3.4f-D; the one path of a status of an ability, of a rule, of a spell, of a
     * status action of the creature, and of a command): the effect of the row (the stun, the root, the speed part, the DMG parts, the
     * heal of its ticks, its icon) until its end, through apply_effect. A row with RemoveStatusEffects or RemoveStatusEffectCategories
     * ends the effects of those rows and categories on the creature first (a dispel: the Null Chamber of the void gauntlet removes the
     * category Buff). A row with no duration and no time is one application now (its heal, its dispel), the rule of the statuses of a
     * character.
     * @param time The time of the effect. No value: the BaseDuration of the row.
     * @param damage Stage B1: the damage of each tick of the status (the source is the attacker). Null: none.
     * @return False: the creature took nothing (dead, on its way back, or a row with nothing for a creature).
     */
    bool give_status(Entity& entity, const gamedata::StatusRow& row, EntityId source, std::optional<std::chrono::milliseconds> time = std::nullopt,
                     const StatusDamage* damage = nullptr);
    /**
     * Stage B2: the ApplicationCooldown of a status row on a creature: one application of the row for each creature in that time (the key is
     * the creature, as for a character: research bow-build-2.md Q4, the bolt of the Boltcaster at most once in 3 s on each target).
     * @param start True: an application that changes the creature starts the time.
     * @return True when the row can come on the creature now.
     */
    bool application_ready(const Entity& entity, const gamedata::StatusRow& row, bool start);
    /**
     * @return True when a running effect of the creature refuses a status row (stage B2, the removal window: its row names the status in
     *         RemoveStatusEffects or a category of it in RemoveStatusEffectCategories). The one test of give_status and, from the code review
     *         of stage B2 (finding B2-R4), of the statuses of a perk (Gear::perk_status: a rend, a damage over time, a chain). It counts and logs
     *         a refusal.
     */
    bool removal_blocks(const Entity& entity, const gamedata::StatusRow& row);
    /**
     * Stage B2: @return The stacks of the effect of a status row on a creature now (0: none). The client keeps one entry for each status
     *         number of an entity (0x4038E80): the stacks of each source count (decision D10 of the orchestrator). `source` not 0: only when
     *         the effect has that source (CheckStatusEffectsOnTargetOwned).
     */
    [[nodiscard]] std::uint32_t stacks_of(const Entity& entity, std::string_view status, EntityId source = 0) const;
    /**
     * Stage B2: ends the effects of the rows and of the categories of the lists ("A,B" or "A+B") on a creature now, of each source (a dispel
     * of a status row, RemoveTargetStatusEffectsList of a passive rule). Each observer gets the status state. @return The number that ended.
     */
    unsigned remove_statuses(Entity& entity, std::string_view rows, std::string_view categories, std::string_view cause);
    /**
     * The heal of a creature (stage 3.4f-D, the one heal path of a creature: a tick of a heal effect): the health goes up by the amount, to
     * its maximum; each session that observes the creature gets the vitals state. A dead creature gets no heal.
     * @return The health that the creature got.
     */
    float heal(Entity& entity, float amount, std::string_view what);
    /** @return The number of the entries of the due list of the effects (stage 3.4f-D: the load). */
    [[nodiscard]] std::size_t effect_due_count() const {
        return m_effectDue.size();
    }
    /**
     * A knockdown or a stagger of an ability: the creature plays the reaction of the power (its
     * fragment of the slice; the fragment of the action "Reaction" for a slice with no such
     * fragment), and it does not move and does not attack for the time. Its attack that plays
     * stops. A dead creature and a creature on its way back take none.
     * @return False: the creature took none.
     */
    bool knock(Entity& entity, unsigned power, std::chrono::milliseconds time);
    /**
     * A hit with an impact (the damage row of the game data, stage 3.3b-S4) moves a creature: a pull or a push on the world
     * clock, through the navigation (world/forced_motion.h). A dead creature and a creature on its way back do not move.
     * @param source The source of the hit (the attacker of a melee hit, the center of an area or of a spell).
     * @param attacker The place of the attacker.
     * @return True when the creature moves.
     */
    bool impact(Entity& entity, const gamedata::DamageImpact& impact, const state::WorldPosition& source,
                const state::WorldPosition& attacker, std::string_view row, std::chrono::milliseconds length = kReactionTime,
                float scale = 1.0F, float pushBack = 1.0F, bool additive = false) {
        return m_forced.impact(entity, impact, source, attacker, row, length, scale, pushBack, additive);
    }
    /** @return The forced movement of the creatures (the pulls and the pushes that run). */
    [[nodiscard]] const ForcedMotion& forced() const {
        return m_forced;
    }
    /** @return The homing of the attacks of the creatures (stage U3). */
    [[nodiscard]] AttackMotion& attack_motion() {
        return m_attackMotion;
    }
    [[nodiscard]] const AttackMotion& attack_motion() const {
        return m_attackMotion;
    }
    /** @return The number of creatures with an additive reaction that plays (stage U3). */
    [[nodiscard]] std::size_t additive_count() const {
        return m_additives.size();
    }

    /**
     * Starts an action of the action table of the slice of a creature, by its name (a
     * development command, and the place where a behavior of a later stage starts an action).
     * Each session that observes the creature gets the action with its movement state.
     * @param seconds The time of an action that has an end: after it the creature has no action
     *        of the server. No value: the action stays until a different one starts.
     * @param tags Development aid: the tag state of the fragments for this action, in the
     *        place of the tags of the slice (a live check of the tags of a slice).
     */
    CreatureResult play(EntityId id, const std::string& action, std::optional<float> seconds = std::nullopt,
                        std::optional<gamedata::FragmentTags> tags = std::nullopt);

    /**
     * Makes each spawn point that waits free again (a command): the distance rule makes its
     * creature again when a player is near. A dead creature of a point leaves the world.
     * @return The number of spawn points that waited.
     */
    std::size_t reset_spawn_points();

    /**
     * Sets the time of the world clock at which a spawn point that waits is free again (the death
     * of a creature sets the respawn time of its spawner; a test or a command can set a different
     * one). No effect for a point that does not wait. From that time on, the point makes its
     * creature again when a player is near it (the distance rule): the body of the dead creature
     * then leaves the world first.
     * @return False when the creature data has no spawn point with this index.
     */
    bool respawn_at(std::uint32_t spawnPoint, WorldClock::time_point time);

    /**
     * The spawn point rule. The world calls it in each tick: it does its work one time for
     * each `kScanInterval` of the world clock.
     */
    void tick();

    /**
     * The next tick does the spawn point rule, also before its interval: a player came to a
     * different place of the world (World::travel).
     */
    void scan_soon() {
        m_lastScan.reset();
    }

    /** The world removed an entity: the spawn point of a creature has no creature now. */
    void entity_removed(const Entity& entity);

    /** @return The creature with this id, or null (also for the entity of a character). */
    [[nodiscard]] Entity* find(EntityId id);

    [[nodiscard]] const std::vector<SpawnPointState>& spawn_points() const {
        return m_points;
    }
    /** @return The number of spawn points that wait. */
    [[nodiscard]] std::size_t waiting_count() const {
        return m_waiting.size();
    }
    /** @return The behavior of the creatures: the targets, the attacks, and the counts of its steps. */
    [[nodiscard]] Behavior& behavior() {
        return m_behavior;
    }
    [[nodiscard]] const Behavior& behavior() const {
        return m_behavior;
    }

    /**
     * A session sends its first record of a creature whose slice needs the spawn action (code review of stage 3.4f-B, finding CS-R4): the
     * creature keeps the time (Creature::recordedAt). Only the time of the creature changes: no state.
     */
    void first_record(EntityId creature);

    // For the behavior of the system (world/behavior.h): it is the one caller of these functions.

    /**
     * Starts an action of the action table of the slice of a creature, and marks its movement
     * state. No effect for an index that the table does not have, or for an action with no fragment.
     * @param length The time of an action that has an end. No value: the action stays.
     * @return True when the action started.
     */
    bool start_action(Entity& entity, std::uint32_t action, std::optional<std::chrono::milliseconds> length = std::nullopt);
    /**
     * Puts a creature at a different place: the position index of the world follows it, and
     * each session that observes the creature gets its movement state.
     * @param gait For a step of a way (`run`, `walk`): the creature plays the locomotion action
     *             of its gait (Creature::moving) from its first step, in one record for the way
     *             (a new record only when the gait changes), and the step replaces an action of
     *             the server that plays (a reaction), also for a slice with no locomotion action.
     *             `place`: a place with no walk (the arrival at the end of the way back).
     */
    void move(Entity& entity, const state::WorldPosition& position, Gait gait = Gait::run);
    /**
     * The immune mark of a creature changed (Creature::immune: its way back started or ended):
     * each session that observes it gets the vitals state with the fields of the mark.
     */
    void immune_changed(Entity& entity);
    /** Ends the walk of a creature (Creature::moving): each observer gets its idle record (rest). */
    void stop(Entity& entity);
    /**
     * The creature has no action of the server and no locomotion now (user test 2, UT2-2): it gets the idle record of its slice with a new
     * start count (Creature::idle), and each observer gets its movement state. The end of an action of the server, of a walk, and of a stun
     * call it. No effect for a dead creature; a slice with no idle action gets the scope fields with no record (a mark of the state).
     */
    void rest(Entity& entity);
    /** Marks the movement state of a creature that changed its direction at its place. */
    void moved(Entity& entity);
    /** Gives a creature that lives its full health: a creature that is at its home again. */
    void restore(Entity& entity);

    /** @return The real time of the scans of the spawn point rule (a measurement). */
    [[nodiscard]] const LoadMeter& scan_load() const {
        return m_scanLoad;
    }
    [[nodiscard]] const CreatureCounts& counts() const {
        return m_counts;
    }
    /** @return The number of creatures that are in the world now. */
    [[nodiscard]] std::size_t count() const {
        return m_creatures.size();
    }
    /** @return The ids of the creatures that are in the world now. */
    [[nodiscard]] const std::set<EntityId>& ids() const {
        return m_creatures;
    }

    /** The interval of the spawn point rule on the world clock. */
    static constexpr std::chrono::milliseconds kScanInterval{500};
    /**
     * The time of the reaction of a creature to a hit when the creature data has no length of its reaction fragment (a rule of this
     * project, from before user test 2; the tool reads the lengths from user test 2: CreatureSlice::reactionLengths). After it the
     * creature has no action of the server: each observer gets its idle record.
     */
    static constexpr std::chrono::milliseconds kReactionTime{1500};

private:
    Entity* make(const gamedata::CreatureKind& kind,
                 std::uint32_t level,
                 const state::WorldPosition& position,
                 float yaw,
                 std::optional<std::uint32_t> spawnPoint,
                 std::uint32_t selector,
                 SpaceId space = kMainSpace);
    void scan();
    /** Makes the table of the spawn points for the creature data, when it does not have its size. */
    void load_points();
    /** Sets or clears the mark "the point waits" of a spawn point. */
    void set_waiting(std::uint32_t point, bool waiting);
    /** The point waits its respawn time (stage 2.2: a cooldown flag of a spawner with a time constraint), as after a death. */
    void start_cooldown(std::uint32_t point, const char* cause);
    /** Removes the action of each creature whose action has an end that came, and each additive reaction whose end came (stage U3). */
    void end_actions();
    /** The plan of a full reaction (stage U3): its fragment (no value: the fragment of the action), its length, its turn, its side. */
    struct ReactionPlan {
        std::optional<std::uint32_t> fragment;
        std::chrono::milliseconds length{kReactionTime};
        std::optional<float> turn;
        bool clip{false};
        const char* side{"front"};
    };
    /** @return The plan of the full reaction of a hit at an angle (the row of the reaction table of the slice, research Q2 and Q3). */
    [[nodiscard]] ReactionPlan plan_reaction(const Entity& entity, const HitReaction& reaction, float angle) const;
    /** The reaction of a creature that lives after a hit (hurt): the full reaction, the additive one, or Pushable, and the push. */
    void react(Entity& entity, const HitReaction& reaction, HitPush& push);
    /**
     * The due list of the effects (stage 3.4f-D; before: a look at each creature with an effect in each tick): the ends and the ticks whose
     * time came. The cost depends on the due entries, not on the creatures with an effect.
     */
    void end_effects();
    /** Removes the effects of one creature whose end came (or each effect of a dead creature), and the end of its knockdown. */
    void sweep(Entity& entity);
    /** One tick of the heal effect `serial` of a creature, due at `at`. */
    void effect_tick(Entity& entity, std::uint64_t serial, WorldClock::time_point at);
    /** Puts an entry into the due list of the effects: a look at the creature (`serial` 0), or a tick of an effect. */
    void due(WorldClock::time_point at, EntityId creature, std::uint64_t serial);
    /** The stun of a creature ended: its action "Stun" stops, and each observer gets its idle action. */
    void end_stun(Entity& entity);
    /**
     * The action of a creature ends or a different action replaces it now: when it is the reaction that holds the reaction limit, the 2 s of
     * the limit start now (code review of user test 2, finding U2-R3). Each place that ends or replaces an action calls it first.
     */
    void action_exits(Creature& creature, WorldClock::time_point now);

    World& m_world;
    const gamedata::CreatureData* m_data;
    /** One state for each spawn point of the creature data, with the same index. */
    std::vector<SpawnPointState> m_points;
    /** The spawn points by their place. */
    SpawnGrid m_grid;
    /** The spawn points that wait: the reset command walks only these. */
    std::set<std::uint32_t> m_waiting;
    /**
     * The creatures whose spawn point a player is near in the scan that runs: a list of the
     * scan, with no value between two scans.
     */
    std::unordered_set<EntityId> m_near;
    LoadMeter m_scanLoad;
    std::set<EntityId> m_creatures;
    /** The creatures whose action has an end. */
    std::set<EntityId> m_acting;
    /** The creatures with an additive reaction that plays (stage U3): end_actions looks only at these. */
    std::set<EntityId> m_additives;
    /**
     * The due list of the effects (stage 3.4f-D): at its time, a look at a creature (its ends: serial 0), or a tick of one effect (its
     * serial). An old entry (an effect that ended, a renewed end) changes nothing.
     */
    struct EffectDue {
        EntityId creature{0};
        std::uint64_t serial{0};
    };
    std::multimap<WorldClock::time_point, EffectDue> m_effectDue;
    std::uint64_t m_nextEffect{0};
    /**
     * The depth of the sweeps and the ticks of the effects that run now (code review of stage B2, finding B2-R2): a new application of an
     * effect whose end came sweeps the creature first, but not inside a sweep or a tick (a tick does not run two times).
     */
    unsigned m_effectDepth{0};
    /** Stage B2: the end of the ApplicationCooldown of each status row (lower case) on each creature. */
    std::map<std::pair<EntityId, std::string>, WorldClock::time_point> m_applications;
    CreatureCounts m_counts;
    std::optional<WorldClock::time_point> m_lastScan;
    /** Number of creatures that commands made. It selects the slice of an alias with more than one. */
    std::uint32_t m_commandSpawns{0};
    /** The behavior of the creatures: a part of this system. */
    Behavior m_behavior;
    /** The forced movement of the creatures (stage 3.3b-S4): a part of this system. */
    ForcedMotion m_forced;
    /** The homing of the attacks of the creatures (stage U3): a part of this system. */
    AttackMotion m_attackMotion;
};

} // namespace soulwarden::backend::world

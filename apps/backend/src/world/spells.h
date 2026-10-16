#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "gamedata/ability_data.h"
#include "gamedata/creature_data.h"
#include "gamedata/spell_data.h"
#include "world/combat.h"
#include "world/entity.h"
#include "world/ids.h"
#include "world/world_clock.h"

// The spell entities of the world (stage 3.3b-S2a, protocol notes 4.25; stage 3.3b-S2b: the Pylon, the trap, the Beacon). World
// thread only.
//
// A spell entity is an entity of the world with the kind `spell` (Entity::spell): its id from the id allocator, its caster, its
// position, its spell row, and its start and end on the world clock. Only this system makes, changes, and removes one. It has no
// character, no account, and no session.
//
//   Make.     The abilities system asks for a spell (Spells::make) with the role of its rules (SpellRole): an area of a cast
//             (the vortex of Gravity Well at the landing of its projectile), a summon (the Pylon of the ice gauntlet), a trap
//             (the trap of the musket at the landing of its projectile), or an aura (the Beacon of the life staff, attached to
//             the entity that its projectile hit).
//   Times.    The times come from the slice of the spell (game data, spell-data.json): the burst and the end of the vortex,
//             the life of the AutoSpell of a summon (15 s), the arm time of the detection volume of a trap (0.1 s) and its end
//             (the trigger of its Lifetime entity at 30 s and the destroy of its trigger timeline 0.508 s later); the life of an
//             aura is the Duration of its spell row (10 s). With no data: the times of the ability rule.
//   Area.     At the time of each phase the system gives it to the abilities system (Abilities::spell_phase).
//   Summon.   Its health is a part of the maximum health of the caster (the base vitals row of its slice:
//             HealthOverridePercentSpellOwnerMaxHealth), its armor the armor of the row. It shoots the spell of its AutoSpell
//             (m_initialCastDelay, then each m_cooldown) at the nearest creature of its detection volume that it sees, through
//             the projectile system (Projectiles::launch_spell): a creature that the shot hurts takes the summon as its target
//             (OverrideTargetForAiThreat of the spell row), walks to it, and attacks it (Spells::creature_hit). At the end of
//             its life (m_lifetimeExpireEvent) or at the health 0 (m_onDeathEvent) it triggers the entity of the event
//             (DestroyTrigger): each observer plays the death chain of the slice, and the entity goes away after the destroy of
//             that chain (1.3 s).
//   Trap.     From the arm time each kDetectStep the system looks for a creature whose capsule is in the detection volume (0.5
//             m). The first one triggers it one time: each creature in the sphere of its offense (1.1 m, the row Musket_Trap,
//             with no wall between) takes one hit and the effects of the plan (the root), the detection events of the slice run
//             (the trigger timeline), and the entity goes away at the destroy of that timeline.
//   Aura.     The aura follows the entity of its attachment (the Spell state names it: the client moves the picture). Each
//             kAuraStep the system looks for the friends of the caster in its radius (the position index): a friend in it has
//             the statuses of the spell row (the source `spell`); a friend that leaves it, and each friend at its end, loses
//             them (ClearOnExit, ClearOnEnd). The friends of a character: the character itself (a group of one; a test fixture
//             can add a friend: World::set_test_friends).
//   Record.   Each session near the spell gets its record (states/spell_state.cpp): the metadata state, the Spell state (or the
//             PositionInTheWorld state of a slice with no Spell component), the vitals state of a summon or a trap, and one
//             delayed event state for each delayed event that started (at the init, or at a trigger of its owner).
//   Ground area of a creature (stage 3.4f-C). The behavior of a creature asks for it at the time of a cast of its action
//             (Spells::make_area; the caster is the creature). Two shapes of the data (gamedata::AreaShape):
//               status area  the volume of the AreaStatusEffectComponent of its slice (a cylinder or a sphere; game data) is active from
//                            the activation of its entity to the end: a character in it gets the statuses of the component
//                            m_initialDelaySeconds after its entry and again each m_reapplicationDelaySeconds while it is in it, and loses
//                            them at its exit and at the end (m_removeOnExit; client code of the server facet 0x3FE5B40, 0x3FE6500,
//                            0x3FF2830). The status keeps the damage source of the creature (its core damage at the application).
//               strike       the volume with no shape (the sphere of the Radius of the spell row) is active between its activation and
//                            its deactivation (0.9 s to 0.95 s): each character in it takes one hit of the damage row of the spell with
//                            the creature rule (Health::creature_hit: the dodge decides it).
//             The life: the destroy event of the slice (26 s), else the Duration of the spell row (5 s).
//   End.      At its end, at a cancel (the leave of the caster; the death of the caster for a spell row with
//             EndOnCasterDeath; the removal of the entity of its attachment), and at a command, the world removes the entity:
//             each observer gets the stop.
//
// The cost: the tick looks only at the due list (one entry for each phase, end, shot, detection step, and aura step that is
// due), and each step looks only at the entities near its spell (the position index). No rule looks at each spell, creature,
// or character of the world (test cases SP7 and SB9 have a time limit).

namespace soulwarden::backend::world {

class World;

/** The constants of the spell entities that the game data does not give (rules of this project, each with its source). */
namespace spell_rules {
/**
 * A trap looks for a creature in its detection volume at this interval (a decision of an agent: a creature that runs 5 m/s moves
 * 0.5 m in it, the radius of the volume of the trap).
 */
inline constexpr std::chrono::milliseconds kDetectStep{100};
/** An aura looks for the friends in its area and follows its attachment at this interval (a decision of an agent). */
inline constexpr std::chrono::milliseconds kAuraStep{250};
/** A summon with no target looks again after this time (a decision of an agent). */
inline constexpr std::chrono::milliseconds kSummonLook{250};
/**
 * The shot of a summon starts this high above its feet (meters; a decision of an agent from the game data: the height of the
 * DetectableObjectComponent of the Pylon, 1.2 m).
 */
inline constexpr float kShotHeight = 1.2F;
/** A creature is in an area or a volume when it is not higher or lower than this from its center (meters), as an ability area. */
inline constexpr float kHeight = 3.0F;
/**
 * A status area of a creature looks for the characters in its volume at this interval (stage 3.4f-C, a decision of an agent: the server
 * facet of the game gets an event at each entry and exit of the physics; a character that runs 8 m/s moves 0.8 m in it). The applications
 * come at their own times (the initial and the reapplication delay of the data).
 */
inline constexpr std::chrono::milliseconds kAreaStep{100};
/** A strike looks for the characters in its sphere at this interval while its damage window is open (stage 3.4f-C, a decision of an agent). */
inline constexpr std::chrono::milliseconds kStrikeStep{25};
/**
 * A sample of a strike runs at its due time (code review of stage 3.4f-C, finding AR-R3: a world step from 890 ms to 960 ms crossed the window
 * 900 ms to 950 ms of the lightning, and the strike hit nobody while the client showed the bolt). A sample that the world processes after the
 * end of the window is made at its due time when it is at most this late (a bounded catch-up, a decision of an agent): each character is
 * at its place of that time (its place now less its velocity times the delay). A later sample is lost.
 */
inline constexpr std::chrono::milliseconds kStrikeCatchUp{250};
} // namespace spell_rules

/** The largest radius of a shield spell that the position index looks for near a segment (stage 3.3b-M2; the row of Maelstrom: 2 m). */
inline constexpr float kShieldReach = 5.0F;

/** One phase of a cast that a spell runs: its index in the plan of the cast, and its time from the start of the spell. */
struct SpellPhase {
    std::uint32_t phase{0};
    std::chrono::milliseconds at{0};
};

/** What the abilities system asks for. */
struct SpellRequest {
    /** The id text of the spell row ("Spell_GreatAxe_GravityWell_Vortex"). */
    std::string spellId;
    /** The entity of the caster (a character in the world). */
    EntityId caster{0};
    state::WorldPosition position;
    /** The cast of the abilities system and the phases that the spell runs. */
    std::uint64_t cast{0};
    std::vector<SpellPhase> phases;
    /** The end with no spell data (the time from the start): the last phase. */
    std::chrono::milliseconds fallbackEnd{0};
    /** Stage 3.3b-S2b: the role of the rules of the spell. */
    SpellRole role{SpellRole::area};
    /** An aura: the entity that it follows (0: none: the aura stays at its place). */
    EntityId attach{0};
    /** The life of the ability rule (an aura: the Duration of its spell row). No value: the life of the slice, or the fallback end. */
    std::optional<std::chrono::milliseconds> life;
    /** An aura: the radius of its area (the Radius of the spell row), and the status rows that the friends in it get (StatusEffects). */
    float radius{0.0F};
    std::vector<std::string> statuses;
    /** True: `statuses` are the statuses of an AreaStatusEffectComponent of the slice (stage 3.3b-M2: the fortify in the well). */
    bool areaStatuses{false};
    /** A trap: the status rows that a creature that the trap hits gets (the StatusEffect of the damage row of its offense). */
    std::vector<std::string> effects;
    /** The weapon of the cast: the shots of a summon and the hit of a trap have its damage and its gem. */
    HitWeapon weapon;
    CastConversion conversion;
    /**
     * An aura (stage 3.3b-S6): the source of the heals of its statuses (the caster and its heal base at the cast). The statuses that
     * the aura gives keep it: their ticks heal with it.
     */
    HealSource heal;
};

/** What the behavior of a creature asks for at a cast of a ground area (stage 3.4f-C). */
struct AreaRequest {
    /** The entity of the creature (the caster). */
    EntityId creature{0};
    /** The cast of the data (the spell, its shape, the damage of a strike). */
    const gamedata::AreaCast* cast{nullptr};
    /** The place of the area (on the floor). */
    state::WorldPosition position;
};

/** Why a spell ended. */
enum class SpellEnd : std::uint8_t {
    /** Its end on the world clock (the destroy event of its slice, the life of a summon or of an aura). */
    time,
    /** The caster left the world (its entity went away). */
    casterLeft,
    /** The caster died, and the spell row has EndOnCasterDeath. */
    casterDied,
    /** A development command. */
    command,
    /** Its health went to 0 (a summon, a trap; stage 3.3b-S2b). */
    destroyed,
    /** A trap triggered (stage 3.3b-S2b). */
    triggered,
    /** The entity of its attachment went away (an aura; stage 3.3b-S2b). */
    targetLeft,
    /** The entity of its attachment died (an aura; code review of stage 3.3b-S2b, finding F3). */
    targetDied,
};
inline constexpr std::size_t kSpellEndCount = 8;

[[nodiscard]] const char* spell_end_text(SpellEnd cause);
[[nodiscard]] const char* spell_role_text(SpellRole role);

/** Counts of the spell system, for the log, `tools/world list`, and the tests. */
struct SpellCounts {
    std::uint64_t made{0};
    std::array<std::uint64_t, kSpellEndCount> ended{};
    /** The phases that ran, and the phases that a cancel ended before their time. */
    std::uint64_t phases{0};
    std::uint64_t phasesCancelled{0};
    /** The entries of the due list that the tick took (each one a phase, an end, a step, or an entry of a spell that went away). */
    std::uint64_t dueTaken{0};
    /** The largest number of spell entities in the world at one time. */
    std::size_t largest{0};
    /** Stage 3.3b-S2b: the shots of the summons, the hits that a summon took, the triggers of the traps and their hits. */
    std::uint64_t shots{0};
    std::uint64_t summonHits{0};
    std::uint64_t triggers{0};
    std::uint64_t trapHits{0};
    /** The statuses that an aura gave, and the statuses that it took back (ClearOnExit, ClearOnEnd). */
    std::uint64_t auraGiven{0};
    std::uint64_t auraTaken{0};
    /** The rows that an aura gave again to a friend in it after their end (code review of stage 3.3b-S2b, finding F5). */
    std::uint64_t auraRenewed{0};
    /** The entities that the steps of the summons, the traps, and the auras looked at (the position index: the load). */
    std::uint64_t looked{0};
    /**
     * Stage 3.4f-C, the ground areas of the creatures: the areas made, the steps of the status areas and of the strikes, the status
     * applications of the areas (the first one and the next ones), the exits that took a status, and the hits of the strikes.
     */
    std::uint64_t areasMade{0};
    std::uint64_t areaSteps{0};
    std::uint64_t areaApplications{0};
    std::uint64_t areaExits{0};
    std::uint64_t strikeHits{0};
    /**
     * Stage 3.4f-D (code review of stage 3.4f-C): the exits that the tick of a status of an area found first (finding AR-R2), and the
     * samples of a strike that a late world step made at their due time after the end of the window (finding AR-R3).
     */
    std::uint64_t exitsBeforeTick{0};
    std::uint64_t strikeCatchUps{0};
};

class Spells {
public:
    Spells(World& world, const gamedata::SpellData* data);

    /** @return True when the backend has the spell data (the slices): a spell then has a record. */
    [[nodiscard]] bool has_data() const {
        return m_data != nullptr && m_data->loaded();
    }

    /**
     * Makes a spell entity for a caster at a place, with the rules of its role. @return The entity, or null: the caster is not in
     * the world, the place is not a position of the world, or (a summon, a trap, an aura) the backend has no slice data of the
     * spell (those roles need the values of the slice).
     */
    Entity* make(const SpellRequest& request);

    /**
     * Makes a ground area of a creature (stage 3.4f-C): the spell of the cast at the place, with the creature as its caster. @return The
     * entity, or null: the creature is not a creature that lives, the place is not a position of the world, or the backend has no data
     * of the shape of the spell (area_usable).
     */
    Entity* make_area(const AreaRequest& request);
    /**
     * @return True when the backend has what a cast of a ground area needs: the slice of its spell (the spell data) with the shape of the
     *         cast, and the spell row (the ability data). A creature uses only such casts (world/behavior.h).
     */
    [[nodiscard]] bool area_usable(const gamedata::AreaCast& cast) const;
    /** @return The CastDistance of the spell row of a cast (meters), or 0 with no row. */
    [[nodiscard]] float cast_distance(const gamedata::AreaCast& cast) const;
    /**
     * A status of a status area of a creature has a tick now (code review of stage 3.4f-C, finding AR-R2: the world runs the statuses
     * before the spells, and a character that left the cloud took one more tick). The area looks at this one character: a character
     * that is not in its active volume leaves it now (its statuses go with m_removeOnExit). Only that area: no scan.
     * @return False: the character left the area.
     */
    bool holds(EntityId area, const Entity& character);

    /**
     * @return The time of the activation of the target with the damage row `row` on the timeline of a spell (the burst of
     *         Gravity Well: 3 s), from the slice of the spell (game data). No value: no data, or no such event.
     */
    [[nodiscard]] std::optional<std::chrono::milliseconds> activation_time(std::string_view spellId, std::string_view row) const;

    /** The world calls it in each tick: the phases, the ends, and the steps that are due. */
    void tick();

    /**
     * @return The part of the segment from `from` to `to` (0 to 1) at which it enters the sphere of a shield of an enemy of the shooter
     *         (stage 3.3b-M2: the projectile shield of Maelstrom with Absorb, the Radius of its spell row), or no value. The shields near
     *         the segment only (the position index).
     */
    [[nodiscard]] std::optional<float> absorbs(const state::WorldPosition& from, const state::WorldPosition& to, EntityId shooter) const;

    /** The caster left the world: each of its spells ends (SpellEnd::casterLeft). */
    void caster_left(EntityId caster);
    /** The caster died: each of its spells whose row has EndOnCasterDeath ends (a character, and from stage 3.4f-C a creature). */
    void caster_died(EntityId caster);
    /** An entity left the world (a character, a creature): each aura that follows it ends (SpellEnd::targetLeft). */
    void target_left(EntityId target);
    /** An entity died (a character, a creature): each aura that follows it ends (SpellEnd::targetDied; finding F3). */
    void target_died(EntityId target);
    /**
     * A character leaves the world (World::remove_entity, before its entity goes away): each aura that gives it its statuses
     * takes them back (ClearOnExit; code review of stage 3.3b-S2b, finding F6: the statuses stay with the character id).
     */
    void member_left(const Entity& character);
    /** Ends a spell (a command). @return False: no spell with this id. */
    bool end(EntityId spell, SpellEnd cause);
    /** Ends each spell of a caster. @return The number of the spells that ended. */
    std::size_t end_caster(EntityId caster, SpellEnd cause);
    /** The world removed the entity of a spell (World::remove_entity): the system forgets it. */
    void entity_removed(const Entity& spell);

    /**
     * A creature hits a summon (Behavior::contact): the damage of the creature rule (Combat::creature_damage, with the armor of
     * the vitals row of the summon) lowers its health; the health 0 starts its death chain. A summon in its end chain takes no
     * damage. @return The damage that the summon took.
     */
    float creature_hit(Entity& creature, const gamedata::CreatureContact& contact, Entity& spell);
    /** Development aid: lowers the health of a summon or a trap (the health 0 starts its death chain). @return False: no such spell. */
    bool hurt(EntityId spell, float amount);

    /** @return The number of spell entities in the world. */
    [[nodiscard]] std::size_t count() const {
        return m_live.size();
    }
    /** @return The number of the entries of the due list (the tests: the list does not grow with the world). */
    [[nodiscard]] std::size_t due_size() const {
        return m_due.size();
    }
    [[nodiscard]] const SpellCounts& counts() const {
        return m_counts;
    }
    /** @return The characters that have the statuses of an aura now (the tests, the development list). */
    [[nodiscard]] std::vector<EntityId> members(EntityId spell) const;

private:
    enum class DueKind : std::uint8_t {
        /** A phase of the cast (its index in Live::phases). */
        phase,
        /** The end of the life (a time end, or the trigger of the end chain of a summon or a trap at the end of its life). */
        end,
        /** The removal at the destroy of an end chain. */
        remove,
        /** A shot of a summon. */
        shot,
        /** A detection step of a trap. */
        detect,
        /** A step of an aura. */
        aura,
        /** Stage 3.4f-C: a step of a status area of a creature. */
        areaStep,
        /** Stage 3.4f-C: a step of the damage window of a strike. */
        strikeStep,
    };
    /** One entry of the due list. */
    struct Due {
        EntityId spell{0};
        DueKind kind{DueKind::phase};
        std::int32_t phase{0};
    };
    struct Live {
        std::uint64_t cast{0};
        std::vector<SpellPhase> phases;
        /** The phases that ran (one time each). */
        std::vector<bool> done;
        SpellRole role{SpellRole::area};
        /** An aura: its radius, its statuses, and the characters that have them now (the entity and its character id). */
        float radius{0.0F};
        std::vector<std::string> statuses;
        std::map<EntityId, std::string> members;
        /** A trap: the status rows of its hit. */
        std::vector<std::string> effects;
        /** The weapon of the cast. */
        HitWeapon weapon;
        CastConversion conversion;
        /** An aura: the source of the heals of its statuses (stage 3.3b-S6). */
        HealSource heal;
        /** A summon and a trap: the end of their life on the world clock (the start of the end chain). */
        WorldClock::time_point lifeEnds{};
        /** The statuses leave a friend that leaves the area, and each friend at the end (stage 3.3b-M2: the cells of the spell row). */
        bool clearOnExit{true};
        bool clearOnEnd{true};
        /**
         * Stage 3.4f-C, a ground area of a creature: its cast, the component and the volume of a status area (or the volume of a strike),
         * the radius of the sphere of a strike (the Radius of its spell row), the damage source of its statuses (the creature and its core
         * damage at the cast), the time from which the volume is active and the time at which it is not, the characters in a status area
         * (the time of the next application of each one), and the characters that a strike hit.
         */
        const gamedata::AreaCast* areaCast{nullptr};
        const gamedata::SpellAreaStatus* areaStatus{nullptr};
        const gamedata::SpellVolume* volume{nullptr};
        float strikeRadius{0.0F};
        DamageSource damage;
        WorldClock::time_point activeFrom{};
        std::optional<WorldClock::time_point> activeTo;
        struct AreaMember {
            std::string characterId;
            WorldClock::time_point next{};
            bool applied{false};
        };
        std::map<EntityId, AreaMember> areaMembers;
        std::set<EntityId> struck;
    };

    void schedule(EntityId spell, WorldClock::time_point at, DueKind kind, std::int32_t phase = 0);
    /** Triggers an entity of the slice of a spell: its events that start at a trigger run from now, and each observer gets them. */
    void trigger(Entity& spell, std::string_view entity);
    /**
     * Starts the end chain of a summon or a trap: triggers the entity (the death event, the expire event, the trigger timeline),
     * and removes the spell at the destroy of that chain.
     */
    void start_end(Entity& spell, std::string_view entity, SpellEnd cause);
    void summon_shot(Entity& spell, Live& live);
    void trap_step(Entity& spell, Live& live);
    void aura_step(Entity& spell, Live& live);
    /** Stage 3.4f-C: one step of a status area of a creature (the entries, the applications, the exits), and one step of a strike. */
    void area_step(Entity& spell, Live& live);
    /** `due`: the due time of the sample of the strike (finding AR-R3). */
    void strike_step(Entity& spell, Live& live, WorldClock::time_point due);
    /** One sample of a strike at a time of its window (`now`: the time of the world tick): one hit for each character in its sphere. */
    void strike_sample(Entity& spell, Live& live, WorldClock::time_point sample, WorldClock::time_point now);
    /**
     * @return True when the capsule of a character is in the volume of a ground area of a creature (stage 3.4f-C). `back`: the character
     *         this far back on its velocity (a sample at an earlier time: finding AR-R3).
     */
    [[nodiscard]] bool in_area(const Entity& spell, const Live& live, const Entity& character, std::array<float, 3> back = {}) const;
    /** A character is no member of a status area of a creature any more: its statuses of the area go (m_removeOnExit). */
    void leave_area(const Entity& spell, Live& live, EntityId member, bool takeStatus);
    /** The friends of the caster of an aura lose its statuses (ClearOnExit, ClearOnEnd). */
    void clear_members(const Entity& spell, Live& live, const std::set<EntityId>& keep);
    /** A friend comes into an aura: the status rows of the aura that it does not have from a different aura. @return True: it has them. */
    bool give_member(const Entity& spell, const Live& live, Entity& friendly);
    /** A friend leaves an aura (or the aura ends): each row that no other aura gives ends, and a row that one gives gets its source. */
    /**
     * A friend is no member of the aura any more. `takeStatus` false (code review of stage 3.3b-M2, finding M2-R3: the end of an aura
     * with no ClearOnEnd, the fortify of Clear Out): only the rows of the aura go away; the status keeps its time, and a later aura
     * can give the row again.
     */
    void remove_member(const Entity& spell, const Live& live, EntityId member, const std::string& characterId, bool takeStatus = true);
    /** The friends in an aura get each row of the aura again that ended while they are in it (finding F5: a row of 10 s in an aura of 15 s). */
    void renew_members(const Entity& spell, const Live& live);
    /** One status row of a character from the auras (code review of stage 3.3b-S2b, finding F4). */
    struct AuraRow {
        /** The auras that give the row to the character now. */
        std::set<EntityId> auras;
        /** The aura whose source the status of the row has. */
        EntityId owner{0};
    };

    World& m_world;
    const gamedata::SpellData* m_data;
    std::multimap<WorldClock::time_point, Due> m_due;
    std::unordered_map<EntityId, Live> m_live;
    /** The spells of each caster (the cancel at its leave and at its death). */
    std::unordered_map<EntityId, std::vector<EntityId>> m_byCaster;
    /** The auras of each entity that they follow (the cancel at its leave). */
    std::unordered_map<EntityId, std::vector<EntityId>> m_byTarget;
    /** The status rows that the auras give: character id to (the lower-case id of the row to its auras). */
    std::unordered_map<std::string, std::map<std::string, AuraRow>> m_auraRows;
    /** The end causes of the spells in their end chain (the log line of the removal). */
    std::unordered_map<EntityId, SpellEnd> m_endCauses;
    /** The status areas of the creatures that each character is in (stage 3.4f-C): character id to the spells (the leave of the world). */
    std::unordered_map<std::string, std::set<EntityId>> m_areasOf;
    SpellCounts m_counts;
};

} // namespace soulwarden::backend::world

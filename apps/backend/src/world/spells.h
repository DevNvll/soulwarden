#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "gamedata/ability_data.h"
#include "gamedata/spell_data.h"
#include "world/entity.h"
#include "world/ids.h"
#include "world/world_clock.h"

// The spell entities of the world (stage 3.3b-S2a, protocol notes 4.25): the vortex of Gravity Well. World thread only.
//
// A spell entity is an entity of the world with the kind `spell` (Entity::spell): its id from the id allocator, its
// caster, its position, its spell row, and its start and end on the world clock. Only this system makes, changes, and
// removes one. It has no character, no account, and no session.
//
//   Make.     The abilities system asks for a spell at the place of a cast (Spells::make: the landing of the projectile
//             of Gravity Well), with the phases of the cast that the spell runs (the vortex at its start, the burst).
//   Times.    The times come from the slice of the spell (game data, spell-data.json): the burst at the activation of
//             the entity with the damage row of the burst (3 s for the vortex), the end at the trigger of the entity
//             with a DestroyGDEComponent (3.5 s). With no data: the times of the ability rule.
//   Phases.   At the time of each phase the system gives it to the abilities system (Abilities::spell_phase), which does
//             the hits of the phase at the place of the spell. Each phase runs one time, while the spell is in the world:
//             a cancel ends the phases that did not come.
//   Record.   Each session that is near the spell (the m_sliceSpawnRadius of its slice) has it in its interest table and
//             gets its record (states/spell_state.cpp: the metadata state, the Spell state, and one delayed event state
//             for each delayed event of its slice, with the time that is left at the time of the record). A session that
//             comes later gets the age of the spell; a session that leaves gets the stop; nothing in the session runs a
//             time or a hit of the spell.
//   End.      At its end, at a cancel (the leave of the caster; the death of the caster for a spell row with
//             EndOnCasterDeath), and at a command, the world removes the entity: each observer gets the stop.
//
// The cost: the tick looks only at the due list (one entry for each phase and each end that is due), not at each spell,
// creature, or character of the world (test case SP7 has a time limit).

namespace soulwarden::backend::world {

class World;

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
};

/** Why a spell ended. */
enum class SpellEnd : std::uint8_t {
    /** Its end on the world clock (the destroy event of its slice). */
    time,
    /** The caster left the world (its entity went away). */
    casterLeft,
    /** The caster died, and the spell row has EndOnCasterDeath. */
    casterDied,
    /** A development command. */
    command,
};
inline constexpr std::size_t kSpellEndCount = 4;

[[nodiscard]] const char* spell_end_text(SpellEnd cause);

/** Counts of the spell system, for the log, `tools/world list`, and the tests. */
struct SpellCounts {
    std::uint64_t made{0};
    std::array<std::uint64_t, kSpellEndCount> ended{};
    /** The phases that ran, and the phases that a cancel ended before their time. */
    std::uint64_t phases{0};
    std::uint64_t phasesCancelled{0};
    /** The entries of the due list that the tick took (each one a phase, an end, or an entry of a spell that went away). */
    std::uint64_t dueTaken{0};
    /** The largest number of spell entities in the world at one time. */
    std::size_t largest{0};
};

class Spells {
public:
    Spells(World& world, const gamedata::SpellData* data);

    /** @return True when the backend has the spell data (the slices): a spell then has a record. */
    [[nodiscard]] bool has_data() const {
        return m_data != nullptr && m_data->loaded();
    }

    /**
     * Makes a spell entity for a caster at a place, with the phases of its cast (Abilities::spell_phase gets each one at its
     * time). @return The entity, or null: the caster is not in the world, or the place is not a position of the world.
     */
    Entity* make(const SpellRequest& request);

    /**
     * @return The time of the activation of the target with the damage row `row` on the timeline of a spell (the burst of
     *         Gravity Well: 3 s), from the slice of the spell (game data). No value: no data, or no such event.
     */
    [[nodiscard]] std::optional<std::chrono::milliseconds> activation_time(std::string_view spellId, std::string_view row) const;

    /** The world calls it in each tick: the phases and the ends that are due. */
    void tick();

    /** The caster left the world: each of its spells ends (SpellEnd::casterLeft). */
    void caster_left(EntityId caster);
    /** The caster died: each of its spells whose row has EndOnCasterDeath ends. */
    void caster_died(EntityId caster);
    /** Ends a spell (a command). @return False: no spell with this id. */
    bool end(EntityId spell, SpellEnd cause);
    /** Ends each spell of a caster. @return The number of the spells that ended. */
    std::size_t end_caster(EntityId caster, SpellEnd cause);
    /** The world removed the entity of a spell (World::remove_entity): the system forgets it. */
    void entity_removed(const Entity& spell);

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

private:
    /** One entry of the due list: a phase of a spell (its index in Live::phases), or its end (kEnd). */
    struct Due {
        EntityId spell{0};
        std::int32_t phase{0};
    };
    static constexpr std::int32_t kEnd = -1;
    struct Live {
        std::uint64_t cast{0};
        std::vector<SpellPhase> phases;
        /** The phases that ran (one time each). */
        std::vector<bool> done;
    };

    World& m_world;
    const gamedata::SpellData* m_data;
    std::multimap<WorldClock::time_point, Due> m_due;
    std::unordered_map<EntityId, Live> m_live;
    /** The spells of each caster (the cancel at its leave and at its death). */
    std::unordered_map<EntityId, std::vector<EntityId>> m_byCaster;
    SpellCounts m_counts;
};

} // namespace soulwarden::backend::world

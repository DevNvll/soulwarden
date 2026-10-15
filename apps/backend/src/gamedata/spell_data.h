#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// The spell entity data (stage 3.3b-S2a, protocol notes 4.25; format 2: stage 3.3b-S2b, the Pylon, the trap, the Beacon): for each
// spell row of the game with a prefab slice, what the
// backend needs to make the spell as an entity of the world and to send its record: the slice (the asset id and the sub
// id), the slot of each replicated component (the Spell state, type 2912; each DelayedEvent, type 3139), the interest
// radius of the slice, and its delayed events (the slot, the delay, the event type, the target entity, the damage row of a
// MomentaryOffenseComponent of the target, and whether the target destroys the entity group).
//
// The data has the values of the game files and no rule: the rules of a spell entity are in the world (world/spells.h).
//
// The data is game data: the tool tools/gamedata/spells.sh makes the file from the game copy of the user, and the file
// stays outside the repository. Without the file the backend starts and works: a spell has its hits on the times of the
// ability rule, and no client sees an entity of it.

namespace soulwarden::backend::gamedata {

/** File name of the spell data in the game data directory of the backend. */
inline constexpr const char* kSpellDataFile = "spell-data.json";

/**
 * The event types of a delayed event (m_type of m_eventData in the slice). The meaning comes from the targets of the vortex
 * of Gravity Well (research ability-use.md) and from the live check L4 of stage 3.3b-S2a: a completed state of an
 * activation plays the burst at once, a completed state of a deactivation hides the vortex.
 */
namespace spell_event {
inline constexpr std::uint32_t kTrigger = 0;
inline constexpr std::uint32_t kActivate = 2;
inline constexpr std::uint32_t kDeactivate = 3;
} // namespace spell_event

/** One delayed event of a spell slice (a DelayedEventComponent with its m_eventData). */
struct SpellEvent {
    /** The slot of its component (its state, type 3139, has this slot in the record of the spell). */
    std::uint32_t slot{0};
    /** m_timeDelaySeconds: the time from the start of the spell (m_startDelayOnInit). */
    float delay{0.0F};
    /** m_type of m_eventData (spell_event). */
    std::uint32_t type{0};
    /** The name of the target entity ("Burst", "Destroy"). */
    std::string target;
    /** The damage row of a MomentaryOffenseComponent of the target ("GreatAxe_GravityWell_Burst"). Empty: none. */
    std::string offense;
    /** True: the target has a DestroyGDEComponent (the end of the spell). */
    bool destroys{false};
    /**
     * Format 2 (stage 3.3b-S2b): the entity of the DelayedEventComponent, m_startDelayOnInit, and m_startDelayOnTrigger. An
     * event with no start at the init runs only after its owner is triggered (the death chain of the Pylon: the events of
     * the entity DestroyTrigger). A file of format 1 has the init start for each event.
     */
    std::string owner;
    bool onInit{true};
    bool onTrigger{false};
};

/** The Vitals component of a spell slice (format 2): its slot, its row of the base vitals tables, and the target of its death event. */
struct SpellVitals {
    std::uint32_t slot{0};
    /** m_rowReference ("IceMagicPylon"). */
    std::string row;
    /** The entity that m_onDeathEvent triggers ("DestroyTrigger"). Empty: none. */
    std::string deathTarget;
};

/** The AutoSpellComponent of a spell slice (format 2): the shots of a summon (the Pylon). */
struct SpellAutoCast {
    /** m_spellName: the spell of each shot ("IceMagic_Pylon_Projectile"). */
    std::string spell;
    /** m_lifetime and m_lifetimeOnHit (seconds). */
    float lifetime{0.0F};
    float lifetimeOnHit{0.0F};
    /** m_initialCastDelay and m_cooldown of m_defaultRateOfFire (seconds). */
    float initialDelay{0.0F};
    float cooldown{0.0F};
    /** The entities that m_lifetimeExpireEvent and m_castEvent trigger ("DestroyTrigger", "FireTrigger"). */
    std::string expireTarget;
    std::string castTarget;
    /** m_excludeTargetNWTags. */
    std::vector<std::string> exclude;
};

/** One DetectionVolumeComponent of a spell slice (format 2). */
struct SpellVolume {
    std::string entity;
    /** The radius of its sphere (meters; 0: a different shape). */
    float radius{0.0F};
    /** The time from the start at which the volume is active (seconds; the activation of an entity that is not active at the start). -1: never. */
    float activeAfter{0.0F};
};

/** One MomentaryOffenseComponent of a spell slice (format 2): its entity, its damage row, and the radius of its sphere. */
struct SpellOffense {
    std::string entity;
    std::string row;
    float radius{0.0F};
};

/** One event of a DetectionVolumeEventComponent (format 2): the entity that a detection triggers, and the event type. */
struct SpellDetectionEvent {
    std::string target;
    std::uint32_t type{0};
};

/** One TimelineEntry of a spell slice (format 2): its owner entity, and the time of the destroy in its timeline (-1: none). */
struct SpellTimeline {
    std::string owner;
    float destroyAfter{-1.0F};
};

/** A row of the base vitals tables that a spell slice names (format 2). */
struct SpellVitalsRow {
    std::string id;
    float healthBaseMax{0.0F};
    /** HealthOverridePercentSpellOwnerMaxHealth: the health is this part of the maximum health of the caster (0: HealthBaseMax). */
    float ownerHealthPart{0.0F};
    float physicalArmor{0.0F};
    float elementalArmor{0.0F};
    float gearScore{0.0F};
};

/** One replicated component of a spell slice. */
struct SpellSlot {
    std::uint32_t slot{0};
    std::string name;
    /** The state type of the component (0: not known). */
    std::uint32_t type{0};
};

/** One faceted component of a spell slice (the facet target of a message: the slice-root rule of protocol notes 4.2). */
struct SpellComponent {
    std::uint64_t entity{0};
    std::uint64_t component{0};
    std::string name;
};

/** One spell slice of the game. */
struct SpellSlice {
    std::string path;
    /** The asset id of the slice (the 16 bytes of the asset catalog) and its sub id. */
    std::array<std::uint8_t, 16> asset{};
    std::uint32_t subId{2};
    /** The root entity of the slice. */
    std::uint64_t root{0};
    /** m_sliceSpawnRadius of the slice (meters): a session observes the spell at this distance. */
    float interestRadius{0.0F};
    /** The slot of the Spell state (type 2912). 0: none (format 2: a slice with a PositionInTheWorld component, the trap). */
    std::uint32_t spellSlot{0};
    /** The slot of the PositionInTheWorld state (type 13; format 2). 0: none. */
    std::uint32_t positionSlot{0};
    std::vector<SpellSlot> slots;
    /** The delayed events, in the sequence of their slots. */
    std::vector<SpellEvent> events;
    std::vector<SpellComponent> components;
    /** Format 2 (stage 3.3b-S2b): the values of the components that a spell entity of the backend uses. */
    std::optional<SpellVitals> vitals;
    std::optional<SpellAutoCast> autoCast;
    std::vector<SpellVolume> volumes;
    std::vector<SpellOffense> offenses;
    std::vector<SpellDetectionEvent> detectionEvents;
    std::vector<SpellTimeline> timelines;

    /**
     * @return The delay of the event that starts at the init and whose target destroys the entity group (the end of the
     *         spell). No value: none.
     */
    [[nodiscard]] std::optional<float> end_delay() const;
    /** @return The delay of the activation of a target with the damage row `row` (the burst). No value: none. */
    [[nodiscard]] std::optional<float> activation_delay(std::string_view row) const;
    /**
     * @return The time from a trigger of the entity `owner` to the destroy of the entity group (format 2): the delay of an event of
     *         the owner that starts at a trigger and whose target destroys (the Pylon: DestroyTrigger, 1.3 s), else the destroy
     *         time of the timeline of the owner (the trap: Trigger, 0.508 s). No value: none.
     */
    [[nodiscard]] std::optional<float> destroy_after_trigger(std::string_view owner) const;
    /** @return The detection volume of an entity, or the largest one (an empty name). Null: none. */
    [[nodiscard]] const SpellVolume* volume(std::string_view entity = {}) const;
    /** @return The first offense of the slice, or null. */
    [[nodiscard]] const SpellOffense* offense() const {
        return offenses.empty() ? nullptr : &offenses.front();
    }
};

/** The data as plain values: the content of the file, and what a test sets. */
struct SpellTables {
    std::vector<SpellSlice> slices;
    /** Each spell row with a slice: its id and the index of its slice. */
    std::vector<std::pair<std::string, std::size_t>> spells;
    /** The rows of the base vitals tables that the slices name (format 2). */
    std::vector<SpellVitalsRow> vitals;
    /**
     * The vitals rows that a slice names and that the base vitals tables do not have (a row of a creature table; the member
     * `vitalsNotInData` of the tool). A slice whose vitals row is in neither list is refused (code review of stage 3.3b-S2b,
     * finding F8), and the spell of a slice whose row is in this list makes no summon and no trap (Spells::make).
     */
    std::vector<std::string> vitalsNotInData;
};

class SpellData {
public:
    /**
     * Reads the file of the tool. A file that is not complete is not used, as a whole: a member of a wrong form, a slice
     * with the null asset id or with no Spell slot of the type 2912, two components of a slice with one slot, a delayed
     * event whose slot is not a DelayedEvent slot (type 3139) of its slice, a delay that is not a number (the game has
     * delays of 0.0001 s and of -6 s: world/spells.h takes them as the start), more delayed events than one record holds, a spell with no slice, two spells
     * with one id, and a member "counts" with a number that is not the size of its list.
     * @return False when the file is not there or is not complete. `error()` then has the cause.
     */
    [[nodiscard]] bool load(const std::string& path);
    /** Sets the data with no file (tests), with the same check. @return False, with no data, when it is not correct. */
    [[nodiscard]] bool set(SpellTables tables);

    [[nodiscard]] bool loaded() const {
        return !m_tables.slices.empty();
    }
    [[nodiscard]] const std::string& error() const {
        return m_error;
    }
    [[nodiscard]] const SpellTables& tables() const {
        return m_tables;
    }
    /** @return The slice of a spell row (the compare ignores the case of the letters A to Z), or null. */
    [[nodiscard]] const SpellSlice* slice_of(std::string_view spellId) const;
    /** @return A row of the base vitals tables (format 2; the compare ignores the case of A to Z), or null. */
    [[nodiscard]] const SpellVitalsRow* vitals_row(std::string_view id) const;

private:
    SpellTables m_tables;
    std::unordered_map<std::string, std::size_t> m_spells;
    std::unordered_map<std::string, std::size_t> m_vitals;
    std::string m_error;
};

} // namespace soulwarden::backend::gamedata

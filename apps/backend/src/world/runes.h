#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include "world/entity.h"
#include "world/ids.h"
#include "world/world_clock.h"

// The heart rune system (stage 1.3b, itemization): the charge of the heart rune of a character
// and its activation. World thread only.
//
// The charge. A character with a heart rune in paperdoll slot 45 gets charge from its hits and
// from the hits that it takes: AttackRuneCharge of the damage row of an accepted hit on a
// creature (Combat::strike), HitRuneCharge of the contact of a creature that hurt the character
// (Gear::on_hit_taken), and SourceRuneChargeOnApply and SourceRuneChargeOnTick of a status of a
// perk with a change of the health (Gear::perk_damage). One gain is `kGainMost` at most (the
// client setting heartgem-max-charge-per-gain, default 6.0 at image RVA 0x7EFA340), and the charge
// is 0 to `kChargeMax` (the ChargeComponent of the player slice: m_initMax 60). The charge state
// of the owner (slot 48, type 5257: one byte, the percent) fills the icon of the HUD (live check
// I6). The charge is a value of the character (the character id is the key), not of a session.
//
// The activation. The key G sends the input bit 450 (rune_ability; live check I6), and the
// client empties its icon itself. The system takes it when the rune has the ability
// Ability_Rune_Detonate (the EquipAbility of its weapon row) and the charge is full: the charge
// goes to 0 (ChargeToConsume 100 of the action Rune_Detonate), and the character gets one status
// of the action for its time (3 s): the first variant whose ability row a perk of the character
// has, in the sequence of the conditions of the action (Dominator, Greater, Major), or the base
// status Rune_Detonate (a rule of this project: one status, thus one explosion). The status
// counts in the gear system (Dominator: -50 percent of the armor). At the end of the status
// (OnEndStatusEffect Rune_Detonate_CastAoE, CastSpell Rune_Detonate_AoE) the explosion hurts each
// creature in the shape of the spell (Radius 5, Height 5) around the character that the
// character sees (CheckObstructions), with the damage row Rune_Detonate_AoE and the weapon of the
// rune (SlotToFillWeaponDamageInfo HEART_GEM_SLOT), through Combat::strike: the perks of the hit
// (Dominator: +20 percent) count there. A character that died or left the world before the end
// makes no explosion. A second press while a status runs, or with a charge that is not full,
// changes nothing.
//
// The cost: an activation looks at the position index around one character (32 creatures at most).

namespace soulwarden::backend::world {

class World;

namespace rune_rules {
/** The largest charge (ChargeComponent of the player slice, m_initMax). The state has the percent of it. */
inline constexpr float kChargeMax = 60.0F;
/** The largest charge of one gain (the client setting heartgem-max-charge-per-gain, default 6.0). */
inline constexpr float kGainMost = 6.0F;
/** The ability of a heart rune that this backend has (the build of stage 1.3b). */
inline constexpr const char* kDetonateAbility = "Ability_Rune_Detonate";
/** The damage row and the spell of the explosion, and the base status of the action. */
inline constexpr const char* kDetonateRow = "Rune_Detonate_AoE";
inline constexpr const char* kDetonateSpell = "Rune_Detonate_AoE";
inline constexpr const char* kDetonateStatus = "Rune_Detonate";
/** The variants of the action Rune_Detonate, in the sequence of its conditions: the ability row of the perk, and the status. */
inline constexpr std::array<std::array<const char*, 2>, 3> kDetonateVariants = {{
    {"Heartgem_Rune_Detonate_Dominator", "Rune_Detonate_Dominator"},
    {"Heartgem_Rune_Detonate_Greater", "Rune_Detonate_Greater"},
    {"Heartgem_Rune_Detonate_Major", "Rune_Detonate_Major"},
}};
/** Largest number of creatures of one explosion (a limit of the cost, as ability_rules::kAreaMost). */
inline constexpr std::size_t kExplosionMost = 32;
} // namespace rune_rules

enum class RuneVerdict : std::uint8_t {
    /** The activation started: the explosion comes at the end of the status. */
    activated,
    /** The session is the owner of no entity. */
    noEntity,
    /** The backend has no perk data, no ability data, or no combat tables. */
    noData,
    /** The character is dead. */
    dead,
    /** The character has no heart rune, or a rune whose ability has no rule of this backend. */
    noRune,
    /** The charge is not full. */
    notFull,
    /** A status of an activation runs. */
    active,
};
inline constexpr std::size_t kRuneVerdictCount = 7;

/** @return A short text for a log line, for example "the charge is not full". */
[[nodiscard]] const char* rune_verdict_text(RuneVerdict verdict);

/** Counts of the rune system, for the log and for the tests. */
struct RuneCounts {
    std::array<std::uint64_t, kRuneVerdictCount> activations{};
    /** Charge gains, explosions, the creatures that the explosions hurt, and the explosions that did not come (death, exit). */
    std::uint64_t gains{0};
    std::uint64_t explosions{0};
    std::uint64_t explosionHits{0};
    std::uint64_t cancelled{0};
};

class Runes {
public:
    explicit Runes(World& world) : m_world(world) {}

    /** A character gets charge (points, before the limit of one gain). No effect with no heart rune or for a dead character. */
    void gain(Entity& character, float amount);
    /** A session gives the key of the heart rune. */
    RuneVerdict activate(SessionId session);
    /** The activation for a character in the world (also a command). */
    RuneVerdict activate_character(const std::string& characterId);
    /** A command sets the charge of a character (0 to 100 percent). @return False: the character is not in the world. */
    bool set_charge(const std::string& characterId, float percent);
    /** The world made the entity of a character: it gets the charge of the character. */
    void entered(Entity& character);
    /** @return The charge of a character in percent (0 to 100). */
    [[nodiscard]] float percent_of(const std::string& characterId) const;

    [[nodiscard]] const RuneCounts& counts() const {
        return m_counts;
    }

private:
    struct CharacterState {
        float charge{0.0F};
        /** The number of the activation whose status runs, and its end. No value: none. */
        std::optional<std::uint64_t> activation;
        WorldClock::time_point ends{};
    };

    RuneVerdict counted(RuneVerdict verdict) {
        ++m_counts.activations[static_cast<std::size_t>(verdict)];
        return verdict;
    }
    /** @return The rune item of a character with the Detonate ability, or null. */
    [[nodiscard]] const ItemInstance* detonate_rune(const Entity& character) const;
    /** Puts the charge into the entity and marks the charge state. */
    void publish(Entity& character, const CharacterState& state);
    /** The end of the status of an activation: the explosion. */
    void explode(const std::string& characterId, std::uint64_t activation);

    World& m_world;
    std::unordered_map<std::string, CharacterState> m_characters;
    std::uint64_t m_nextActivation{1};
    RuneCounts m_counts;
};

} // namespace soulwarden::backend::world

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include "gamedata/combat_tables.h"

// The reaction of a creature to a hit (user test 2, UT2-9): the cells of the damage row of the hit (gamedata::DamageReaction). The
// combat system makes it from the row, and the creature system plays it (Creatures::hurt). World thread only.

namespace soulwarden::backend::world {

/**
 * The reaction of a hit on a creature. The default value is the reaction of stage 3.2a (the fragment of the action Reaction, no hit stun):
 * a command, and a row of a file of a tool from before user test 2.
 */
struct HitReaction {
    /** The attack type of the damage row (the column AttackType): the gates of the reaction rule of the action lists. */
    enum class Attack : std::uint8_t { light, heavy, other };
    /** False: the hit plays no reaction (a tick of a damage over time, which has no damage row; NoReaction of the row). */
    bool plays{true};
    /**
     * True for a row with NoReaction: the action Pushable of the action lists (baseai_pushable.actionlist, game data) pushes the creature
     * for 0.4 s with no reaction (kPushableTime).
     */
    bool pushable{false};
    /** True: the reaction rule of the action lists applies (a row of the data). False: the reaction of stage 3.2a (a command). */
    bool ruled{false};
    Attack attack{Attack::other};
    /**
     * The power level of the hit on a creature (PowerLevel_AI, else PowerLevel): the fragment r_R{power}_F of the slice. No value: the
     * fragment of the action Reaction.
     */
    std::optional<std::uint32_t> power;
    /** The hit stun on a creature (HitStun_AI, else HitStun), in frames of 1/30 s. 0: none. */
    std::uint32_t stunFrames{0};
};

/** The push of the action Pushable for a damage with no reaction (baseai_pushable.actionlist: ActiveTime 0.4, UsePushNoDecay). */
inline constexpr std::chrono::milliseconds kPushableTime{400};

/** @return The reaction of a hit of a damage row on a creature (the default reaction for a row with no reaction cells). */
[[nodiscard]] HitReaction hit_reaction(const gamedata::DamageRow& row);

} // namespace soulwarden::backend::world

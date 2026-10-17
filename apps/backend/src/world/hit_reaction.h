#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include "gamedata/combat_tables.h"
#include "state/store.h"

// The reaction of a creature to a hit (user test 2, UT2-9): the cells of the damage row of the hit (gamedata::DamageReaction). The
// combat system makes it from the row, and the creature system plays it (Creatures::hurt). World thread only.

namespace soulwarden::backend::world {

/**
 * The reaction of a hit on a creature. The default value is the reaction of stage 3.2a (the fragment of the action Reaction, no hit stun):
 * a command, and a row of a file of a tool from before user test 2.
 */
struct HitReaction {
    /**
     * The attack type of the damage row (the column AttackType): the gates of the reaction rule of the action lists. `ability`: the type
     * Ability; `other`: each other type (Magic, MagicSpell, Heartgem, ...; code review of user test 2, finding U2-R2: the branch below power
     * 1 of baseai_reaction.actionlist names only Ability).
     */
    enum class Attack : std::uint8_t { light, heavy, ability, other };
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
    /**
     * The hit stun on a creature (HitStun_AI, else HitStun), in frames of 1/30 s (client code 0x2559640: the clip HitStun adds it to times
     * x 30; game data: AdjustedHitStun 20 of the level table). 0: none.
     */
    std::uint32_t stunFrames{0};
    /**
     * The source of the hit (stage U3, research Q2): the attacker of a melee hit, the point of a projectile, the center of an area; the
     * attacker for a row with UseAttackerPosForReaction (the point of the push direction). Its angle from the front of the creature selects
     * the side of the reaction. No value: the front (a command, a damage over time).
     */
    std::optional<state::WorldPosition> source;
    /** True for a hit of a projectile (the additive reaction does not give the limiter again for a ranged hit; game data). */
    bool ranged{false};
};

/**
 * The push of a hit on a creature (user test 2, stage U3): the longest time of the push of the impact of the row (the reaction that the hit
 * started, the additive reaction, or the action Pushable), and the push scales of that action (the entry of ReactionMotion: Scale and
 * PushBackScale; client code 0x48A0690). No length: the hit moves nothing.
 */
struct HitPush {
    std::optional<std::chrono::milliseconds> length;
    float scale{1.0F};
    float pushBack{1.0F};
    /** True for the push of an additive reaction: it does not hold the behavior (ForcedMove::additive). */
    bool additive{false};
};

/** The push of the action Pushable for a damage with no reaction (baseai_pushable.actionlist: ActiveTime 0.4, UsePushNoDecay). */
inline constexpr std::chrono::milliseconds kPushableTime{400};

/** @return The reaction of a hit of a damage row on a creature (the default reaction for a row with no reaction cells). */
[[nodiscard]] HitReaction hit_reaction(const gamedata::DamageRow& row);

} // namespace soulwarden::backend::world

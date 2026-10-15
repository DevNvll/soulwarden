#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "gamedata/creature_data.h"
#include "world/entity.h"
#include "world/ids.h"

// The health system: the health and the life of the characters. A character takes damage, dies
// at the health 0, and comes back with its full health at a respawn place (protocol notes
// 4.18). The health of a character is in its entity (Entity::vitals), and its life state too
// (Entity::life): only this system changes the two (the progress system sets the maximum of
// the health, world/progress.h). A change marks the vitals state: the owner and each observer
// of the character get it (roadmap, rule MP-2).
//
// The client of a character finds no hit of a creature itself and sends no report for it: the
// world tests each contact of an attack of a creature (world/behavior.h), and this system
// applies the damage that the combat system computes (Combat::creature_damage).
//
// The rules of the death (rules of this project):
//   - The health 0 is the death. The session of the character tells its client: the dead bit
//     of the vitals state, and the death notice with the time to the respawn screen.
//   - A dead character takes no damage, does no damage, and is the target of no creature.
//   - The client asks for the respawn. The character then lives again with its full health at
//     its respawn place: the travel place of the game data that is nearest to the place of its
//     death (the spirit shrines and the inns), or the place of its death when the backend has
//     no travel places. The change of the place is a travel (World::travel).
//   - A dead character that leaves the world is not dead at its next world entry: the world
//     writes its respawn place into its row when it leaves, and each world entry starts with
//     the full health. The health and the death of a character are not in the database.
//
// The health that comes back (a rule of this project; the vitals table of the game has no
// value for it: its column HealthBaseTickRate is 0 for a player):
//   - A character that lives, and that took no damage and did no damage for `kRestTime`, gets
//     `kRestPart` of its maximum health each `kRestInterval` of the world clock, to its
//     maximum. Its observers get the vitals state of each change.
//
// World thread only.

namespace soulwarden::backend::world {

class World;

/** What the session of a character gets for one damage that the character took: the mark on the screen of its client. */
struct DamageTaken {
    /** The entity that did the damage, and its place. 0: a command. */
    EntityId source{0};
    state::WorldPosition from;
    /** Number of the damage type on the wire, and the damage. */
    std::uint8_t type{0};
    float amount{0.0F};
    /** The damage as a part of the maximum health of the character, 0 to 1. */
    float part{0.0F};
};

enum class HealthError {
    none,
    /** The character has no entity: it is not in the world. */
    notInWorld,
    /** The damage is not a finite number that is 0 or more. */
    badValue,
    /** The character is dead: it takes no damage. */
    dead,
    /** The character lives: a respawn is for a dead character. */
    alive,
};

struct HealthResult {
    HealthError error{HealthError::none};
    /** The entity of the character. Null when `error` is `notInWorld`. */
    Entity* entity{nullptr};
    float before{0.0F};
    float after{0.0F};
    /** True when this change was the death of the character, and when it made a dead character live again. */
    bool died{false};
    bool revived{false};
    /** A respawn: true when the travel to the respawn place was done (the session then gets SessionSink::traveled). */
    bool travelled{false};
};

/**
 * One heal of a character (stage 3.3b-S6). The caller gives the amount before the received modifiers of the target: for a heal of an
 * ability the heal base of the healer times the coefficient of the data, the potency, and the outgoing modifiers of the healer
 * (Health::outgoing); for a potion its fixed amount and its part of the maximum health; for a life steal its part of the damage.
 */
struct HealRequest {
    float amount{0.0F};
    HealKind kind{HealKind::ability};
    /** True: the owner of the target sees the number of the gain (bit 1 of healthChangeFlags of its vitals state; live check L3). */
    bool number{true};
    /** The entity of the healer. A healer that is a different character gets the notice of the gain (message 4298). 0: none. */
    EntityId healer{0};
    /** The source for the log line ("Status_Ability_Lifestaff_Beacon"). */
    const char* what{""};
};

/** What one heal did. */
struct HealOutcome {
    /** The health that the target got: the amount times the received factor, to its maximum. 0: no heal. */
    float gained{0.0F};
    /** The received factor of the target (1 + its HealMod and the part of the kind, 0 or more). */
    float received{1.0F};
    float before{0.0F};
    float after{0.0F};
    /** True: the healer got the notice (a different character). */
    bool notice{false};
};

/** Counts of the heals (stage 3.3b-S6), for the log, tools/world, and the tests. */
struct HealCounts {
    std::uint64_t heals{0};
    /** Heals with a gain that showed the number to the owner of the target. */
    std::uint64_t numbers{0};
    /** Notices of a healer (message 4298). */
    std::uint64_t notices{0};
    /** Heals that changed nothing: a dead target, a target that is not a player, an amount that is not above 0, a full target. */
    std::uint64_t nothing{0};
};

/** The place at which a character lives again. */
struct RespawnPlace {
    state::WorldPosition position;
    /** Name of the travel place. Empty: the place of the death. */
    std::string name;
};

class Health {
public:
    explicit Health(World& world) : m_world(world) {}

    /**
     * One contact of an attack of a creature that passed its test: the target takes the damage
     * of the rule (Combat::creature_damage), and the session of the target gets the mark for
     * its screen. The health 0 is the death of the target.
     */
    void creature_hit(Entity& creature, const gamedata::CreatureContact& contact, Entity& target);

    /**
     * A session entered the world with a character that is dead (a second session took the
     * character over): its client gets the death notice, with the time to the respawn screen
     * that stays. No effect for a character that lives.
     */
    void entered(const Entity& character);

    /** A character takes damage that a command gives (a development aid, and the tests). */
    HealthResult hurt(const std::string& characterId, float damage);

    /**
     * A command gives a character its full health. A dead character lives again at its place:
     * the death screen of its client closes.
     */
    HealthResult heal(const std::string& characterId);

    /**
     * The client of a session asks for the respawn of its character. The world finds the
     * character from the session (roadmap, rule MP-5). A dead character lives again with its
     * full health at its respawn place. A character that lives keeps its state: the result
     * has the error `alive`.
     */
    HealthResult respawn(SessionId session);

    /** @return The place at which a character that is dead now lives again. */
    [[nodiscard]] RespawnPlace respawn_place(const Entity& character) const;

    /** A character did damage to a creature: it is in a fight (its health does not come back for `kRestTime`). */
    void fought(Entity& character);

    /**
     * The one heal of a character (stage 3.3b-S6; before: Health::heal_amount of stage 1.3b): a character that lives gets the amount
     * times its received factor (Health::received), to its maximum. The vitals state of the owner and of each observer changes; the
     * owner sees the number of the gain when the request has it (bit 1 of healthChangeFlags, owner view only); a healer that is a
     * different character gets the notice of the gain (message 4298, only its session).
     */
    HealOutcome heal(Entity& target, const HealRequest& request);

    /**
     * @return The heal base of a healer with a weapon (stage 3.3b-S6, a decision of the user of 2026-10-15): the base damage of the
     *         weapon at its gear score (the value of the damage rule, Combat::weapon_base) times the scaling of Focus with the curve and
     *         the table of a damage attribute (1 + the ScalingFocus of the weapon times the damage value of the Focus of the healer:
     *         the column ModifierValueSum of the Focus table). 0: no weapon row, or no combat tables.
     */
    [[nodiscard]] float heal_base(const Entity& healer, const std::string& itemId, std::uint32_t gearScore) const;

    /**
     * @return The outgoing factor of a heal of an ability of a healer (stage 3.3b-S6): 1 plus one additive pool of the outgoing parts
     *         (a decision of an agent, as the damage parts and the critical pool): the HealScalingValueMultiplier of its statuses (live
     *         check L9: "outgoing healing is reduced by 3% per stack"), of its rules with OnHealed whose conditions pass for the target
     *         (Passives::heal_bonus), and the heal factor of its equip load less 1 (live check L9: "Outgoing Healing +30%"). 0 or more.
     */
    [[nodiscard]] float outgoing(const Entity& healer, const Entity& target, const HealSource& source) const;

    /**
     * @return The received factor of a heal of a kind (stage 3.3b-S6): 1 plus the HealMod of the statuses of the target (each heal;
     *         live check L9: "incoming healing is reduced by 6%"), plus ConsumableHealMod (a consumable), NonConsumableHealMod (an
     *         ability, the gear, a life steal), and DmgPctToHealthHealMod (a life steal) (game texts: "reduces healing taken", Anointed
     *         "healing from non-consumables and lifesteal"). One additive pool (a decision of an agent), 0 or more.
     */
    [[nodiscard]] float received(const Entity& target, HealKind kind) const;

    [[nodiscard]] const HealCounts& heal_counts() const {
        return m_healCounts;
    }

    /** The world calls it in each tick: the health of the characters that are not in a fight comes back. */
    void tick();

    /** @return Number of health changes that the rest rule made. For the tests. */
    [[nodiscard]] std::uint64_t rests() const {
        return m_rests;
    }

    /** The time from the death of a character to the respawn screen of its client, in seconds (a rule of this project). */
    static constexpr float kRespawnTime = 5.0F;
    /** The health comes back after this time with no damage, in steps of this interval, by this part of the maximum (rules of this project). */
    static constexpr std::chrono::milliseconds kRestTime{6000};
    static constexpr std::chrono::milliseconds kRestInterval{1000};
    static constexpr float kRestPart = 0.03F;

private:
    /** Lowers the health of a character that lives, and makes it dead at the health 0. */
    HealthResult apply(Entity& target, float damage);

    World& m_world;
    std::optional<WorldClock::time_point> m_lastRest;
    std::uint64_t m_rests{0};
    HealCounts m_healCounts;
};

} // namespace soulwarden::backend::world

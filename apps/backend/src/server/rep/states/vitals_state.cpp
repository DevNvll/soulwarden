#include <algorithm>
#include <cmath>

#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// MB::VitalsComponentReplicatedState (constructor at image RVA 0x671E040), slot 41 of the
// player slice. The fields of health and stamina are in group 0, the fields of mana are in
// group 1 (the group numbers are in three variables of the client at image RVA 0xA563290).
// The client makes a stat when it gets one of its amount, maximum, or tick rate fields
// (client facet at image RVA 0x67A7560). Without the stats the client has no health value and
// logs "Could not find stat" on each frame.
//
// View     Fields
// owner    group 0: HealthAmount, StaminaAmount, HealthMax, HealthTickRate, StaminaMax,
//          StaminaTickRate, vitalsId, maxHealth. Group 1: ManaAmount, ManaMax, ManaTickRate.
// observer group 0 only. The remote player slice has the vitals and the stamina component
//          and no mana component.
// Each state after the first one of a session, and the first state for a dead character, also
// has vitalsData (bit 0 of its flags: the character is dead) and healthChangeFlags (bit 0), in
// the two views (protocol notes 4.18). The health of a character changes with a damage, and
// the client of the owner shows the new value on its HUD from HealthAmount alone (live proof).
// Stage 3.3b-S6: the owner view of the state that carries a heal has bit 1 of healthChangeFlags
// too (live check L3, protocol notes 4.31): the client shows its green heal number, the new
// health less its own health. The observer view never has bit 1.
//
// A creature (encode_creature_vitals_state) has only the observer view, and only fields of
// group 0. The first state of a session has HealthAmount, vitalsData, healthChangeFlags,
// HealthMax, HealthTickRate, vitalsId, vitalsCategoryId, vitalsLevel, invulnerability,
// displayImmuneWhenInvulnerable, and maxHealth. A later state (a change of the health, the
// death) has HealthAmount, vitalsData, and healthChangeFlags (bit 0, in each state: the name
// plate gets a health change only with it), and the two immune fields when the immune mark
// changed after the last state of the session. The client makes the name, the
// level, and the health bar of the name plate from these fields: vitalsCategoryId gives the
// name, vitalsLevel the level, and bit 0 of vitalsData is "dead" (protocol notes 4.14).
//
// The immune mark (stage 3.4e, protocol notes 4.17): a creature on its way back (the reset) has
// invulnerability 1 and displayImmuneWhenInvulnerable 1 (one byte each; the facet keeps them at
// +0x284 and +0x285, client apply function at image RVA 0x683E670). Live check: the client
// makes no melee contact with a creature that has invulnerability 1 (no hit mark, no claim, no
// damage number); displayImmuneWhenInvulnerable alone changes nothing that the check saw.

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 2;
constexpr unsigned kHealthAmount = 0;
constexpr unsigned kStaminaAmount = 1;
constexpr unsigned kVitalsData = 2;
constexpr unsigned kHealthChangeFlags = 3;
constexpr unsigned kHealthMax = 4;
constexpr unsigned kHealthTickRate = 5;
constexpr unsigned kStaminaMax = 6;
constexpr unsigned kStaminaTickRate = 7;
constexpr unsigned kVitalsId = 8;
constexpr unsigned kVitalsCategoryId = 9;
constexpr unsigned kVitalsLevel = 10;
constexpr unsigned kInvulnerability = 11;
constexpr unsigned kDisplayImmune = 12;
constexpr unsigned kMaxHealth = 13;
constexpr unsigned kManaGroup = 1;
constexpr unsigned kManaAmount = 0;
constexpr unsigned kManaMax = 3;
constexpr unsigned kManaTickRate = 4;
/**
 * Row of the player in the level variant vitals tables of the client: the CRC-32 of the row
 * name "player" (client lookup at image RVA 0x5C423D0). The client reads the regeneration
 * rates of the stats from this row.
 */
constexpr std::uint32_t kPlayerVitalsId = 0x98197A65;
/** A tick rate is a 16-bit float on the wire (client reader at image RVA 0x87AE90). Zero: no change with time. */
constexpr std::uint16_t kNoTickRate = 0;
/** Bit 0 of the flags of vitalsData: the entity is dead (client getter at image RVA 0x684AC70). */
constexpr std::uint8_t kVitalsDead = 0x01;
/**
 * Bit 0 of healthChangeFlags: the client gives the new health to the listeners of the vitals
 * component (the name plate). Bit 1 makes the client of the owner show a number for a health
 * gain: the backend does not set it for a creature.
 */
constexpr std::uint8_t kHealthChangeNotify = 0x01;
/**
 * Bit 1 of healthChangeFlags (stage 3.3b-S6, live check L3): the client of the owner shows a green number of the gain (its new
 * health less the health that it had; client callback at image RVA 0x689FDC0). The owner view of the state that carries a heal with
 * a number has it; an observer shows no number for it (live check L3), and its view does not have it.
 */
constexpr std::uint8_t kHealthGainNumber = 0x02;
/** The field maxHealth has 16 bits. */
constexpr float kMaxHealthField = 65535.0F;

/**
 * vitalsData (client reader at image RVA 0x69515B0): the flags, the time of the last "death's
 * door" condition, the time in that condition, and its count. The backend has no such
 * condition: the three values are 0.
 */
void write_vitals_data(hub::Writer& writer, bool dead) {
    writer.u8(dead ? kVitalsDead : 0);
    writer.u64(0);
    writer.u64(0);
    writer.u8(0);
}

} // namespace

std::vector<hub::Bytes> encode_creature_vitals_state(const world::Entity& entity, View, const PartsRequest& request) {
    const world::Vitals& vitals = entity.vitals;
    const world::Creature& creature = entity.creature;
    StateWriter state(kGroups);
    state.field(0, kHealthAmount).f32(vitals.health);
    write_vitals_data(state.field(0, kVitalsData), creature.dead);
    // Without this bit the client stores the health and tells no listener: the bar of the name
    // plate does not change (client code at image RVA 0x689FDC0; live proof).
    state.field(0, kHealthChangeFlags).u8(kHealthChangeNotify);
    const std::uint8_t immune = creature.immune() ? 1 : 0;
    if (request.since != 0 && creature.immuneChanged > request.since) {
        state.field(0, kInvulnerability).u8(immune);
        state.field(0, kDisplayImmune).u8(immune);
    }
    if (request.since == 0) {
        // The client keeps the 16-bit maximum only from a state that has HealthMax too (client
        // code at image RVA 0x683E670).
        state.field(0, kHealthMax).f32(vitals.healthMax);
        state.field(0, kHealthTickRate).u16(kNoTickRate);
        state.field(0, kVitalsId).u32(creature.kind->vitalsId);
        state.field(0, kVitalsCategoryId).u32(creature.kind->categoryId);
        state.field(0, kVitalsLevel).u32(creature.level);
        state.field(0, kInvulnerability).u8(immune);
        state.field(0, kDisplayImmune).u8(immune);
        state.field(0, kMaxHealth).u16(static_cast<std::uint16_t>(std::min(vitals.healthMax, kMaxHealthField)));
    }
    std::vector<hub::Bytes> parts;
    parts.push_back(state.finish());
    return parts;
}

namespace {

/**
 * @param life True: the state also has the life data (the dead mark) and the mark "the health
 *        changed", for a state after the first one and for a dead character.
 * @param gain True: the state carries a heal with a number for the owner (bit 1 of healthChangeFlags).
 */
hub::Bytes player_vitals(const world::Entity& entity, View view, bool life, bool gain, float health) {
    const world::Vitals& vitals = entity.vitals;
    StateWriter state(kGroups);
    state.field(0, kHealthAmount).f32(health);
    if (life) {
        // Bit 0 of the flags of vitalsData is the death of the character: the client of the
        // owner shows its death screen with it, and it closes that screen when the bit is 0
        // again (client code at image RVA 0x67A6F10). The health 0 alone is not a death.
        write_vitals_data(state.field(0, kVitalsData), entity.life.dead);
        // The listeners of the vitals component (the name plate of a remote player) get the new health; the owner the number of a heal.
        state.field(0, kHealthChangeFlags).u8(static_cast<std::uint8_t>(kHealthChangeNotify | (gain ? kHealthGainNumber : 0)));
    }
    state.field(0, kHealthMax).f32(vitals.healthMax);
    state.field(0, kHealthTickRate).u16(kNoTickRate);
    // The client shows the 16-bit maximum on the HUD (client getter at image RVA 0x67FCDC0).
    state.field(0, kMaxHealth).u16(static_cast<std::uint16_t>(vitals.healthMax));
    state.field(0, kVitalsId).u32(kPlayerVitalsId);
    state.field(0, kStaminaAmount).f32(vitals.stamina);
    state.field(0, kStaminaMax).f32(vitals.staminaMax);
    state.field(0, kStaminaTickRate).u16(kNoTickRate);
    if (view == View::owner) {
        state.field(kManaGroup, kManaAmount).f32(vitals.mana);
        state.field(kManaGroup, kManaMax).f32(vitals.manaMax);
        state.field(kManaGroup, kManaTickRate).u16(kNoTickRate);
    }
    return state.finish();
}

} // namespace

hub::Bytes encode_vitals_state(const world::Entity& entity, View view, const world::WorldClock&) {
    return player_vitals(entity, view, false, false, entity.vitals.health);
}

std::vector<hub::Bytes> encode_vitals_parts(const world::Entity& entity, View view, const PartsRequest& request) {
    // The first state of a session for a character that lives has no life data: the client has
    // the start values (alive). A character can be dead when a session starts to observe it.
    std::vector<hub::Bytes> parts;
    const bool life = request.since != 0 || entity.life.dead;
    // The number of a heal (stage 3.3b-S6): only the owner, only for a heal after the last state of this session (the same health
    // again gives no second number: live check L3). The first state of a session has none. The client shows the change of its health
    // in a state with bit 1: each heal with a number is its own state, from the health before it to the health after it, and a change
    // with no number (the rest rule, a food) is a state with no bit 1 (code review of stage 3.3b-S6, finding S6-R7: a silent gain made
    // the next number larger).
    if (view == View::owner && request.since != 0) {
        const std::vector<world::Vitals::HealStep>& steps = entity.vitals.healSteps;
        float current = request.ownerHealth;
        bool any = false;
        for (const world::Vitals::HealStep& step : steps) {
            if (step.revision <= request.since) {
                continue;
            }
            if (current >= 0.0F && std::fabs(step.from - current) > 0.001F) {
                parts.push_back(player_vitals(entity, view, life, false, step.from));
            }
            parts.push_back(player_vitals(entity, view, life, true, step.to));
            current = step.to;
            any = true;
        }
        if (any) {
            if (std::fabs(entity.vitals.health - current) > 0.001F) {
                parts.push_back(player_vitals(entity, view, life, false, entity.vitals.health));
            }
            return parts;
        }
    }
    parts.push_back(player_vitals(entity, view, life, false, entity.vitals.health));
    return parts;
}

} // namespace soulwarden::backend::server::rep

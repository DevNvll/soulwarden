#include <algorithm>
#include <cctype>
#include <optional>
#include <string_view>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>

#include "gamedata/progression_tables.h"
#include "server/rep/replication.h"
#include "server/rep/states/half_float.h"
#include "server/rep/states/state_codecs.h"

// The states of a spell entity (stage 3.3b-S2a, protocol notes 4.25; research abilities-shared.md Q2, live check L4).
//
// MB::SpellComponentReplicatedState (type 2912), the Spell component of the slice of the spell (slot of the slice, game
// data: 1 for the vortex of Gravity Well). One group, eleven fields (constructor 0x67185C0, apply 0x683DE00):
//   0 spllDtId   u32: the CRC-32 of the lower-case SpellID (the client resolves the spell row with it)
//   1 strtpos    x:f32 y:f32 z:u16 (the height over -100 to 2000: reader 0x5CEA190, not the range of the movement state)
//   2 strtrot    compressed quaternion: 07 = identity (reader 0x5CEA140 -> 0x87B250)
//   3 chldpos    the same as 1
//   4 chldrot    the same as 2
//   5 prevAttch  actor:raw16 group:u64; none: the zero actor and the group 00000000FFFFFFFF (constructor 0x6718390)
//   6 attch      the same: the entity that the spell follows (stage 3.3b-S2b, live check: the Beacon on a remote character
//                follows each move of the character), or none (the vortex, the Pylon)
//   7 casterId   the actor and the group of the caster (not of the spell)
//   8 spawnTime  u64: the start on the server time in microseconds (the unit of the time anchor of a movement state; the
//                unit of this field is not proved, and the client shows no change for it: live check L4)
//   9 numSpwn    u32: 0
//   10 m_aoeRadiusScaling f32: 1
// The field masks are chained: each mask byte comes before the values of its 7 fields (01 ff ... 0f ...). Live check L4:
// the smallest record that shows the vortex has the fields 0 and 1; the backend sends each field.
//
// MB::DelayedEventComponentReplicatedState (type 3139), one fragment for each DelayedEventComponent of the slice (the
// slots of the slice, game data: 4 to 9 for the vortex). One group, two fields (reader 0x3066370):
//   0 totalDelayDuration  half float: the time that is left (seconds)
//   1 delayedEventCompleted u8
// The client runs the delay from this time (live check L4: a late observer gets the burst at the right age; with no state
// it starts the delay of the slice at the record). An event whose time passed:
//   - a deactivation or a trigger: completed (the client applies it at once: the vortex of a late observer after 3 s does
//     not show);
//   - an activation (the burst): not completed, with a time after the end of the spell: a completed state makes the client
//     play the burst again at once (live check L4), and the stop of the spell comes first.
//
// Stage 3.4f-C (live check L8): a passed activation of an entity with an AreaStatusEffectComponent of the enemies (the cloud of the mushroom
// of the Dryad Shaman) is completed for a late observer: the client then shows the lasting area at once (with the hold of a burst it shows no cloud). A
// passed activation of a burst keeps the hold (the bolt of the lightning strike plays again with a completed state).
//
// Stage 3.3b-S2b (live checks of the Pylon, the trap, and the Beacon):
//   - A delayed event that starts at a trigger of its owner entity (m_startDelayOnTrigger, no start at the init: the death chain
//     of the Pylon, the charge picture of its shots) has no state until the server triggers its owner: then its time runs from
//     the trigger. A delayed event that starts at the init and whose target the server triggered (the event of slot 11 of the
//     Pylon that triggers DestroyTrigger at 45 s) is completed from the trigger on: the client triggers its target at once and
//     plays the chain of the slice (live check: the light pillar, the ice shards, the Pylon gone 1.0 s later).
//   - MB::PositionInTheWorldReplicatedState (type 13; constructor 0x5BB27D0), the PositionInTheWorld component of a slice with
//     no Spell component (the trap of the musket: slot 4). One group: 0 position (the reader of the Spell position, 0x5CEA190),
//     1 rotation (0x5CEA140: 07 = identity), 2 scale (not sent). Live check: with the metadata state alone the trap does not
//     show; with this state it shows open on the ground at the place.
//   - The vitals state of a summon or a trap (MB::VitalsComponentReplicatedState at the Vitals slot of the slice: the Pylon 1, the
//     trap 1): the first state has the health, the life data, the change mark, the maximum, the tick rate 0, the vitals id (the
//     CRC-32 of the lower-case vitals row of the slice), the immune values 0, and the 16-bit maximum; a later state the health,
//     the life data (dead at 0), and the change mark. The client shows no name plate and no health bar for the Pylon of its
//     owner (live check): the state keeps the health of the client equal to the health of the server.
//
// A spell has only the observer view: the session of the caster observes its own spell as each other session does.

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 1;
constexpr unsigned kRowKey = 0;
constexpr unsigned kStartPosition = 1;
constexpr unsigned kStartRotation = 2;
constexpr unsigned kChildPosition = 3;
constexpr unsigned kChildRotation = 4;
constexpr unsigned kPreviousAttachment = 5;
constexpr unsigned kAttachment = 6;
constexpr unsigned kCaster = 7;
constexpr unsigned kSpawnTime = 8;
constexpr unsigned kSpawnCount = 9;
constexpr unsigned kRadiusScale = 10;
constexpr unsigned kRemaining = 0;
constexpr unsigned kCompleted = 1;
/** The vitals state (states/vitals_state.cpp: the field numbers of group 0). */
constexpr unsigned kVitalsGroups = 2;
constexpr unsigned kHealthAmount = 0;
constexpr unsigned kVitalsData = 2;
constexpr unsigned kHealthChangeFlags = 3;
constexpr unsigned kHealthMax = 4;
constexpr unsigned kHealthTickRate = 5;
constexpr unsigned kVitalsId = 8;
constexpr unsigned kInvulnerability = 11;
constexpr unsigned kDisplayImmune = 12;
constexpr unsigned kMaxHealth = 13;
/** The rotation (0, 0, 0, 1): the header bits of x, y, and z are 0 (0x87B250). */
constexpr std::uint8_t kIdentityRotation = 0x07;
/** The height range of the position of a spell (reader 0x5CEA190). */
constexpr float kHeightMin = -100.0F;
constexpr float kHeightMax = 2000.0F;
/** The group of a reference with no entity (constructor 0x6718390). */
constexpr std::uint64_t kNoGroup = 0x00000000FFFFFFFFULL;
/** A passed activation waits this much after the end of the spell (the stop of the spell comes first). */
constexpr std::chrono::milliseconds kHoldAfterEnd{1000};

void write_position(hub::Writer& writer, const state::WorldPosition& position) {
    writer.u32(std::bit_cast<std::uint32_t>(position.x));
    writer.u32(std::bit_cast<std::uint32_t>(position.y));
    const float clamped = std::clamp(position.z, kHeightMin, kHeightMax);
    writer.u16(static_cast<std::uint16_t>(std::lround((clamped - kHeightMin) / (kHeightMax - kHeightMin) * 65535.0F)));
}

void write_reference(hub::Writer& writer, const world::ActorId* actor) {
    if (actor == nullptr) {
        writer.raw(world::ActorId{}.bytes);
        writer.u64(kNoGroup);
        return;
    }
    writer.raw(actor->bytes);
    writer.u64(world::entity_group_id(*actor));
}

} // namespace

hub::Bytes encode_spell_state(const world::Entity& entity, View view, const world::WorldClock& clock) {
    (void)clock;
    if (entity.kind != world::EntityKind::spell || entity.spell.slice == nullptr || entity.spell.slice->spellSlot == 0) {
        return {};
    }
    const world::SpellBody& spell = entity.spell;
    StateWriter state(kGroups);
    state.field(0, kRowKey).u32(spell.key);
    write_position(state.field(0, kStartPosition), entity.position);
    state.field(0, kStartRotation).u8(kIdentityRotation);
    write_position(state.field(0, kChildPosition), entity.position);
    state.field(0, kChildRotation).u8(kIdentityRotation);
    write_reference(state.field(0, kPreviousAttachment), nullptr);
    write_reference(state.field(0, kAttachment), spell.attached != 0 ? &spell.attachedActor : nullptr);
    write_reference(state.field(0, kCaster), &spell.casterActor);
    state.field(0, kSpawnTime)
        .u64(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(spell.started.time_since_epoch()).count()));
    state.field(0, kSpawnCount).u32(0);
    state.field(0, kRadiusScale).u32(std::bit_cast<std::uint32_t>(1.0F));
    (void)view;
    return state.finish();
}

std::vector<SlotState> encode_delayed_event_states(const world::Entity& entity, View view, const PartsRequest& request) {
    (void)view;
    std::vector<SlotState> states;
    if (entity.kind != world::EntityKind::spell || entity.spell.slice == nullptr || request.clock == nullptr) {
        return states;
    }
    const world::SpellBody& spell = entity.spell;
    const world::WorldClock::time_point now = request.clock->now();
    using Seconds = std::chrono::duration<float>;
    const auto lower_equal = [](std::string_view a, std::string_view b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                   return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
               });
    };
    // The latest trigger of an entity of the slice by the server (stage 3.3b-S2b). No value: none.
    const auto triggered = [&](std::string_view name) -> std::optional<world::WorldClock::time_point> {
        std::optional<world::WorldClock::time_point> at;
        for (const world::SpellTrigger& trigger : spell.triggers) {
            if (lower_equal(trigger.entity, name) && trigger.at <= now) {
                at = trigger.at;
            }
        }
        return at;
    };
    for (const gamedata::SpellEvent& event : spell.slice->events) {
        world::WorldClock::time_point start = spell.started;
        bool completed = false;
        if (const auto owner = event.onTrigger ? triggered(event.owner) : std::nullopt) {
            start = *owner; // The event of an owner that the server triggered: its time runs from the trigger.
        } else if (!event.onInit) {
            continue; // No start yet: the client keeps it until a trigger.
        } else if (event.type == gamedata::spell_event::kTrigger && triggered(event.target)) {
            completed = true; // The server triggered its target: the client triggers it at once (live check of the Pylon).
        }
        float remaining = 0.0F;
        if (!completed) {
            const float delay = std::max(event.delay, 0.0F);
            const auto due = start + std::chrono::duration_cast<world::WorldClock::time_point::duration>(Seconds(delay));
            remaining = Seconds(due - now).count();
            if (remaining <= 0.0F) {
                // An activation that passed of an entity with an AreaStatusEffectComponent (stage 3.4f-C: the cloud of the mushroom, a
                // lasting area): completed, the client activates it at once (live check L8, M4b; the hold of a burst hides it, M4a).
                const bool lasting = std::any_of(spell.slice->areaStatuses.begin(), spell.slice->areaStatuses.end(), [&](const gamedata::SpellAreaStatus& area) {
                    return area.targets == 2 && lower_equal(area.entity, event.target);
                });
                if (event.type == gamedata::spell_event::kActivate && !lasting) {
                    // An activation that passed (the burst of the vortex, the bolt of the lightning strike): the client must not play it
                    // again (live check L4 of stage 3.3b-S2a, live check L8 of stage 3.4f-C, S3a and S3b).
                    remaining = Seconds(spell.ends - now + kHoldAfterEnd).count();
                } else {
                    remaining = 0.0F;
                    completed = true;
                }
            }
        }
        StateWriter state(kGroups);
        state.field(0, kRemaining).u16(half_of(remaining));
        state.field(0, kCompleted).u8(completed ? 1 : 0);
        states.push_back(SlotState{event.slot, state.finish()});
    }
    return states;
}

hub::Bytes encode_spell_place_state(const world::Entity& entity, View view, const world::WorldClock& clock) {
    (void)view;
    (void)clock;
    if (entity.kind != world::EntityKind::spell || entity.spell.slice == nullptr || entity.spell.slice->positionSlot == 0) {
        return {};
    }
    return position_in_the_world_state(entity.position, {0.0F, 0.0F, 0.0F, 1.0F}); // The rotation (0, 0, 0, 1): `07`.
}

std::vector<hub::Bytes> encode_spell_vitals_parts(const world::Entity& entity, View view, const PartsRequest& request) {
    (void)view;
    if (entity.kind != world::EntityKind::spell || entity.spell.vitalsRow == nullptr) {
        return {};
    }
    const world::Vitals& vitals = entity.vitals;
    StateWriter state(kVitalsGroups);
    state.field(0, kHealthAmount).f32(vitals.health);
    // vitalsData (states/vitals_state.cpp): the flags (bit 0: dead), two times, and a count.
    hub::Writer& data = state.field(0, kVitalsData);
    data.u8(vitals.health <= 0.0F ? 1 : 0);
    data.u64(0);
    data.u64(0);
    data.u8(0);
    state.field(0, kHealthChangeFlags).u8(1);
    if (request.since == 0) {
        state.field(0, kHealthMax).f32(vitals.healthMax);
        state.field(0, kHealthTickRate).u16(0);
        state.field(0, kVitalsId).u32(gamedata::progression_id(entity.spell.vitalsRow->id));
        state.field(0, kInvulnerability).u8(0);
        state.field(0, kDisplayImmune).u8(0);
        state.field(0, kMaxHealth).u16(static_cast<std::uint16_t>(std::clamp(vitals.healthMax, 0.0F, 65535.0F)));
    }
    std::vector<hub::Bytes> parts;
    parts.push_back(state.finish());
    return parts;
}

} // namespace soulwarden::backend::server::rep

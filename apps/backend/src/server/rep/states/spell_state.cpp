#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>

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
//   6 attch      the same: none (the vortex is not attached)
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
    if (entity.kind != world::EntityKind::spell || entity.spell.slice == nullptr) {
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
    write_reference(state.field(0, kAttachment), nullptr);
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
    for (const gamedata::SpellEvent& event : spell.slice->events) {
        const float delay = std::max(event.delay, 0.0F);
        const auto due = spell.started + std::chrono::duration_cast<world::WorldClock::time_point::duration>(Seconds(delay));
        float remaining = Seconds(due - now).count();
        bool completed = false;
        if (remaining <= 0.0F) {
            if (event.type == gamedata::spell_event::kActivate) {
                // An activation that passed (the burst): the client must not play it again (live check L4).
                remaining = Seconds(spell.ends - now + kHoldAfterEnd).count();
            } else {
                remaining = 0.0F;
                completed = true;
            }
        }
        StateWriter state(kGroups);
        state.field(0, kRemaining).u16(half_of(remaining));
        state.field(0, kCompleted).u8(completed ? 1 : 0);
        states.push_back(SlotState{event.slot, state.finish()});
    }
    return states;
}

} // namespace soulwarden::backend::server::rep

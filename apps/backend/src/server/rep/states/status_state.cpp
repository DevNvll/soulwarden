#include <bit>
#include <cmath>

#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// MB::StatusEffectsComponentReplicatedState (constructor at image RVA 0x4013900, apply at 0x4028F70): the component
// StatusEffects of a creature slice (stage 3.3a) and of the player slices (slot 44, stage 3.3b-S1). Group 0 is empty;
// group 1 has the maps of the owner, group 2 shared values, and group 3 the maps of an observer. A map is a list in the
// full form: 00, stamp := 01 sequence:size64, count:size, entries. A list with a larger stamp replaces the list of the
// client; a list with a smaller stamp has no effect (live check L1 of stage 3.3b-S1); an empty list removes each entry.
// The backend sends the world revision of the change as the stamp.
//
// Entries (big-endian numbers):
//   full      statusIndex:u16 wallEnd:u64 potency:f16 stacks:u8 flags:u8     (reader 0x4044140, 0x4004F80)
//   tray      statusCrc:u32 wallEnd:u64 stacks:u8 potency:f32               (reader 0x40428E0; HUD 0x403D850)
//   compact   statusIndex:u16 packed:u8 (bits 0 to 6: the stacks; bit 7: the potency 1; reader 0x4044030, 0x5CE77B0)
// statusIndex is the number of the status row (the position of its CRC in the CRC list of all rows), statusCrc the CRC-32
// of the lower-case status id. wallEnd is the end on the synced wall clock of the client (Unix ns): the Player state
// (player_state.cpp) gives the client the wall clock pair first. flags is 0 (the meaning of its bits is not known).
//
// Creature  observer: group 3 field 1, one entry for each effect of the creature that shows at the name plate (one
//           stack, the potency 1). The client shows the icon of a creature from the compact map only (live check L5 of
//           stage 3.3a). No fragment in the first record of a creature with no such effect.
// Player    owner: group 1 field 0 the full map with each status of the character on the wire (not a status of the gear:
//           the client has the items), field 1 the lightweight map (empty: the full map has each status), field 2 the
//           tray, empty. The live checks of stage 3.3b-S1: an entry of the full map alone gives the HUD icon of a row
//           with ShowInUITray, its countdown, its stack count, its visual effect, and its values in the character sheet
//           (L1: the armor of a dust); a tray entry gives a second icon of the same status (live run s1-live: a food
//           and its recovery status showed four icons). The empty tray removes a tray icon of an earlier state.
//           observer: group 3 field 0 the full map and field 1 the compact map with the public statuses (ShowInNameplates,
//           AlwaysReplicate, ForceReplicateToRemotes; the compact map only with ShowInNameplates: the icon at the name
//           plate, live check L2). No tray and no owner field (test case ST7).
//           No fragment in the first record of a character with no such status.

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 4;
constexpr unsigned kOwnerGroup = 1;
constexpr unsigned kObserverGroup = 3;
constexpr unsigned kFullMap = 0;
constexpr unsigned kCompactMap = 1;
constexpr unsigned kLightweightMap = 1;
constexpr unsigned kTray = 2;
/** One stack and the potency 1. */
constexpr std::uint8_t kPackedOneStack = 0x81;

/** @return The IEEE half of a float (round to nearest), for the potency of a full entry. */
std::uint16_t half_of(float value) {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    const std::uint16_t sign = static_cast<std::uint16_t>((bits >> 16) & 0x8000U);
    const int exponent = static_cast<int>((bits >> 23) & 0xFFU) - 127 + 15;
    std::uint32_t mantissa = bits & 0x7FFFFFU;
    if (!std::isfinite(value) || exponent >= 31) {
        return static_cast<std::uint16_t>(sign | 0x7BFFU);
    }
    if (exponent <= 0) {
        return sign;
    }
    mantissa += 0x1000U;
    std::uint32_t result = (static_cast<std::uint32_t>(exponent) << 10) + (mantissa >> 13);
    if (result >= 0x7C00U) {
        result = 0x7BFFU;
    }
    return static_cast<std::uint16_t>(sign | result);
}

void stamp(hub::Writer& map, std::uint64_t sequence, std::size_t count) {
    map.size(0);
    map.u8(1);
    map.size64(sequence);
    map.size(static_cast<std::uint32_t>(count));
}

std::uint64_t wall_end(const world::ActiveStatus& status, const world::WorldClock& clock) {
    return status.permanent ? 0 : clock.wall_time_ns(status.ends);
}

void full_entry(hub::Writer& map, const world::ActiveStatus& status, const world::WorldClock& clock) {
    map.u16(status.index);
    map.u64(wall_end(status, clock));
    map.u16(half_of(status.potency));
    map.u8(status.stacks);
    map.u8(0);
}

} // namespace

std::vector<hub::Bytes> encode_player_status_state(const world::Entity& entity, View view, const PartsRequest& request) {
    if (request.clock == nullptr) {
        return {};
    }
    const world::WorldClock& clock = *request.clock;
    const world::WorldClock::time_point now = clock.now();
    std::vector<const world::ActiveStatus*> sent;
    for (const world::ActiveStatus& status : entity.statuses) {
        const bool running = status.permanent || now < status.ends;
        if (running && status.replicated && (view == View::owner || status.observed)) {
            sent.push_back(&status);
        }
    }
    if (request.since == 0 && sent.empty()) {
        return {};
    }
    const auto changed = entity.changed.find(world::ComponentKey::status);
    const std::uint64_t sequence = changed == entity.changed.end() ? 1 : changed->second;
    StateWriter state(kGroups);
    if (view == View::owner) {
        hub::Writer& full = state.field(kOwnerGroup, kFullMap);
        stamp(full, sequence, sent.size());
        for (const world::ActiveStatus* status : sent) {
            full_entry(full, *status, clock);
        }
        stamp(state.field(kOwnerGroup, kLightweightMap), sequence, 0);
        // The tray stays empty: the full map gives the icons (a tray entry is a second icon of the same status).
        stamp(state.field(kOwnerGroup, kTray), sequence, 0);
        return {state.finish()};
    }
    hub::Writer& full = state.field(kObserverGroup, kFullMap);
    stamp(full, sequence, sent.size());
    for (const world::ActiveStatus* status : sent) {
        full_entry(full, *status, clock);
    }
    std::vector<const world::ActiveStatus*> plates;
    for (const world::ActiveStatus* status : sent) {
        if (status->nameplate) {
            plates.push_back(status);
        }
    }
    hub::Writer& compact = state.field(kObserverGroup, kCompactMap);
    stamp(compact, sequence, plates.size());
    for (const world::ActiveStatus* status : plates) {
        compact.u16(status->index);
        compact.u8(static_cast<std::uint8_t>(0x80U | (std::min<unsigned>(status->stacks, 0x7FU))));
    }
    return {state.finish()};
}

std::vector<hub::Bytes> encode_creature_status_state(const world::Entity& entity, View, const PartsRequest& request) {
    std::vector<const world::CreatureEffect*> shown;
    for (const world::CreatureEffect& effect : entity.creature.effects) {
        if (effect.shown) {
            shown.push_back(&effect);
        }
    }
    if (request.since == 0 && shown.empty()) {
        return {};
    }
    const auto changed = entity.changed.find(world::ComponentKey::status);
    const std::uint64_t sequence = changed == entity.changed.end() ? 1 : changed->second;
    StateWriter state(kGroups);
    hub::Writer& map = state.field(kObserverGroup, kCompactMap);
    map.size(0);
    map.u8(1);
    map.size64(sequence);
    map.size(static_cast<std::uint32_t>(shown.size()));
    for (const world::CreatureEffect* effect : shown) {
        map.u16(effect->index);
        map.u8(kPackedOneStack);
    }
    return {state.finish()};
}

} // namespace soulwarden::backend::server::rep

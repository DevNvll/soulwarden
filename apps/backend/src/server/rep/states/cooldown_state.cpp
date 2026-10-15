#include <chrono>

#include "gamedata/progression_tables.h"
#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"
#include "world/skills.h"

// MB::CooldownTimersComponentReplicatedState (constructor at image RVA 0x66F3C90, apply at
// 0x683B0B0), slot 45 of the player slice (component CooldownTimers of the entity "Stats"):
// the cooldowns of the abilities of the character (stage 3.3a, protocol notes 4.22). One group;
// the fields 0, 1, and 2 are the maps of the three places of the ability bar. Each map is a
// list in the full form (writer 0x666C2B0, reader 0x6951A10):
//   00 (no change entries), stamp := 01 sequence:size64, count:size,
//   ( abilityId:u32 end:u64 minimumEnd:u64 )*
// The two times are the server time in microseconds (the time of the world clock; the client
// computes the remaining time from its actor clock, getter 0x67E9840). The client keys the
// three maps by the input numbers of the three places (CooldownTimers +0x5D4, +0x5D8, +0x5DC:
// 402, 115, 139 in the live client), thus field N is the map of place N of the bar of each
// weapon, and the key in the map is the ability id (live check L4). A list with a larger stamp
// replaces the list of the client. The backend sends the world revision of the change.
//
// View     Fields
// owner    group 0: the three maps, with each cooldown of an ability that is on a place of a
//          bar of the character. No fragment in the first record of a character with no
//          cooldown. Field 3 (ccdmap, stage 1.3b) when the character has a cooldown of a
//          consumable: one entry for each item, key = the CRC-32 of the lower-case item id, the
//          end, and two values 0 (key:u32 end:u64 a:u32 b:u32; live check I7: the key of the
//          item shows the countdown on its quick slot, the key of the cooldown id shows nothing).
// observer no fragment: the remote player slice has no cooldown component.

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 1;
/** Field 3: the cooldowns of the consumables by item (ccdmap). */
constexpr unsigned kItemMap = 3;

std::uint64_t microseconds(world::WorldClock::time_point time) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(time.time_since_epoch()).count());
}

/** @return The place of an ability on a bar of the character (0 to 2). No value: on no bar. */
std::optional<unsigned> place_of(const world::SkillSet& skills, std::uint32_t ability) {
    for (const auto& [table, bar] : skills.bars) {
        for (unsigned place = 0; place < bar.size(); ++place) {
            if (bar[place] == ability) {
                return place;
            }
        }
    }
    return std::nullopt;
}

} // namespace

std::vector<hub::Bytes> encode_cooldown_state(const world::Entity& entity, View view, const PartsRequest& request) {
    if (view != View::owner || (request.since == 0 && entity.cooldowns.empty() && entity.itemCooldowns.empty())) {
        return {};
    }
    const auto changed = entity.changed.find(world::ComponentKey::cooldowns);
    const std::uint64_t sequence = changed == entity.changed.end() ? 1 : changed->second;
    std::array<std::vector<std::pair<std::uint32_t, const world::AbilityCooldown*>>, world::kBarSlots> maps;
    for (const auto& [ability, timer] : entity.cooldowns) {
        if (const std::optional<unsigned> place = place_of(entity.skills, ability)) {
            maps[*place].emplace_back(ability, &timer);
        }
    }
    StateWriter state(kGroups);
    for (unsigned place = 0; place < world::kBarSlots; ++place) {
        hub::Writer& field = state.field(0, place);
        field.size(0);
        field.u8(1);
        field.size64(sequence);
        field.size(static_cast<std::uint32_t>(maps[place].size()));
        for (const auto& [ability, timer] : maps[place]) {
            field.u32(ability);
            field.u64(microseconds(timer->end));
            field.u64(microseconds(timer->minEnd));
        }
    }
    if (!entity.itemCooldowns.empty()) {
        hub::Writer& field = state.field(0, kItemMap);
        field.size(0);
        field.u8(1);
        field.size64(sequence);
        field.size(static_cast<std::uint32_t>(entity.itemCooldowns.size()));
        for (const world::ItemCooldown& cooldown : entity.itemCooldowns) {
            field.u32(cooldown.item);
            field.u64(microseconds(cooldown.end));
            field.u32(0);
            field.u32(0);
        }
    }
    return {state.finish()};
}

} // namespace soulwarden::backend::server::rep

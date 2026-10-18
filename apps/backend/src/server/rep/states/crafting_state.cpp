#include <algorithm>
#include <chrono>
#include <vector>

#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// MB::CraftingComponentReplicatedState (type 205, apply at image RVA 0x2E819C0), slot 63 of the player slice (stage 3.7b,
// world/crafting.h, docs/systems/crafting.md, protocol notes 4.51). One group with two maps in the full form (readers 0x2E897C0 and
// 0x2E89910):
//   field 0, the cooldowns of the recipes:   changes:size = 0, 01 sequence:size64, count:size (100 at most), {recipeCrc:u32 used:u8 end:u64}*
//   field 1, the gear score bonus of a skill: changes:size = 0, 01 sequence:size64, count:size (21 at most), {skillCode:u8 bonus:u16}*
// (big-endian numbers). recipeCrc is the CRC-32 of the lower-case recipe id; `used` the crafts in the window; `end` the end of the window on
// the synced wall clock of the client (Unix nanoseconds, the clock of the end of a status effect: the Player state gives the wall clock
// pair). Live check L6 of stage 3.7b: IngotT51 with used 1 and the end in one hour showed "Crafting Cooldown: 59m 57s per 9 items";
// used 10 of 10 showed "per 0 items" and a disabled CRAFT; at the end the client itself showed "per 10 items" again. The skill code is the
// TradeskillType of the client with Weaponsmithing 0 (Engineering 6: the bonus 50 of code 6 changed the knife of the Workshop from gear
// score 205 to 255). Stage 3.7c: field 1 is the craft gear score bonus of the statuses of the character (Entity::craftGearBonus: the
// MaxGS<skill> values of a food, of the status of a perk of an equipped item, of a trophy). Live check L5: the client does not add the
// statuses itself (the food FoodEngineerT1 on the HUD left the knife at 205; the entry 6 = 5 gave 210, also with the food: no double), and it
// caps the value at 50 (the entry 6 = 60 gave 255).
//
// View     Fragments
// owner    group 0, both maps: each cooldown with an active window (its end after now) and each skill with a bonus, at each change (also
//          empty maps after a change). No fragment in the first record of a character with no active cooldown and no bonus (the client
//          works with no state 205: live check L1 of stage 3.7a).
// observer no fragment: the remote player slice has no Crafting component.

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 1;
/** The limits of the readers of the client: 100 cooldown entries (0x2E897C0: count < 0x65), 21 bonus entries (0x2E89910). */
constexpr std::size_t kMaxCooldowns = 100;
constexpr std::size_t kMaxBonuses = 21;

} // namespace

std::vector<hub::Bytes> encode_crafting_state(const world::Entity& entity, View view, const PartsRequest& request) {
    if (view != View::owner || request.clock == nullptr) {
        return {};
    }
    const world::WorldClock& clock = *request.clock;
    const world::WorldClock::time_point now = clock.now();
    std::vector<std::pair<std::uint32_t, const world::CraftCooldown*>> active;
    for (const auto& [crc, cooldown] : entity.craftCooldowns) {
        if (now < cooldown.end) {
            active.emplace_back(crc, &cooldown);
        }
    }
    if (request.since == 0 && active.empty() && entity.craftGearBonus.empty()) {
        return {};
    }
    // More than 100 windows (no player has them: 61 recipes of the supported build have a cooldown, 45 of them with no cause): the 100
    // that end first.
    if (active.size() > kMaxCooldowns) {
        std::sort(active.begin(), active.end(), [](const auto& one, const auto& two) { return one.second->end < two.second->end; });
        active.resize(kMaxCooldowns);
    }
    const auto changed = entity.changed.find(world::ComponentKey::crafting);
    const std::uint64_t sequence = changed == entity.changed.end() ? 1 : changed->second;
    StateWriter state(kGroups);
    hub::Writer& cooldowns = state.field(0, 0);
    cooldowns.size(0);
    cooldowns.u8(1);
    cooldowns.size64(sequence);
    cooldowns.size(static_cast<std::uint32_t>(active.size()));
    for (const auto& [crc, cooldown] : active) {
        cooldowns.u32(crc);
        cooldowns.u8(static_cast<std::uint8_t>(std::min<std::uint32_t>(cooldown->used, 255)));
        cooldowns.u64(clock.wall_time_ns(cooldown->end));
    }
    hub::Writer& bonuses = state.field(0, 1);
    bonuses.size(0);
    bonuses.u8(1);
    bonuses.size64(sequence);
    // The codes are the 21 TradeskillType values (0 to 20): the map has 21 entries at most.
    const std::size_t count = std::min(entity.craftGearBonus.size(), kMaxBonuses);
    bonuses.size(static_cast<std::uint32_t>(count));
    std::size_t written = 0;
    for (const auto& [code, bonus] : entity.craftGearBonus) {
        if (written++ == count) {
            break;
        }
        bonuses.u8(code);
        bonuses.u16(bonus);
    }
    std::vector<hub::Bytes> parts;
    parts.push_back(state.finish());
    return parts;
}

} // namespace soulwarden::backend::server::rep

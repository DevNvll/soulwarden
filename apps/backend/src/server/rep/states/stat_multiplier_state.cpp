#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// MB::StatMultiplierTableReplicatedState (type 1525), the component StatMultiplierTable of the player slice, slot 43
// (stage 3.3b-S2a, criterion I10; research status-speed.md). The run and the sprint of the client read the value of a key
// of the owner table (getter 0x3FEA880: an absent key is 1.0) every tick of their actions (0x2A91860), and the movement
// scales its speed with it. The status map (slot 44) does not change the speed (live check L1 of stage 3.3b-S1).
//
// View     Fields
// owner    group 1 field 0 (multiplierTable): a full table snapshot
//            deltaCount:size = 0, hasSequence:u8 = 1, sequence:size64, count:size, entries
//            entry := key:u8 value:i32 (big-endian, the multiplier times 10000: DECIMAL_PLACES 4 of the slice) m_syncVitals:u8
//          the keys 0x34 (MoveSpeedMod) and 0x35 (SprintSpeedMod) with the multipliers of the character
//          (Entity::moveMultiplier, sprintMultiplier). Both at 1: the empty table (live check: it gives 1.0 again).
//          The sequence is the world revision of the change: a newer table has a larger sequence.
// observer no fragment (the remote map of group 2 is not proved; the remote player needs no speed).
// The first record of a character with both multipliers at 1 has no fragment (the client makes an empty table itself).
//
// Live check S2a-I10 (status-speed.md L1, L2): the key 0x35 scales the run and the sprint of the client; the key 0x34 did
// not change them in the walks of the check. The backend sends both from the statuses.

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 3;
constexpr unsigned kOwnerGroup = 1;
constexpr unsigned kMultiplierTable = 0;
constexpr std::uint8_t kMoveSpeedKey = 0x34;
constexpr std::uint8_t kSprintSpeedKey = 0x35;
constexpr float kScale = 10000.0F;

std::uint32_t wire_value(float multiplier) {
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(std::lround(multiplier * kScale)));
}

} // namespace

std::vector<hub::Bytes> encode_stat_multiplier_state(const world::Entity& entity, View view, const PartsRequest& request) {
    if (view != View::owner) {
        return {};
    }
    const bool neutral = entity.moveMultiplier == 1.0F && entity.sprintMultiplier == 1.0F;
    if (neutral && request.since == 0) {
        return {};
    }
    const auto changed = entity.changed.find(world::ComponentKey::statMultipliers);
    const std::uint64_t sequence = changed != entity.changed.end() ? changed->second : 1;
    StateWriter state(kGroups);
    hub::Writer& table = state.field(kOwnerGroup, kMultiplierTable);
    table.size(0); // no delta operation: a full table
    table.u8(1);   // the sequence is present
    table.size64(sequence);
    if (neutral) {
        table.size(0);
        return {state.finish()};
    }
    table.size(2);
    for (const auto& [key, value] : {std::pair<std::uint8_t, float>{kMoveSpeedKey, entity.moveMultiplier}, {kSprintSpeedKey, entity.sprintMultiplier}}) {
        table.u8(key);
        table.u32(wire_value(value));
        table.u8(0); // m_syncVitals
    }
    return {state.finish()};
}

} // namespace soulwarden::backend::server::rep

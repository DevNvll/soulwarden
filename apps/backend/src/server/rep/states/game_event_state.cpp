#include <algorithm>

#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// The daily bonuses of a character (stage QG6b, docs/systems/mission-boards.md, the daily bonus; static read
// evidence/quests/qg6b/implement/static/faction-board/FACTION.md section 5).
//
// GameEvent state (type 497, constructor 0x31E5850), slot 68 of the player slice (research factions.md 4.4). Group 0: field 0 gameEvents,
// field 1 dailyBonusesUsed (state +0xE40). The backend writes only field 1, the full form of the reader 0x3205890 (element reader
// 0x31DFDF0, five entries at most):
//   00 hasVersion:u8 (1) version:size64 count:size { modifierCrc:u32 bonusesUsed:u8 }*
// The apply 0x31FAFF0 copies the entries into the used-count map of the client; GetAvailableDailyBonuses 0x31F1140 gives the maximum (the
// default 3 of javelin.daily-bonus-mission-count) minus the uses of the key of FactionMissionBonus (0x85D30F59).
//
// View     Fragments
// owner    the state when the character has a use of a daily bonus (a character with none has no fragment: the client counts no use)
// observer no fragment

namespace soulwarden::backend::server::rep {

hub::Bytes encode_game_event_state(const world::Entity& entity, View view, const world::WorldClock& clock) {
    if (view != View::owner || entity.kind != world::EntityKind::player || entity.dailyBonuses.uses.empty()) {
        return {};
    }
    const auto found = entity.changed.find(world::ComponentKey::gameEvent);
    const std::uint64_t sequence = found != entity.changed.end() && found->second != 0 ? found->second : 1;
    const std::uint64_t now = clock.wall_time_ns(clock.now());
    StateWriter state(1);
    hub::Writer& used = state.field(0, 1);
    used.size(0);
    used.u8(1);
    used.size64(sequence);
    const auto count = static_cast<std::uint32_t>(std::min<std::size_t>(entity.dailyBonuses.uses.size(), 5));
    used.size(count);
    std::uint32_t written = 0;
    for (const auto& [modifier, use] : entity.dailyBonuses.uses) {
        if (written == count) {
            break;
        }
        used.u32(modifier);
        // A use of an earlier day counts as 0 (the reset of the day: the uses of the row stay until the next hand-in writes the row).
        used.u8(now < use.resetNs ? use.used : 0);
        ++written;
    }
    return state.finish();
}

} // namespace soulwarden::backend::server::rep

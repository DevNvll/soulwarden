#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// The Faction state of a character (stage QG7, world/factions.h; docs/systems/factions.md; protocol notes 4.63).
//
// MB::FactionComponentReplicatedState (type 3152, constructor at image RVA 0x30A7910, common reader 0x17B4110), slot 49 of the player slice
// and of the remote player slice (the component Faction of the entity Social). Four groups (group 0 has no field); the fields of this stage:
//   group 1 field 3  lastFactionChangeTimepoint  u64 big-endian (reader 0x279E480): Unix nanoseconds of the wall clock of the world
//   group 1 field 4  factionChangeCount          u16 big-endian (reader 0x2A42E10)
//   group 2 field 0  faction                     u8 (reader 0x17B3C60): 0 none, 1 Syndicate, 2 Marauder, 3 Covenant
// The initializer 0x30B6BA0 gives group 1 the owner as its only target and group 3 the null target (no normal client); group 2 has no
// target list: each observer gets it. The backend sends no PvP field (pvpFlag, ffaFlag, the pending times, group 3): a later stage.
// The apply 0x30B6210 sets the faction (0x30C0A40) and republishes it on the HUD (0x30C2E50) at each received faction field, also when
// the value is the same: after a refused request the owner gets the faction of the server, and the HUD has it again.
// Bytes: the Syndicate for an observer `04 01 01`; the owner with time 0 and count 0 `06 18 <8 bytes> <2 bytes> 01 01`.
//
// View     Fragments
// owner    groups 1 and 2 when the state changed (a join, a change, a refusal, a command). No fragment in the first record of a character
//          with no faction, the time 0, and the count 0 (the client starts with faction 0: the slice has startingFaction 0).
// observer group 2 in the first record of a character with a faction, and after each change of the faction.

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kFactionGroups = 4;

} // namespace

std::vector<hub::Bytes> encode_faction_state(const world::Entity& entity, View view, const PartsRequest& request) {
    const world::FactionState& faction = entity.faction;
    StateWriter state(kFactionGroups);
    if (view == View::owner) {
        if (request.since == 0 && faction.faction == 0 && faction.changedNs == 0 && faction.changes == 0) {
            return {};
        }
        state.field(1, 3).u64(faction.changedNs);
        state.field(1, 4).u16(faction.changes);
        state.field(2, 0).u8(faction.faction);
        return {state.finish()};
    }
    // An observer: the public faction in its first record (none for a character with no faction), then only after a change of it (not
    // after a refused request of the owner).
    if (request.since == 0 ? faction.faction == 0 : faction.publicChanged <= request.since) {
        return {};
    }
    state.field(2, 0).u8(faction.faction);
    return {state.finish()};
}

} // namespace soulwarden::backend::server::rep

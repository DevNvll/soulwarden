#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// MB::ProgressionComponentReplicatedState (constructor at image RVA 0x3CDF390), slot 70 of the
// player slice and of the remote player slice (component Progression: level and experience).
// Two groups, each field a 32-bit number (protocol notes 4.12):
//   group 0: level (field 0; the number of the screen minus 1), bonusLevel (field 1)
//   group 1: experiencePoints (field 0; the total experience), restedExp (field 1)
// The client facet (apply at image RVA 0x3CF15D0) shows the level-up notice when it gets a
// larger level after its first one. It has no message for a level-up.
//
// The client needs the state for its bag window too: the script of an item cell compares the
// level of the player (`Hud.LocalPlayer.Progression.Level`) with the level that the item
// needs. With no level the script stops with an error (protocol notes 4.10).
//
// View     Fields
// owner    group 0: level, bonusLevel. Group 1: experiencePoints, restedExp.
// observer group 0: level, bonusLevel. The name tag of a remote player shows its level (live
//          proof). The experience of a character is not a value for a different player.

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 2;
constexpr unsigned kLevel = 0;
constexpr unsigned kBonusLevel = 1;
constexpr unsigned kExperienceGroup = 1;
constexpr unsigned kExperiencePoints = 0;
constexpr unsigned kRestedExperience = 1;

} // namespace

hub::Bytes encode_progression_state(const world::Entity& entity, View view, const world::WorldClock&) {
    const world::Progression& progression = entity.progression;
    StateWriter state(kGroups);
    state.field(0, kLevel).u32(progression.level == 0 ? 0 : progression.level - 1);
    state.field(0, kBonusLevel).u32(0);
    if (view == View::owner) {
        state.field(kExperienceGroup, kExperiencePoints).u32(progression.experience);
        state.field(kExperienceGroup, kRestedExperience).u32(0);
    }
    return state.finish();
}

} // namespace soulwarden::backend::server::rep

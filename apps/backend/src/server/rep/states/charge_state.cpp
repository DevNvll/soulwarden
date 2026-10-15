#include <algorithm>
#include <cmath>

#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// MB::ChargeComponentReplicatedState (constructor at image RVA 0x66F13D0, apply at 0x683AB80),
// slot 48 of the player slice (component ChargeComponent): the heart rune charge (stage 1.3b,
// protocol notes 4.23). One group with one field, chrgPcnt: one byte, the percent (reader
// 0x279E210). The apply function sets the charge to the maximum of the component times the
// percent times 0.01, and the HUD fills the icon of the heart rune from it (live check I6: 100
// lights the icon, 50 fills half of it).
//
// View     Fields
// owner    group 0, field 0: the whole percent of the charge (world/runes.h). No fragment in the
//          first record of a character with no charge.
// observer no fragment: the remote player slice has no charge component.

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 1;
constexpr unsigned kChargePercent = 0;

} // namespace

std::vector<hub::Bytes> encode_charge_state(const world::Entity& entity, View view, const PartsRequest& request) {
    if (view != View::owner || (request.since == 0 && entity.runeCharge <= 0.0F)) {
        return {};
    }
    StateWriter state(kGroups);
    state.field(0, kChargePercent).u8(static_cast<std::uint8_t>(std::clamp(std::floor(entity.runeCharge), 0.0F, 100.0F)));
    return {state.finish()};
}

} // namespace soulwarden::backend::server::rep

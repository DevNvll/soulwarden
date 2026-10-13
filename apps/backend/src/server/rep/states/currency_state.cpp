#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// MB::CurrencyComponentReplicatedState (constructor at image RVA 0x37FF0E0), slot 60 of the
// player slice (component Currency of the entity "Inventory"). One group with one field:
// `currency`, a 64-bit number in hundredths of a coin (client reader at image RVA 0x2A43500).
// The client facet (apply at image RVA 0x3820570) keeps the amount, not more than its wallet
// limit, and the bag window shows the amount divided by 100 with 2 decimals.
//
// View     Fields
// owner    group 0: currency.
// observer no fragment: the remote player slice has no facet for the component.

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 1;
constexpr unsigned kCurrency = 0;

} // namespace

hub::Bytes encode_currency_state(const world::Entity& entity, View view, const world::WorldClock&) {
    if (view != View::owner) {
        return {};
    }
    StateWriter state(kGroups);
    state.field(0, kCurrency).u64(entity.wallet.coin);
    return state.finish();
}

} // namespace soulwarden::backend::server::rep

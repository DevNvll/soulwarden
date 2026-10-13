#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// MB::PlayerAppearanceComponentReplicatedState (tools/st PlayerAppearance), slot 12 of the
// player slice: one group, 12 fields of one byte each (state/creation_data.h has the field
// order). Each value is an index into a character creation database of the client (facet
// handler at image RVA 0x683BA40). The client reads the appearance only when it has the gender
// and the race. Without them the body has no skin material (it is white).
//
// View     Fields
// owner    all 12 fields
// observer all 12 fields: each client makes the body of the character from them.

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 1;

} // namespace

hub::Bytes encode_appearance_state(const world::Entity& entity, View, const world::WorldClock&) {
    StateWriter writer(kGroups);
    for (unsigned index = 0; index < state::kAppearanceFieldCount; ++index) {
        writer.field(0, index).u8(entity.appearance[index]);
    }
    return writer.finish();
}

} // namespace soulwarden::backend::server::rep

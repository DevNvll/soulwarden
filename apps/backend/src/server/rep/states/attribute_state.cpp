#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"

// MB::AttributeComponentReplicatedState (constructor at image RVA 0x276C250), slot 27 of the
// player slice (component Attribute of the entity "RootPlayer"). One group with four fields
// (protocol notes 4.12):
//   0 attributes               count:size, then type:u32 value:u32 for each attribute. The
//                              value has the minimum of the attribute tables (5) in it.
//   1 attributeBonuses         replicated map: the bonus of each attribute from equipment
//   2 placingBonuses           replicated array: bonus values that go to one attribute
//   3 persistentAttributeData  unspent:u32 ignoreBackstory:u8 hasRespec:u8 count:size
//                              (type:u32 points:u32)* behavior:u32
// A replicated map or array in the full form is `00` (no change entries), a version flag
// (`00`: no version), and the count of the elements. The backend has no bonus: the two fields
// are empty. They are in each fragment: a container in the full form is a change for the
// client in each fragment, thus the facet (apply at image RVA 0x2788BE0) copies all four fields
// each time, and the values of the server replace the values that the client computed itself
// when it sent a request.
//
// View     Fields
// owner    group 0: attributes, attributeBonuses, placingBonuses, persistentAttributeData.
// observer no fragment: the remote player slice has no facet for the component.

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 1;
constexpr unsigned kAttributes = 0;
constexpr unsigned kAttributeBonuses = 1;
constexpr unsigned kPlacingBonuses = 2;
constexpr unsigned kPersistentData = 3;
/** `chosenMagnifyBehavior`: the client selects the attribute that gets the placing bonuses itself. */
constexpr std::uint32_t kAutomaticPlacing = 5;

/** An empty replicated map or array in the full form: no change entry, no version, no element. */
void empty_container(hub::Writer& writer) {
    writer.size(0);
    writer.u8(0);
    writer.size(0);
}

} // namespace

hub::Bytes encode_attribute_state(const world::Entity& entity, View view, const world::WorldClock&) {
    if (view != View::owner) {
        return {};
    }
    const world::Attributes& attributes = entity.attributes;
    StateWriter state(kGroups);
    // The client writes the values of an attribute to its screen only when the map has it:
    // all five attributes are in the map.
    hub::Writer& values = state.field(0, kAttributes);
    values.size(world::attribute::kCount);
    std::uint32_t spentTypes = 0;
    for (unsigned type = 0; type < world::attribute::kCount; ++type) {
        values.u32(type);
        values.u32(attributes.value(type));
        spentTypes += attributes.spent[type] != 0 ? 1 : 0;
    }
    empty_container(state.field(0, kAttributeBonuses));
    empty_container(state.field(0, kPlacingBonuses));
    hub::Writer& persistent = state.field(0, kPersistentData);
    persistent.u32(attributes.unspent);
    persistent.u8(0); // ignoreBackstoryPoints: the client does not read it.
    persistent.u8(0); // hasRespec: no reset without its cost.
    // The spent points: an entry only for an attribute with points.
    persistent.size(spentTypes);
    for (unsigned type = 0; type < world::attribute::kCount; ++type) {
        if (attributes.spent[type] != 0) {
            persistent.u32(type);
            persistent.u32(attributes.spent[type]);
        }
    }
    persistent.u32(kAutomaticPlacing);
    return state.finish();
}

} // namespace soulwarden::backend::server::rep

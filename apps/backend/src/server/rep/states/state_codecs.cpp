#include "server/rep/states/state_codecs.h"

#include <array>

namespace soulwarden::backend::server::rep {

std::span<const StateCodec> player_codecs() {
    // The client sorts the fragments of a record by their slot before it applies them (client
    // code at image RVA 0x15CE270). This sequence is the sequence of the stored bytes of the
    // tests.
    static constexpr std::array<StateCodec, 19> codecs{{
        {world::ComponentKey::metadata, kMetadataSlot, kTypeGdeMetadataState, encode_metadata_state},
        {world::ComponentKey::player, kPlayerComponentSlot, kTypePlayerComponentState, encode_player_state},
        {world::ComponentKey::movement, kMovementSlot, kTypeMovementState, encode_movement_state},
        {world::ComponentKey::vitals, kVitalsSlot, kTypeVitalsState, nullptr, encode_vitals_parts},
        // The mana meter of the HUD reads the mana component, not the vitals state. The remote
        // player slice has no mana component: owner only.
        {world::ComponentKey::mana, kManaSlot, kTypeManaState, encode_mana_state, nullptr, true},
        {world::ComponentKey::appearance, kAppearanceSlot, kTypeAppearanceState, encode_appearance_state},
        // The level is before the bag: it is in the first bundle of a record also when the bag
        // needs more than one bundle, and the client needs it to show an item.
        // The remote player slice has the component: an observer gets the level.
        {world::ComponentKey::progression, kProgressionSlot, kTypeProgressionState, encode_progression_state},
        // The equipment panel of the inventory screen shows the attributes: they go with the
        // level. The remote player slice has no facet for them: owner only.
        {world::ComponentKey::attribute, kAttributeSlot, kTypeAttributeState, encode_attribute_state, nullptr, true},
        // The equipment is before the bag for the same cause: the clothing of a character is in
        // the first bundle. The remote player slice has the component, and the encoder has the
        // two views.
        {world::ComponentKey::paperdoll, kPaperdollSlot, kTypePaperdollState, nullptr, encode_paperdoll_state},
        // The remote player slice has no facet for the coin and for the bag: owner only.
        {world::ComponentKey::currency, kCurrencySlot, kTypeCurrencyState, encode_currency_state, nullptr, true},
        {world::ComponentKey::container, kContainerSlot, kTypeContainerState, nullptr, encode_container_state, true},
        // The skills: owner only. The ability state is the last one: it is the largest state
        // of a new character, and the client has its own empty bars before it gets the state.
        {world::ComponentKey::categorical, kCategoricalSlot, kTypeCategoricalState, encode_categorical_state, nullptr, true},
        {world::ComponentKey::points, kPointsSlot, kTypePointsState, nullptr, encode_points_state, true},
        {world::ComponentKey::abilities, kAbilitySlot, kTypeAbilityState, nullptr, encode_ability_state, true},
        // The cooldowns of the abilities (stage 3.3a): owner only, after the bars that name the
        // abilities of the maps.
        {world::ComponentKey::cooldowns, kCooldownSlot, kTypeCooldownState, nullptr, encode_cooldown_state, true},
        // The heart rune charge (stage 1.3b): owner only.
        {world::ComponentKey::charge, kChargeSlot, kTypeChargeState, nullptr, encode_charge_state, true},
        // The status effects (stage 3.3b-S1): the owner gets its maps and the tray, an observer the public maps (the
        // remote player slice has the component at the same slot).
        {world::ComponentKey::status, kStatusSlot, kTypeStatusEffectsState, nullptr, encode_player_status_state},
        // The speed table (stage 3.3b-S2a, S2a-I10): owner only; the client runs with its multipliers.
        {world::ComponentKey::statMultipliers, kStatMultiplierSlot, kTypeStatMultiplierState, nullptr, encode_stat_multiplier_state, true},
        // The movement of a character for its observers (stage 3.3b-S4: a dash): observer view only (the encoder gives the
        // owner no fragment).
        {world::ComponentKey::motion, kMovementSlot, kTypeMovementState, encode_observed_motion_state},
    }};
    return codecs;
}

std::span<const StateCodec> creature_codecs() {
    // The slot of each component is a value of the slice of the creature (game data).
    static constexpr std::array<StateCodec, 5> codecs{{
        {world::ComponentKey::metadata, kMetadataSlot, kTypeGdeMetadataState, encode_metadata_state},
        {world::ComponentKey::movement, 0, kTypeMovementState, nullptr, encode_creature_movement_parts, false,
         [](const world::Entity& entity) { return entity.creature.body->movementSlot; }},
        {world::ComponentKey::vitals, 0, kTypeVitalsState, nullptr, encode_creature_vitals_state, false,
         [](const world::Entity& entity) { return entity.creature.body->vitalsSlot; }},
        {world::ComponentKey::marker, 0, kTypeMarkerState, encode_marker_state, nullptr, false,
         [](const world::Entity& entity) { return entity.creature.body->markerSlot; }},
        // The status effects of abilities on the creature (stage 3.3a): the icons at its name plate.
        {world::ComponentKey::status, 0, kTypeStatusEffectsState, nullptr, encode_creature_status_state, false,
         [](const world::Entity& entity) { return entity.creature.body->statusSlot; }},
    }};
    return codecs;
}

std::span<const StateCodec> spell_codecs() {
    // The slot of the Spell state and of each delayed event is a value of the slice of the spell (game data).
    static constexpr std::array<StateCodec, 3> codecs{{
        {world::ComponentKey::metadata, kMetadataSlot, kTypeGdeMetadataState, encode_metadata_state},
        {world::ComponentKey::spell, 0, kTypeSpellState, encode_spell_state, nullptr, false,
         [](const world::Entity& entity) { return entity.spell.slice != nullptr ? entity.spell.slice->spellSlot : 0U; }},
        {world::ComponentKey::delayedEvents, 0, kTypeDelayedEventState, nullptr, nullptr, false, nullptr, encode_delayed_event_states},
    }};
    return codecs;
}

std::span<const StateCodec> probe_codecs() {
    // Development aid: the metadata state of the slice of the probe; its other states are the fragments of the command.
    static constexpr std::array<StateCodec, 1> codecs{{
        {world::ComponentKey::metadata, kMetadataSlot, kTypeGdeMetadataState, encode_metadata_state},
    }};
    return codecs;
}

std::span<const StateCodec> codecs_of(const world::Entity& entity) {
    switch (entity.kind) {
    case world::EntityKind::creature:
        return creature_codecs();
    case world::EntityKind::probe:
        return probe_codecs();
    case world::EntityKind::spell:
        return spell_codecs();
    case world::EntityKind::player:
        break;
    }
    return player_codecs();
}

std::uint32_t slot_of(const StateCodec& codec, const world::Entity& entity) {
    return codec.slotOf != nullptr ? codec.slotOf(entity) : codec.slot;
}

} // namespace soulwarden::backend::server::rep

#include "server/rep/states/state_codecs.h"

#include <array>

namespace soulwarden::backend::server::rep {

std::span<const StateCodec> player_codecs() {
    // The client sorts the fragments of a record by their slot before it applies them (client
    // code at image RVA 0x15CE270). This sequence is the sequence of the stored bytes of the
    // tests.
    static constexpr std::array<StateCodec, 34> codecs{{
        {world::ComponentKey::metadata, kMetadataSlot, kTypeGdeMetadataState, encode_metadata_state},
        {world::ComponentKey::player, kPlayerComponentSlot, kTypePlayerComponentState, encode_player_state},
        {world::ComponentKey::movement, kMovementSlot, kTypeMovementState, encode_movement_state},
        {world::ComponentKey::vitals, kVitalsSlot, kTypeVitalsState, nullptr, encode_vitals_parts},
        // The mana meter of the HUD reads the mana component, not the vitals state. The remote
        // player slice has no mana component: owner only.
        {world::ComponentKey::mana, kManaSlot, kTypeManaState, encode_mana_state, nullptr, true},
        {world::ComponentKey::appearance, kAppearanceSlot, kTypeAppearanceState, encode_appearance_state},
        // The ownership of the DLC (stage 1.1b): owner only, before the level. The client reads the expansion of its
        // character (the achievement) for the limit of the level and for the mastery trees.
        {world::ComponentKey::entitlements, kEntitlementSlot, kTypeEntitlementState, encode_entitlement_state, nullptr, true},
        {world::ComponentKey::achievements, kAchievementSlot, kTypeAchievementState, encode_achievement_state, nullptr, true},
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
        // The stamina (stage 3.4f-B): owner only; the client predicts its own value (live checks LS2, LS3).
        {world::ComponentKey::stamina, kStaminaSlot, kTypeStaminaState, nullptr, encode_stamina_state, true},
        // The movement of a character for its observers (stage 3.3b-S4: a dash): observer view only (the encoder gives the
        // owner no fragment).
        {world::ComponentKey::motion, kMovementSlot, kTypeMovementState, encode_observed_motion_state},
        // The committed interaction (stage 2.5a): the owner and each observer (the remote player slice has slot 26 too).
        {world::ComponentKey::interactor, kInteractorSlot, kTypeInteractorState, nullptr, encode_interactor_parts},
        // The open of a chest (stage 3.5b.1): owner only (the remote player slice has neither component).
        {world::ComponentKey::lootTracker, kLootTrackerSlot, kTypeLootTrackerState, nullptr, encode_loot_tracker_state, true},
        {world::ComponentKey::gathering, kGatheringSlot, kTypeGatheringState, nullptr, encode_gathering_state, true},
        // The loot bags (stage 3.5b.2): owner only (the remote player slice has no storage component).
        {world::ComponentKey::selectedContainer, kSelectedContainerSlot, kTypeContainerState, nullptr, encode_selected_container_state, true},
        {world::ComponentKey::globalStorage, kGlobalStorageSlot, kTypeGlobalStorageState, nullptr, encode_global_storage_state, true},
        // The cooldowns of the recipes (stage 3.7b): owner only (the remote player slice has no Crafting component).
        {world::ComponentKey::crafting, kCraftingSlot, kTypeCraftingState, nullptr, encode_crafting_state, true},
        // The quests (stage 1.8a): owner only, and only after the first 2709 of the session (live check L1: the readiness of slot 73
        // and the tracked list of slot 22 pin the objective on the HUD). In one record the client applies slot 22 before slot 66 (it
        // sorts by slot): live check L2 shows that the task change and a bit of the same change work in that order.
        {world::ComponentKey::seasons, kSeasonsSlot, kTypeSeasonsTrackedStatState, encode_seasons_state, nullptr, true, nullptr, nullptr, true},
        {world::ComponentKey::objectives, kObjectivesSlot, kTypeObjectivesState, encode_objectives_state, nullptr, true, nullptr, nullptr, true},
        // The faction (stage QG7): the owner gets its time and count of the last change, each observer the faction (the remote player
        // slice has the component at the same slot). No fragment in the first record of a character with no faction.
        {world::ComponentKey::faction, kFactionSlot, kTypeFactionState, nullptr, encode_faction_state},
        // The empty Social state (stage QG7): owner only, after the first 2709 (the friend list of the client: the faction poster of a
        // conversation reads it).
        {world::ComponentKey::social, kSocialSlot, kTypeSocialState, encode_social_state, nullptr, true, nullptr, nullptr, true},
        // The provider state of a mission board (stage QG6a): owner only, after the first open of a board in this world entry.
        {world::ComponentKey::objectiveInteractor, kObjectiveInteractorSlot, kTypeObjectiveInteractorState, encode_objective_interactor_state,
         nullptr, true},
        // The daily bonuses of the faction missions (stage QG6b): owner only, when the character has a use.
        {world::ComponentKey::gameEvent, kGameEventSlot, kTypeGameEventState, encode_game_event_state, nullptr, true},
    }};
    return codecs;
}

std::span<const StateCodec> creature_codecs() {
    // The slot of each component is a value of the slice of the creature (game data).
    static constexpr std::array<StateCodec, 7> codecs{{
        {world::ComponentKey::metadata, kMetadataSlot, kTypeGdeMetadataState, encode_metadata_state},
        {world::ComponentKey::movement, 0, kTypeMovementState, nullptr, encode_creature_movement_parts, false,
         [](const world::Entity& entity) { return entity.creature.body->movementSlot; }},
        // A stationary target (stage QG5c1: a totem, a nest): its place, in the place of a movement state (slot 0 for a creature).
        {world::ComponentKey::targetPlace, 0, kTypePositionInTheWorldState, encode_target_place_state, nullptr, false,
         [](const world::Entity& entity) { return entity.creature.body->placeSlot; }},
        {world::ComponentKey::vitals, 0, kTypeVitalsState, nullptr, encode_creature_vitals_state, false,
         [](const world::Entity& entity) { return entity.creature.body->vitalsSlot; }},
        {world::ComponentKey::marker, 0, kTypeMarkerState, encode_marker_state, nullptr, false,
         [](const world::Entity& entity) { return entity.creature.body->markerSlot; }},
        // The status effects of abilities on the creature (stage 3.3a): the icons at its name plate.
        {world::ComponentKey::status, 0, kTypeStatusEffectsState, nullptr, encode_creature_status_state, false,
         [](const world::Entity& entity) { return entity.creature.body->statusSlot; }},
        // The skinning of the body (stage 3.6a): the GatherableController state of a dead creature with a skinning row (slot 0: none).
        {world::ComponentKey::gatherable, 0, kTypeGatherableState, encode_gatherable_state, nullptr, false,
         [](const world::Entity& entity) { return entity.creature.skinSlot; }},
    }};
    return codecs;
}

std::span<const StateCodec> spell_codecs() {
    // The slot of the Spell state and of each delayed event is a value of the slice of the spell (game data).
    // Stage 3.3b-S2b: the PositionInTheWorld state of a slice with no Spell component (the trap), and the vitals state of a summon
    // or a trap.
    static constexpr std::array<StateCodec, 5> codecs{{
        {world::ComponentKey::metadata, kMetadataSlot, kTypeGdeMetadataState, encode_metadata_state},
        {world::ComponentKey::spell, 0, kTypeSpellState, encode_spell_state, nullptr, false,
         [](const world::Entity& entity) { return entity.spell.slice != nullptr ? entity.spell.slice->spellSlot : 0U; }},
        {world::ComponentKey::spellPlace, 0, kTypePositionInTheWorldState, encode_spell_place_state, nullptr, false,
         [](const world::Entity& entity) { return entity.spell.slice != nullptr ? entity.spell.slice->positionSlot : 0U; }},
        {world::ComponentKey::vitals, 0, kTypeVitalsState, nullptr, encode_spell_vitals_parts, false,
         [](const world::Entity& entity) {
             return entity.spell.slice != nullptr && entity.spell.slice->vitals && entity.spell.vitalsRow != nullptr ? entity.spell.slice->vitals->slot
                                                                                                                       : 0U;
         }},
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

std::uint32_t object_place_slot(const world::Entity& entity) {
    return entity.object.positionSlot;
}

std::uint32_t object_buildable_slot(const world::Entity& entity) {
    return entity.object.buildableSlot;
}

std::uint32_t object_ownership_slot(const world::Entity& entity) {
    return entity.object.ownershipSlot;
}

std::uint32_t object_loot_drop_slot(const world::Entity& entity) {
    return entity.object.lootDropSlot;
}

std::uint32_t object_node_slot(const world::Entity& entity) {
    return entity.object.nodeSlot;
}

std::uint32_t object_variation_slot(const world::Entity& entity) {
    return entity.object.variationSlot;
}

std::uint32_t object_territory_slot(const world::Entity& entity) {
    return entity.object.territorySlot;
}

std::uint32_t object_land_claim_slot(const world::Entity& entity) {
    return entity.object.world ? kLandClaimManagerSlot : 0;
}

std::span<const StateCodec> object_codecs() {
    // The slot of each state is a value of the slice of the object (game data): one Interact fragment for each bound. A level object
    // has no place state, no BuildableController state, and no Ownership state (slot 0): the client has them from its level data.
    static constexpr std::array<StateCodec, 10> codecs{{
        {world::ComponentKey::metadata, kMetadataSlot, kTypeGdeMetadataState, encode_metadata_state},
        // The LandClaimManager state of the world entity (stage QG6a): the claims of the towns (slot 2 of slices/globalgde; 0: not it).
        {world::ComponentKey::landClaims, 0, kTypeLandClaimManagerState, encode_land_claim_state, nullptr, false, object_land_claim_slot},
        // The TerritoryInterface state of a mission board (stage QG6a): the projects of its town (the slot of its slice; 0: not a board).
        {world::ComponentKey::territoryInterface, 0, kTypeTerritoryInterfaceState, encode_territory_interface_state, nullptr, false,
         object_territory_slot},
        // The variant of a start object of an encounter (stage QG5c2): the slot of its VariationData component (0: none).
        {world::ComponentKey::variation, 0, kTypeVariationState, encode_variation_state, nullptr, false, object_variation_slot},
        {world::ComponentKey::buildable, 0, kTypeBuildableState, encode_buildable_state, nullptr, false, object_buildable_slot},
        // The node state of a gathering node (stage 3.6a): Healthy or Depleted at the slot of its GatherableController (0: none).
        {world::ComponentKey::gatherable, 0, kTypeGatherableState, encode_gatherable_state, nullptr, false, object_node_slot},
        // The LootDrop state of a loot bag (stage 3.5b.2): the rarity (the slot of its slice; 0: not a bag).
        {world::ComponentKey::lootDrop, 0, kTypeLootDropState, encode_loot_drop_state, nullptr, false, object_loot_drop_slot},
        {world::ComponentKey::objectPlace, 0, kTypePositionInTheWorldState, encode_object_place_state, nullptr, false, object_place_slot},
        {world::ComponentKey::ownership, 0, kTypeOwnershipState, encode_ownership_state, nullptr, false, object_ownership_slot},
        {world::ComponentKey::interact, 0, kTypeInteractState, nullptr, nullptr, false, nullptr, encode_interact_states},
    }};
    return codecs;
}

std::span<const StateCodec> projectile_codecs() {
    // The slot of the Projectile state is a value of the projectile slice (game data).
    static constexpr std::array<StateCodec, 2> codecs{{
        {world::ComponentKey::metadata, kMetadataSlot, kTypeGdeMetadataState, encode_metadata_state},
        {world::ComponentKey::projectile, 0, kTypeProjectileState, nullptr, encode_projectile_parts, false, projectile_slot},
    }};
    return codecs;
}

std::span<const StateCodec> codecs_of(const world::Entity& entity) {
    switch (entity.kind) {
    case world::EntityKind::projectile:
        return projectile_codecs();
    case world::EntityKind::object:
        return object_codecs();
    case world::EntityKind::creature:
        return creature_codecs();
    case world::EntityKind::probe:
        return probe_codecs();
    case world::EntityKind::spell:
        return spell_codecs();
    case world::EntityKind::encounter:
        return encounter_codecs();
    case world::EntityKind::player:
        break;
    }
    return player_codecs();
}

std::uint32_t slot_of(const StateCodec& codec, const world::Entity& entity) {
    return codec.slotOf != nullptr ? codec.slotOf(entity) : codec.slot;
}

} // namespace soulwarden::backend::server::rep

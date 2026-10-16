#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "server/hub/wire.h"
#include "world/entity.h"
#include "world/world_clock.h"

// One encoder for each component state of an entity. An encoder makes the wire form of the
// state from the entity of the world, for one of two views (roadmap, rule MP-3):
//   owner     the session whose client plays the character
//   observer  each other session that observes the entity
// No other code writes the bytes of a state. The fields of each view are in the file of the
// state and in the protocol notes (4.9, 4.10 for the items and the coin, 4.11 for the
// equipment, 4.12 for the level and the attributes, 4.13 for the skills, and 4.14 for a
// creature).
//
// A state that only the owner gets (the remote player slice has no facet for its component)
// has the mark `ownerOnly`: a session sends no fragment of it for the observer view.
//
// A creature has its own codec list (creature_codecs): it has only the observer view, and the
// slot of each of its states is a value of its slice (game data).
//
// The slots are the slots of slices/player.dynamicslice. slices/remoteplayer.dynamicslice,
// which the client uses for the player of a different client, has the same slot for each
// component that it keeps (protocol notes 4.4).

namespace soulwarden::backend::server::rep {

/** Type indexes of the state types (typeindex.json of the supported client build). */
inline constexpr std::uint32_t kTypeGdeMetadataState = 10;     // 203dc8c7-0c60-454b-a46f-566114314b84
inline constexpr std::uint32_t kTypePlayerComponentState = 3935; // MB::PlayerComponentReplicatedState
inline constexpr std::uint32_t kTypeMovementState = 11;         // MB::ALCReplicatedState
inline constexpr std::uint32_t kTypeVitalsState = 15;           // MB::VitalsComponentReplicatedState
inline constexpr std::uint32_t kTypeAppearanceState = 1195;     // MB::PlayerAppearanceComponentReplicatedState
inline constexpr std::uint32_t kTypeProgressionState = 899;     // MB::ProgressionComponentReplicatedState
inline constexpr std::uint32_t kTypeCurrencyState = 3786;       // MB::CurrencyComponentReplicatedState
inline constexpr std::uint32_t kTypeContainerState = 1755;      // MB::ContainerComponentReplicatedState
inline constexpr std::uint32_t kTypePaperdollState = 3183;      // MB::PaperdollComponentReplicatedState
inline constexpr std::uint32_t kTypeAttributeState = 129;       // MB::AttributeComponentReplicatedState
inline constexpr std::uint32_t kTypeCategoricalState = 911;     // MB::CategoricalProgressionReplicatedState
inline constexpr std::uint32_t kTypePointsState = 3681;         // MB::ProgressionPointReplicatedState
inline constexpr std::uint32_t kTypeAbilityState = 185;         // MB::AbilityComponentReplicatedState
inline constexpr std::uint32_t kTypeMarkerState = 5399;         // MB::MarkerComponentReplicatedState
inline constexpr std::uint32_t kTypeManaState = 1652;           // MB::ManaComponentReplicatedState
inline constexpr std::uint32_t kTypeCooldownState = 2932;       // MB::CooldownTimersComponentReplicatedState
inline constexpr std::uint32_t kTypeChargeState = 5257;         // MB::ChargeComponentReplicatedState
inline constexpr std::uint32_t kTypeStatusEffectsState = 4236;  // MB::StatusEffectsComponentReplicatedState
inline constexpr std::uint32_t kTypeStatMultiplierState = 1525; // MB::StatMultiplierTableReplicatedState
inline constexpr std::uint32_t kTypeSpellState = 2912;          // MB::SpellComponentReplicatedState
inline constexpr std::uint32_t kTypeDelayedEventState = 3139;   // MB::DelayedEventComponentReplicatedState
inline constexpr std::uint32_t kTypePositionInTheWorldState = 13; // MB::PositionInTheWorldReplicatedState (stage 3.3b-S2b)
inline constexpr std::uint32_t kTypeEntitlementState = 3133;    // MB::EntitlementComponentReplicatedState (stage 1.1b)
inline constexpr std::uint32_t kTypeAchievementState = 3210;    // MB::AchievementComponentReplicatedState (stage 1.1b)
inline constexpr std::uint32_t kTypeInteractorState = 3752;     // MB::InteractorComponentReplicatedState (stage 2.5a)
inline constexpr std::uint32_t kTypeInteractState = 2930;       // MB::InteractReplicatedState (stage 2.5a)
inline constexpr std::uint32_t kTypeBuildableState = 1594;      // MB::BuildableControllerReplicatedState (stage 2.5b)
inline constexpr std::uint32_t kTypeOwnershipState = 3217;      // MB::OwnershipComponentReplicatedState (stage 2.5b)
inline constexpr std::uint32_t kTypeProjectileState = 16;       // MB::ProjectileReplicatedState (stage 3.4f-A)
inline constexpr std::uint32_t kTypeStaminaState = 4297;        // MB::StaminaComponentReplicatedState (stage 3.4f-B)

/** Slot of the metadata state in each slice. */
inline constexpr std::uint32_t kMetadataSlot = 0;
/** Slots of the components in the component list of slices/player.dynamicslice. */
inline constexpr std::uint32_t kPlayerComponentSlot = 9;
inline constexpr std::uint32_t kAppearanceSlot = 12;
inline constexpr std::uint32_t kMovementSlot = 16;
inline constexpr std::uint32_t kVitalsSlot = 41;
/** The component Mana of the entity "Stats" of the player slice: the mana meter of the HUD reads it. */
inline constexpr std::uint32_t kManaSlot = 47;
/** The components Currency, Container (the bag), and Paperdoll (the equipment) of the entity "Inventory" of the player slice. */
inline constexpr std::uint32_t kCurrencySlot = 60;
inline constexpr std::uint32_t kContainerSlot = 61;
/** The remote player slice has the paperdoll component at the same slot. */
inline constexpr std::uint32_t kPaperdollSlot = 62;
/**
 * The component Progression (level and experience) of the entity "GameEvents" of the player
 * slice. The remote player slice has it at the same slot.
 */
inline constexpr std::uint32_t kProgressionSlot = 70;
/** The component Attribute of the entity "RootPlayer" of the player slice. */
inline constexpr std::uint32_t kAttributeSlot = 27;
/** The components CategoricalProgression and ProgressionPoint of the entity "GameEvents", and Ability of the entity "Stats". */
inline constexpr std::uint32_t kCategoricalSlot = 65;
inline constexpr std::uint32_t kPointsSlot = 67;
inline constexpr std::uint32_t kAbilitySlot = 46;
/** The component CooldownTimers of the entity "Stats" of the player slice (stage 3.3a). */
inline constexpr std::uint32_t kCooldownSlot = 45;
/** The component ChargeComponent of the player slice: the heart rune charge (stage 1.3b). */
inline constexpr std::uint32_t kChargeSlot = 48;
/** The component StatusEffects of the player slice and of the remote player slice (stage 3.3b-S1, research abilities-shared.md Q1). */
inline constexpr std::uint32_t kStatusSlot = 44;
/** The component Stamina of the player slice (stage 3.4f-B; research abilities-melee.md, creature-ranged.md task D). */
inline constexpr std::uint32_t kStaminaSlot = 42;
/** The component StatMultiplierTable of the player slice (stage 3.3b-S2a, S2a-I10; research status-speed.md): the speed table. */
inline constexpr std::uint32_t kStatMultiplierSlot = 43;
/** The components Entitlement and Achievement of the player slice (stage 1.1b; research dlc-entitlements.md, Q2). */
inline constexpr std::uint32_t kEntitlementSlot = 19;
inline constexpr std::uint32_t kAchievementSlot = 66;
/** The component Interactor of the entity RootPlayer of the player slice and of the remote player slice (stage 2.5a). */
inline constexpr std::uint32_t kInteractorSlot = 26;

enum class View {
    owner,
    observer,
};

using StateEncoder = server::hub::Bytes (*)(const world::Entity& entity, View view, const world::WorldClock& clock);

/** What an encoder of a state with more than one fragment gets from the session. */
struct PartsRequest {
    /**
     * World revision of the last record of the session for the entity. 0: the session has no
     * record of the entity, and the client has nothing of the state.
     */
    world::Revision since{0};
    /** Largest number of bytes of one fragment state, so that one bundle stays in one carrier message. */
    std::size_t budget{0};
    /** The clock of the world, for a state with a time. Null: a caller with no clock (an encoder that needs none). */
    const world::WorldClock* clock{nullptr};
    /** The key of the scope fields of a creature that the session sent last (Known::scope, creature_scope). */
    std::uint32_t scope{0xFFFFFFFFU};
    /**
     * True: the movement state of a creature has no scope fields, also for a different action:
     * the client of the session plays the spawn action of a first record, and the action that
     * plays follows at the end of the lead of that spawn action (rep_session.cpp, kSpawnLead).
     */
    bool keepScope{false};
    /**
     * The health of the last vitals state of the owner that the session sent (code review of stage 3.3b-S6, finding S6-R7): the health
     * that the client of the owner has. Below 0: not known.
     */
    float ownerHealth{-1.0F};
};

/**
 * Encoder of a state that can need more than one fragment: a state with a list that is larger
 * than one carrier message. The session sends each part in its own record, in this sequence.
 */
using PartsEncoder = std::vector<server::hub::Bytes> (*)(const world::Entity& entity, View view, const PartsRequest& request);

/** One fragment of a state for a slot of the slice of the entity (a spell has one delayed event state for each event slot). */
struct SlotState {
    std::uint32_t slot{0};
    hub::Bytes state;
};

/** Encoder of a state with one fragment for each of some slots of the slice (the delayed events of a spell). */
using SlotPartsEncoder = std::vector<SlotState> (*)(const world::Entity& entity, View view, const PartsRequest& request);

struct StateCodec {
    world::ComponentKey key;
    /** Index of the component in the component list of the slice (`m_replicationIndex`). */
    std::uint32_t slot;
    std::uint32_t type;
    /** Encoder of a state with one fragment. Null when `parts` is set. */
    StateEncoder encode;
    PartsEncoder parts{nullptr};
    /** True: only the session of the owner gets the state. */
    bool ownerOnly{false};
    /**
     * For a state of a creature: the slot of the component in the slice of the creature, which
     * is game data (0: the slice has no such component, and the state has no fragment). Null:
     * the slot is `slot`.
     */
    std::uint32_t (*slotOf)(const world::Entity& entity){nullptr};
    /** Encoder of a state with a fragment for each of some slots (the slot of each fragment is its own). Null: not such a state. */
    SlotPartsEncoder slotParts{nullptr};
};

/** @return The codecs of a player entity, in the sequence of the fragments of its record. */
[[nodiscard]] std::span<const StateCodec> player_codecs();
/**
 * @return The codecs of a creature, in the sequence of the fragments of its record. A creature
 *         has only the observer view.
 */
[[nodiscard]] std::span<const StateCodec> creature_codecs();
/**
 * @return The codecs of a spell entity (stage 3.3b-S2a), in the sequence of the fragments of its record: the metadata
 *         state (the slice of the spell), the Spell state, and the delayed event states. A spell has only the observer
 *         view, and the slots come from its slice (game data).
 */
[[nodiscard]] std::span<const StateCodec> spell_codecs();
/** @return The codecs of a probe entity (development aid, tools/world probe): only the metadata state. */
[[nodiscard]] std::span<const StateCodec> probe_codecs();
/**
 * @return The codecs of an object entity (stage 2.5a): the metadata state (the slice and the managed actor id of the placement)
 *         and the Interact state of each bound (the slots of the data). Only the observer view. A server child (stage 2.5b) also has
 *         the PositionInTheWorld state, the BuildableController state, and the Ownership state at the slots of its slice (game data;
 *         slot 0: none).
 */
[[nodiscard]] std::span<const StateCodec> object_codecs();
/**
 * @return The codecs of a projectile entity of a creature (stage 3.4f-A): the metadata state (the projectile slice) and the Projectile
 *         state at the slot of the Projectile component of the slice (game data). Only the observer view.
 */
[[nodiscard]] std::span<const StateCodec> projectile_codecs();
/** @return The codecs of an entity of each kind. */
[[nodiscard]] std::span<const StateCodec> codecs_of(const world::Entity& entity);
/** @return The slot of the state of a codec for an entity. 0 for a state that is not the metadata state: no fragment. */
[[nodiscard]] std::uint32_t slot_of(const StateCodec& codec, const world::Entity& entity);

[[nodiscard]] hub::Bytes encode_metadata_state(const world::Entity& entity, View view, const world::WorldClock& clock);
/**
 * @return The Projectile state of a projectile entity (states/projectile_state.cpp, type 16, stage 3.4f-A): for a session with no record
 *         the launch (and the collision when the flight ended), for a session that has the launch the collision at the end.
 */
[[nodiscard]] std::vector<hub::Bytes> encode_projectile_parts(const world::Entity& entity, View view, const PartsRequest& request);
/** @return The slot of the Projectile component of the slice of a projectile entity (game data). 0: none. */
[[nodiscard]] std::uint32_t projectile_slot(const world::Entity& entity);
/**
 * @return The PositionInTheWorld state (type 13; states/object_states.cpp): the place and the rotation. One encoder for each entity
 *         with such a state: a server child (stage 2.5b) and a spell with no Spell component (stage 3.3b-S2b).
 */
[[nodiscard]] hub::Bytes position_in_the_world_state(const state::WorldPosition& position, const std::array<float, 4>& rotation);
/**
 * Writes a rotation of the place states (PositionInTheWorld field 1, the Spell state fields 2 and 4: reader 0x5CEA140 -> 0x87B250): one
 * header byte (bits 0 to 2: x, y, z are 0; bits 3 to 5: x, y, z are 1), then a u16 for each other component of x, y, z; w not negative.
 * The identity is `07`.
 */
void write_place_rotation(hub::Writer& writer, std::array<float, 4> rotation);
/** @return The rotation (x, y, z, w) of a yaw about the vertical axis (degrees, 0: north, the direction of a creature). */
[[nodiscard]] std::array<float, 4> yaw_rotation(float yawDegrees);
/** @return The states of a server child (stage 2.5b, states/object_states.cpp): its place, its BuildableController, its Ownership. */
[[nodiscard]] hub::Bytes encode_object_place_state(const world::Entity& entity, View view, const world::WorldClock& clock);
[[nodiscard]] hub::Bytes encode_buildable_state(const world::Entity& entity, View view, const world::WorldClock& clock);
[[nodiscard]] hub::Bytes encode_ownership_state(const world::Entity& entity, View view, const world::WorldClock& clock);
[[nodiscard]] hub::Bytes encode_player_state(const world::Entity& entity, View view, const world::WorldClock& clock);
[[nodiscard]] hub::Bytes encode_movement_state(const world::Entity& entity, View view, const world::WorldClock& clock);
/**
 * @return The movement state of a character for an observer (stage 3.3b-S4, ComponentKey::motion: a dash): the observer view
 *         of encode_movement_state. The owner gets no fragment (its client moves its character itself).
 */
[[nodiscard]] hub::Bytes encode_observed_motion_state(const world::Entity& entity, View view, const world::WorldClock& clock);
[[nodiscard]] hub::Bytes encode_vitals_state(const world::Entity& entity, View view, const world::WorldClock& clock);
/**
 * @return The vitals state of a character (states/vitals_state.cpp), in one fragment. The first
 *         state of a session for a character that lives is the state of encode_vitals_state.
 *         Each later state, and the first state for a dead character, also has the life data
 *         (the dead mark) and the mark "the health changed".
 */
[[nodiscard]] std::vector<hub::Bytes> encode_vitals_parts(const world::Entity& entity, View view, const PartsRequest& request);
[[nodiscard]] hub::Bytes encode_appearance_state(const world::Entity& entity, View view, const world::WorldClock& clock);
/**
 * @return The level of the character (states/progression_state.cpp): the level and the
 *         experience for the owner, and only the level for an observer.
 */
[[nodiscard]] hub::Bytes encode_progression_state(const world::Entity& entity, View view, const world::WorldClock& clock);
/** @return The attributes of the character for the owner (states/attribute_state.cpp). No byte for the observer view. */
[[nodiscard]] hub::Bytes encode_attribute_state(const world::Entity& entity, View view, const world::WorldClock& clock);
/**
 * The three states of the skills of a character (states/skill_states.cpp), for the owner only:
 * the rank and the experience of each weapon mastery and trade skill, the point pools and the
 * nodes of the weapon trees, and the ability bar of each weapon.
 */
[[nodiscard]] hub::Bytes encode_categorical_state(const world::Entity& entity, View view, const world::WorldClock& clock);
[[nodiscard]] std::vector<hub::Bytes> encode_points_state(const world::Entity& entity, View view, const PartsRequest& request);
[[nodiscard]] std::vector<hub::Bytes> encode_ability_state(const world::Entity& entity, View view, const PartsRequest& request);

/**
 * @return The state of slot 65 for these skills.
 * @param sequence The sequence number of each list of the state.
 */
[[nodiscard]] hub::Bytes encode_categorical(const world::SkillSet& skills, std::uint64_t sequence);
/**
 * @return The state of slot 67 for these skills: each pool and each node, in one state of each
 *         size. The client replaces its lists with the lists of a state: the backend does not
 *         cut this state into fragments.
 * @param since World revision of the last record of the session: a node that the character
 *        lost after it goes out with the level 0. 0: the client has no node.
 */
[[nodiscard]] hub::Bytes encode_points(const world::SkillSet& skills, std::uint64_t sequence, world::Revision since);
/**
 * @return The state of slot 46 for these skills: one element for each ability table of the client.
 * @param placeNumbers True: each place has its place number (0, 1, 2) as its button number,
 *        not the input number of the client. The client shows the same bar. The backend uses
 *        this form only to make a state that is different from the usual one (after a bar
 *        request that it refused).
 */
[[nodiscard]] hub::Bytes encode_ability_bars(const world::SkillSet& skills, bool placeNumbers = false);
/**
 * @return The cooldowns of the abilities of a character for the owner (states/cooldown_state.cpp):
 *         the three maps of the places of the bar. No fragment for the observer view, and none in
 *         the first record of a character with no cooldown.
 */
[[nodiscard]] std::vector<hub::Bytes> encode_cooldown_state(const world::Entity& entity, View view, const PartsRequest& request);
/** The heart rune charge of a character (slot 48, stage 1.3b). */
[[nodiscard]] std::vector<hub::Bytes> encode_charge_state(const world::Entity& entity, View view, const PartsRequest& request);
/**
 * @return The status effects of a creature for an observer (states/status_state.cpp): the compact
 *         map with the effects that show at the name plate. No fragment in the first record of a
 *         creature with no such effect.
 */
[[nodiscard]] std::vector<hub::Bytes> encode_creature_status_state(const world::Entity& entity, View view, const PartsRequest& request);
/**
 * @return The status effects of a character (states/status_state.cpp, stage 3.3b-S1): for the owner the full map, an
 *         empty lightweight map, and the tray; for an observer the full map and the compact map of the public statuses.
 *         No fragment in the first record of a character with no such status. Needs `request.clock` (the wall ends).
 */
[[nodiscard]] std::vector<hub::Bytes> encode_player_status_state(const world::Entity& entity, View view, const PartsRequest& request);
/**
 * @return The speed table of a character for the owner (states/stat_multiplier_state.cpp, stage 3.3b-S2a): the multipliers
 *         of the run and of the sprint. No fragment for the observer view, and none in the first record of a character with
 *         the multipliers 1.
 */
[[nodiscard]] std::vector<hub::Bytes> encode_stat_multiplier_state(const world::Entity& entity, View view, const PartsRequest& request);
/**
 * @return The Spell state of a spell entity (states/spell_state.cpp, type 2912): the key of the spell row, the place, the
 *         rotation, the attachments (none), the caster, the start time, the count, and the scale of the radius.
 */
[[nodiscard]] hub::Bytes encode_spell_state(const world::Entity& entity, View view, const world::WorldClock& clock);
/**
 * @return The delayed event states of a spell entity (states/spell_state.cpp, type 3139): one fragment for each delayed event
 *         of its slice, with the time that is left at the time of the record (`request.clock`) and the completed mark.
 */
[[nodiscard]] std::vector<SlotState> encode_delayed_event_states(const world::Entity& entity, View view, const PartsRequest& request);
/**
 * @return The PositionInTheWorld state of a spell entity whose slice has no Spell component (states/spell_state.cpp, type 13; stage
 *         3.3b-S2b: the trap of the musket): its place and the identity rotation.
 */
[[nodiscard]] hub::Bytes encode_spell_place_state(const world::Entity& entity, View view, const world::WorldClock& clock);
/**
 * @return The vitals state of a summon or a trap (states/spell_state.cpp, stage 3.3b-S2b): the first state with the maximum and the
 *         vitals id of its row, a later one with the health and the dead mark.
 */
[[nodiscard]] std::vector<hub::Bytes> encode_spell_vitals_parts(const world::Entity& entity, View view, const PartsRequest& request);
/**
 * @return The entitlements of the account of a character for the owner (states/ownership_states.cpp, stage 1.1b): the bit
 *         vector, an empty balance map, and the mark "received". No fragment for an observer or for an entity with none.
 */
[[nodiscard]] hub::Bytes encode_entitlement_state(const world::Entity& entity, View view, const world::WorldClock& clock);
/** @return The achievements of a character for the owner (stage 1.1b): the bit vector. No fragment for an observer or with none. */
[[nodiscard]] hub::Bytes encode_achievement_state(const world::Entity& entity, View view, const world::WorldClock& clock);
/** @return The coin of the character for the owner. No byte for the observer view. */
[[nodiscard]] hub::Bytes encode_currency_state(const world::Entity& entity, View view, const world::WorldClock& clock);
/**
 * @return The Stamina state of a character for the owner (states/stamina_state.cpp, stage 3.4f-B): the stamina, the maximum, the winded
 *         time and the delay that are left, and the two multipliers. No fragment for an observer, and none in the first record of a full
 *         stamina.
 */
[[nodiscard]] std::vector<hub::Bytes> encode_stamina_state(const world::Entity& entity, View view, const PartsRequest& request);
/** @return The mana state of a character (states/mana_state.cpp): owner only. */
[[nodiscard]] hub::Bytes encode_mana_state(const world::Entity& entity, View view, const world::WorldClock& clock);
/**
 * @return The fragments of the bag for the owner (states/container_state.cpp): the full vector
 *         for a session that has no record of the entity, and after that only the slots that
 *         changed. No fragment for the observer view.
 */
[[nodiscard]] std::vector<hub::Bytes> encode_container_state(const world::Entity& entity, View view, const PartsRequest& request);

/**
 * @return The fragments of the equipment (states/paperdoll_state.cpp): for a session with no
 *         record of the entity each slot that has an item, and after that only the slots that
 *         changed. The owner gets each item. An observer gets the look of the items that show
 *         on the body, and no fragment when the entity has no such item.
 */
[[nodiscard]] std::vector<hub::Bytes> encode_paperdoll_state(const world::Entity& entity, View view, const PartsRequest& request);

/** How the encoder writes one item. */
struct ItemWire {
    /**
     * True: an item has the appearance id and the skin dye bytes at its end (8 bytes). The
     * client reads them when its setting `javelin.enable-transmog` is true, which is the
     * default of the supported build (live read: protocol notes 4.10).
     */
    bool transmog{true};
    /**
     * The cause of the empty element. The start value of a slot for a container. The
     * paperdoll uses "unequipped": the client does the entries with that cause first.
     */
    std::uint8_t emptyAction{world::sync::kLoad};
};

/**
 * @return The wire form of one element of a container: the item of a slot, or the empty
 *         element when `item` is null or has no index (states/container_state.cpp).
 * @param slot Number of the slot in its container (0 to 500), or 501 for an item that is in
 *        no container (an item of the paperdoll).
 * @param action The cause of the change (world/item.h, namespace sync).
 */
[[nodiscard]] hub::Bytes encode_item(const world::ItemInstance* item,
                                     std::uint32_t slot,
                                     std::uint8_t action,
                                     ItemWire wire = {});

/**
 * @return The look of an item for the client of an observer: the item index with no dye and
 *         no appearance, or the index 0 when `item` is null or has no index
 *         (states/paperdoll_state.cpp).
 */
[[nodiscard]] hub::Bytes encode_item_look(const world::ItemInstance* item, ItemWire wire = {});

/** The key of the scope fields of a creature: none sent yet (a session with no record), and the fields with no record. */
inline constexpr std::uint32_t kScopeUnknown = 0xFFFFFFFFU;
inline constexpr std::uint32_t kScopeNone = 0;

/** The action that the client of a session must play for a creature (states/movement_state.cpp). */
struct CreatureScope {
    /** The action of the scope record. Null: the scope fields with no record (the idle action). */
    const world::CreatureAction* action{nullptr};
    /** True: the record has the time 0 (the spawn action of a first record). */
    bool fromStart{false};
    /** The key of this scope for the comparison with the key that the session sent last: kScopeNone or the start count. */
    std::uint32_t key{kScopeNone};
};

/**
 * @return The action that the client of a session must play for a creature: in the first
 *         record of the session the spawn action of a slice that needs it (the death action of
 *         a dead creature comes first), then the action of the server that plays (an attack, a
 *         reaction, the death, the action of a command), then the locomotion action of a
 *         creature on its way, then none.
 * @param first True for the first record of the session for the creature.
 */
[[nodiscard]] CreatureScope creature_scope(const world::Creature& creature, bool first);

/**
 * @return The movement state of a creature (states/movement_state.cpp): its position, its
 *         direction, and the action that the client must play.
 * @param since The world revision of the last record of the session for the creature. 0: the
 *        session has no record of it.
 * @param sent The key of the scope fields that the session sent last (kScopeUnknown: not
 *        known). The state has the scope fields only when the scope of creature_scope has a
 *        different key: the client keeps the fields of a state that a later state does not
 *        have, and it plays its action with no jump. A record that comes again with a new time
 *        makes the body jump (user test 1), and no fields after the end of a kept action make
 *        the client start that action again.
 * @param keepScope True: no scope fields for a session that has a record of the creature, also
 *        for a different action (the lead of a spawn action, PartsRequest::keepScope).
 */
[[nodiscard]] hub::Bytes encode_creature_movement_state(const world::Entity& entity, const world::WorldClock& clock,
                                                        world::Revision since, std::uint32_t sent = kScopeUnknown,
                                                        bool keepScope = false);
/** @return The same, in one fragment, for a session whose last record of the creature has the revision `request.since`. */
[[nodiscard]] std::vector<hub::Bytes> encode_creature_movement_parts(const world::Entity& entity, View view, const PartsRequest& request);
/**
 * @return The vitals state of a creature (states/vitals_state.cpp), in one fragment: the full
 *         state for a session that has no record of the creature, and after that the health
 *         and the dead mark.
 */
[[nodiscard]] std::vector<hub::Bytes> encode_creature_vitals_state(const world::Entity& entity, View view, const PartsRequest& request);
/** @return The marker state of a creature (states/marker_state.cpp): the name plate is on. */
[[nodiscard]] hub::Bytes encode_marker_state(const world::Entity& entity, View view, const world::WorldClock& clock);

/**
 * @return The id of a movement state for the owner: a value that is 0x4000 or more from the
 *         newest input number of its client, thus not the number of an input that the client
 *         has in its history (states/movement_state.cpp).
 * @param newestInput The newest input number that the session got from its client. 0 before
 *        the first input report.
 */
[[nodiscard]] std::uint16_t movement_state_id(std::uint16_t newestInput);

/**
 * @return The heartbeat state of the movement component: a new time anchor and the id of the
 *         state. The owner gets it at a constant interval after its full movement state
 *         (states/movement_state.cpp).
 * @param newestInput The newest input number that the session got from its client.
 */
[[nodiscard]] hub::Bytes encode_movement_heartbeat(const world::WorldClock& clock, std::uint16_t newestInput);

/**
 * @return The Interactor state of a character (stage 2.5a, states/interaction_states.cpp): the G of the object of its committed
 *         interaction, or the empty value after its end. No fragment for a character that had none.
 */
[[nodiscard]] std::vector<hub::Bytes> encode_interactor_parts(const world::Entity& entity, View view, const PartsRequest& request);
/** @return The Interact state of each bound of an object entity (stage 2.5a): Enabled and HasInteractors. */
[[nodiscard]] std::vector<SlotState> encode_interact_states(const world::Entity& entity, View view, const PartsRequest& request);

} // namespace soulwarden::backend::server::rep

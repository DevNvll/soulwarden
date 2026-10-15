#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "gamedata/ability_data.h"
#include "world/entity.h"
#include "world/ids.h"
#include "world/world_clock.h"

// The consumable system (stage 1.3b, itemization): the use of a potion, a food, or a dust from a
// quick slot or from the bag. World thread only.
//
// The client sends no message for a quick slot key: it sends the input bit of the key
// (Use_Quickslot_1 to 4: bits 404, 117, 140, 513 for the paperdoll slots 14 to 17; live check
// I7), and it plays the drink itself, also while a cooldown of the server runs (its gate reads
// only its own cooldown). The entry "Use" of the action menu of an item in the bag sends
// ContainerComponentServerFacet_ServerUseItem (type 2917) with the bag slot. Both come here with
// the session; the world finds the character from the session (rule MP-5).
//
// One use (Consumables::use), with the cells of the game data (perk-data.json: the consumable
// rows; ability-data.json: the status rows):
//   1. The item of the slot is a consumable of the data. The item class has no tag
//      SiegeWarOnly (an item of a siege war). A row with DisableUseInCombat: the character is not
//      in a fight. The cooldown of its CooldownId does not run for this item.
//   2. The inventory takes one item in one transaction (Inventory::take_one). A use that a
//      check refuses, or whose row the database does not take, changes nothing.
//   3. The effects (AddStatusEffects, with DurationOverrides): an effect with no duration and no
//      TickRate is one heal (Health + HealthModifierPercent of the maximum, times the heal factor
//      of the equip load) and one mana change (Mana; through the cost path of the abilities). An
//      effect with a duration stays on the character: its values count in the gear system (a
//      food: MODConstitution), a TickRate gives the heal and the mana at each tick, and
//      StopOnHitCount ends it after hits. RemoveStatusEffects and RemoveStatusEffectCategories
//      end the older effects first (a new food ends the old food).
//   4. The cooldown: CooldownDuration for the item, times 1 + the CooldownTimer of each perk of
//      the character with OnUsedConsumable (an earring). With a SharedCooldownLocString, the
//      other items of the same CooldownId get `kSharedPart` of the time; with none, the same time.
//      The cooldown state of the owner (field 3, the item map) shows the countdown on each
//      quick slot and in the bag: the key is the CRC-32 of the lower-case item id (live check I7).
//
// The effects and the cooldowns are values of the character (the character id is the key), not
// of a session: a new session of the character gets them. The tick looks only at the characters
// with an effect (m_active).

namespace soulwarden::backend::world {

class World;

namespace consumable_rules {
/**
 * The part of the cooldown that a use gives the other items of the same cooldown id when the consumable row has a
 * SharedCooldownLocString. The client text of that string: "Using this will put all other survival potions on 80%
 * cooldown". A project value with the number of the text: the data has no cell for the part.
 */
inline constexpr float kSharedPart = 0.8F;
} // namespace consumable_rules

enum class UseVerdict : std::uint8_t {
    used,
    /** The session is the owner of no entity. */
    noEntity,
    /** The backend has no perk data or no ability data. */
    noData,
    /** The character is dead. */
    dead,
    /** The slot has no item. */
    emptySlot,
    /** The item is not a consumable of the data. */
    notConsumable,
    /** The cooldown of the item runs. */
    cooldown,
    /** The item is for a siege war only. */
    notAllowed,
    /** The row has DisableUseInCombat, and the character is in a fight. */
    inCombat,
    /** The database did not take the change. */
    storage,
};
inline constexpr std::size_t kUseVerdictCount = 10;

/** @return A short text for a log line, for example "the cooldown runs". */
[[nodiscard]] const char* use_verdict_text(UseVerdict verdict);

/** Counts of the consumable system, for the log and for the tests. */
struct ConsumableCounts {
    std::array<std::uint64_t, kUseVerdictCount> uses{};
    /** Ticks of the effects, heals, and effects that ended (by time, by hits, by a newer effect, or by death). */
    std::uint64_t ticks{0};
    std::uint64_t heals{0};
    std::uint64_t ended{0};
};

class Consumables {
public:
    explicit Consumables(World& world) : m_world(world) {}

    /** A session gives the key of a quick slot (1 to 4: the paperdoll slots 14 to 17). */
    UseVerdict use_quick(SessionId session, unsigned key);
    /** A session gives the use of an item in a bag slot (message 2917). */
    UseVerdict use_bag(SessionId session, std::uint32_t slot);
    /** The use of the item of a slot of the bag or of the paperdoll by a character in the world (also a command). */
    UseVerdict use(const std::string& characterId, bool paperdoll, std::uint32_t slot);

    /** The world made the entity of a character: it gets the effects and the item cooldowns of the character. */
    void entered(Entity& character);
    /** The world calls it in each tick: the ticks and the ends of the effects. */
    void tick();
    /** A character took a hit of a creature: an effect with StopOnHitCount counts it. */
    void hit_taken(Entity& character);
    /**
     * Gives a character an effect of a status row for a time (a consumable, the status of a heart rune): the effect
     * counts in the gear system, and a TickRate gives its heal and mana. The same status again has the new times.
     * @param seconds The time; 0 or less: one application now (no effect stays).
     */
    void add_effect(Entity& character, const gamedata::StatusRow& status, float seconds, float potency = 1.0F);

    /** @return The effects of a character (the tests and the development route). */
    [[nodiscard]] const std::vector<PlayerEffect>* effects_of(const std::string& characterId) const;
    [[nodiscard]] const ConsumableCounts& counts() const {
        return m_counts;
    }

private:
    /** The cooldown of one cooldown id of a character. */
    struct Group {
        /** The item of the last use, and the end of its cooldown. */
        std::string itemId;
        WorldClock::time_point end{};
        /** The end for the other items of the id. */
        WorldClock::time_point sharedEnd{};
    };
    struct CharacterState {
        std::map<std::string, Group> groups;
        std::vector<PlayerEffect> effects;
    };

    UseVerdict counted(UseVerdict verdict) {
        ++m_counts.uses[static_cast<std::size_t>(verdict)];
        return verdict;
    }
    /** Puts the effects and the item cooldowns into the entity, marks the cooldown state, and refreshes the gear values. */
    void publish(Entity& character, CharacterState& state, bool effectsChanged);
    /** The heal and the mana of one application or tick of an effect. */
    void give(Entity& character, const gamedata::StatusRow& row, float potency);
    /** Ends the effects of a character that `end(effect)` selects. @return The number of effects that ended. */
    template <class Select>
    unsigned end_effects(CharacterState& state, Select&& end);

    World& m_world;
    std::unordered_map<std::string, CharacterState> m_characters;
    /** The characters with an effect: the tick looks only at these. */
    std::set<std::string> m_active;
    ConsumableCounts m_counts;
};

} // namespace soulwarden::backend::world

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
//      SiegeWarOnly (an item of a siege war). The level of the character is not below the
//      RequiredLevel of the item (code review of stage 1.3b, F5). A row with DisableUseInCombat:
//      the character is not in a fight. The cooldown of its CooldownId does not run for this item.
//   2. The inventory takes one item in one transaction (Inventory::take_one). A use that a
//      check refuses, or whose row the database does not take, changes nothing.
//   3. The effects (AddStatusEffects, with DurationOverrides) go to the status system of the
//      character (world/statuses.h, stage 3.3b-S1), with the source "consumable": a status with no
//      duration is one heal and one mana change, a status with a duration stays (its values, its
//      ticks, StopOnHitCount). RemoveStatusEffects and RemoveStatusEffectCategories end the older
//      statuses first (a new food ends the old food).
//   4. The cooldown: CooldownDuration for the item, times 1 + the CooldownTimer of each perk of
//      the character with OnUsedConsumable (an earring). With a SharedCooldownLocString, the
//      other items of the same CooldownId get `kSharedPart` of the time; with none, the same time.
//      The cooldown state of the owner (field 3, the item map) shows the countdown on each
//      quick slot and in the bag: the key is the CRC-32 of the lower-case item id (live check I7).
//      Each item keeps its own end, and a shared end does not make an end earlier (finding F7). A
//      change of the bag or of the quick slots gives the map again: an item that comes into the
//      bag during a cooldown of its id gets the countdown (finding F8).
//
// The cooldowns are values of the character (the character id is the key), not of a session: a new
// session of the character gets them.

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
    /** The level of the character is below the RequiredLevel of the item (finding F5). */
    level,
};
inline constexpr std::size_t kUseVerdictCount = 11;

/** @return A short text for a log line, for example "the cooldown runs". */
[[nodiscard]] const char* use_verdict_text(UseVerdict verdict);

/** Counts of the consumable system, for the log and for the tests. */
struct ConsumableCounts {
    std::array<std::uint64_t, kUseVerdictCount> uses{};
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

    /** The world made the entity of a character: it gets the item cooldowns of the character. */
    void entered(Entity& character);
    /**
     * The bag or the quick slots of a character changed (world/inventory.h): the item map of the cooldown state gets the
     * items of a cooldown that runs (finding F8).
     */
    void items_changed(Entity& character);
    [[nodiscard]] const ConsumableCounts& counts() const {
        return m_counts;
    }

private:
    /** The cooldown of one cooldown id of a character. */
    struct Group {
        /** The end of the own cooldown of each item that was used (the lower-case item id; finding F7). */
        std::map<std::string, WorldClock::time_point> ends;
        /** The end for each item of the id: the latest shared end. */
        WorldClock::time_point sharedEnd{};
        /** @return The end of the cooldown of one item of the id. */
        [[nodiscard]] WorldClock::time_point end_of(const std::string& item) const {
            const auto found = ends.find(item);
            return found == ends.end() ? sharedEnd : std::max(found->second, sharedEnd);
        }
    };
    struct CharacterState {
        std::map<std::string, Group> groups;
    };

    UseVerdict counted(UseVerdict verdict) {
        ++m_counts.uses[static_cast<std::size_t>(verdict)];
        return verdict;
    }
    /** Puts the item cooldowns into the entity and marks the cooldown state. */
    void publish(Entity& character, const CharacterState& state);

    World& m_world;
    std::unordered_map<std::string, CharacterState> m_characters;
    ConsumableCounts m_counts;
};

} // namespace soulwarden::backend::world

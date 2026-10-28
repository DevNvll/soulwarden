#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "world/entity.h"
#include "world/inventory.h"

// The loot container items of a character (stage 1.8d, docs/systems/items-and-equipment.md): the open of an item of the class
// LootContainer (the Meat Pouch, QuestReward_MB_Meat_Pouch, the reward of 06_skinningknife) rolls the loot table of its RepairRecipe
// ("[LTID]NAME": the loot data `containerRoots`) and gives the items of the roll in one transaction with the take of the container.
//
// The client (research quests-18d rules Q7): the entry "Open" of the action menu of the bag (and a double click) sends 1733,
// CraftingComponentServerFacet_InitiateSalvage, with the bag slot, the instance, and the quantity 1 (no selector). The items come with the
// cause OpenRewardChest (26) in the container state of the bag; the client shows its reward popup. No other answer.
//
// The rules: the item of the slot must be the instance of the request, of the class LootContainer, with a table of the loot data, and not
// locked by the player; the quantity is 1 to the stack; each unit is one roll of the table (the roller of the rewards, Rewards::roll_root:
// the roller of the client 0x368B070) with the level of the character; the items have the rules of a new instance of stage 1.3b
// (Rewards::grant_items). A full bag refuses the whole open (the rule of a gather: decision of the user for gathering, a decision of an
// agent for a container); a refused write changes nothing. The character is the character of the session (a facet of a different entity
// changes nothing).
//
// Stage QG2a (research quest-gaps.md section 8): a container with a SalvageGameEventID (the coin purses QuestReward_MB_Coin_Purse2 and
// QuestReward_WW_Coin_Purse2, Coin_Stack_500) pays that game event at its open: CurrencyReward (the units of the wallet: 2500 is 25.00 coin)
// with CoinRewardChance for each unit, in the transaction of the take, up to the wallet limit. A container with a table and an event rolls
// the table and pays the event. An event with another reward (an entitlement: a skin box; a mount achievement; AzothSalt; an item, the
// experience, or the Azoth of an event: no path of the open yet) or with no row of the data refuses the open (eventNotPayable), and the
// item stays. No loot table is made for such an item [data: the purses have no RepairRecipe; agent: the open path pays the coin only].
//
// The cost: one slot, one table roll for each unit (MP-8). World thread only.

namespace soulwarden::backend::world {

class World;

enum class OpenError : std::uint8_t {
    none,
    /** The backend has no loot data or no item catalog. */
    noData,
    /** The slot, the instance, the quantity, or the lock: the inventory refused it (ContainerResult::itemError has the cause). */
    itemRefused,
    /** The item is not a loot container, or the loot data has no table of it. */
    notContainer,
    /** The roll gave no item (a table with conditions that the character does not meet): the container stays. */
    emptyRoll,
    /**
     * Code review of stage 1.8d, finding QR8D-R5: the roll gave a loot container that cannot open (no loot table of it in the loot data): the
     * whole open is refused, as a hand-in refuses such a reward, and the container stays.
     */
    innerNotOpenable,
    /** Stage QG2a: the salvage event of the container has no row, or a reward that the open does not pay (OpenResult::detail names it). */
    eventNotPayable,
};

[[nodiscard]] const char* open_error_text(OpenError error);

struct OpenResult {
    OpenError error{OpenError::none};
    ItemError itemError{ItemError::none};
    std::string itemId;
    std::uint32_t opened{0};
    /** The items of the rolls ("ITEM xN"). */
    std::vector<std::string> items;
    /** Stage QG2a: the coin of the salvage event (the units of the wallet, after the wallet limit), and the cause of eventNotPayable. */
    std::uint64_t coin{0};
    std::string detail;
    [[nodiscard]] bool ok() const {
        return error == OpenError::none;
    }
};

struct ContainerCounts {
    std::uint64_t opens{0};
    std::uint64_t refused{0};
    std::uint64_t rolls{0};
    std::uint64_t items{0};
    /** Stage QG2a: the salvage events that an open paid, and their coin (the units of the wallet). */
    std::uint64_t events{0};
    std::uint64_t coin{0};
};

class ItemContainers {
public:
    explicit ItemContainers(World& world) : m_world(world) {}

    /**
     * @return True when an item is a loot container whose open the backend runs: the loot data has its table, or (stage QG2a) its salvage
     *         event pays only coin (the hand-in of a quest pays such an item).
     */
    [[nodiscard]] bool openable(const std::string& itemId) const;
    /** Stage QG2a: @return The cause why a loot container cannot open (an empty text: it can, or the item is not a loot container). */
    [[nodiscard]] std::string open_refusal(const std::string& itemId) const;
    /**
     * The open of `quantity` units of the loot container of a bag slot of a character (message 1733 of the client, or a command): the rolls,
     * one transaction of the inventory (the take and the items), then the entity and the container state.
     */
    OpenResult open(Entity& character, std::uint32_t slot, const InstanceId& instance, std::uint32_t quantity);

    [[nodiscard]] const ContainerCounts& counts() const {
        return m_counts;
    }

private:
    World& m_world;
    ContainerCounts m_counts;
};

} // namespace soulwarden::backend::world

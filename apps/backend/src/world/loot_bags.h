#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "gamedata/interaction_data.h"
#include "world/entity.h"
#include "world/inventory.h"
#include "world/world_clock.h"

// The personal loot bags (stage 3.5b.2, docs/systems/loot-bags.md, protocol notes 4.42): the bag of the items of a kill (AILoot) and the
// bag of the discard of a player (Discarded). Both are the personal container of the game data (slices/instanced_loot_container; the
// interaction data, format 3: lootContainers "personal").
//
// A bag is a server child of the interaction system (its record: the metadata state, PositionInTheWorld, Interact, LootDrop) with a
// private list: only its eligible characters observe it (ObjectBody::viewers). Each eligible character has its own part: its own item
// list (stored item instances: the instance id, the gear score, the durability, the flags, the perks; made one time). An observer that is
// not eligible gets no record (live check L5 of stage 3.5b.2: a character with no LootTracker entry of a bag sees nothing of it).
//
// The states of the owner (live check L5): the LootTracker entry of each of its bags (slot 33: Rolled with items, Looted when its part is
// empty; the kind 1 AILoot or 5 Discarded; the place; the selector of the rarity of its best item), the GlobalStorage map (slot 77: the
// items of each of its bags under the actor of the bag; the item list under the prompt), and the selected Container (slot 76: the items of
// the bag that it opened; the panel). The client clears its selected Container at the open: the server sends slot 76 after it took the
// request.
//
// The requests (live check L5):
//   - OpenStorageOption (T, or Tab near a bag): an interaction request of the bag (action 26, committed). The use selects the bag: slot 76.
//   - TakeAllOption (E): an interaction request (action 28, committed). The server takes each item that fits, then the use ends (Looted:
//     the client ends it; items left: 3746).
//   - 2939 TakeAllItems and 2942 TakeItem of the panel: the reference names the selected Container of the character, not the bag. The
//     bag is the bag of the open use of the character (its committed interaction), never a value of the request.
//   - 2801 GiveItem: an item of the bag of the character into the open bag (the drop into the panel).
//   - 1838 DropInventoryItems and 826 PaperdollDropItem: a discard: the items go into a new Discarded bag at the feet of the character.
// A pickup is one transaction of the store: the bag rows of the character (Inventory::receive), the rows of its part. A full bag takes
// what fits; the rest stays in the part (no item lost, made again, or rolled again).
//
// The lifetime (game data, live check L6): the bag ends at its lifetime (KillSelfDelay of the slice, 180 s from the bag), and FastKillSelfDelay
// (1.25 s) after the last item of the last part. The client does not end a bag by itself (live check L6): the server removes the record.
// The end time is on the wall clock in the rows: a restart keeps the remaining time, and the time goes on while the backend is off. The fast
// end is in the rows too (the take that empties the bag writes it; code review of stage 3.5b.2, finding LB-R9). A request at either end
// changes nothing (finding LB-R2).
//
// The corrections of stage 2.2 (code review of stage 3.5b.2): a part has 500 stacks at most (the Container of the client; a give into a
// full part merges into a stack or is refused; finding LB-R4); the GlobalStorage selection is made again after a walk (finding LB-R3); an
// item of a kill that the item catalog does not have stays in the rows of the part with no instance (a kept roll; the client does not see
// it; the open point of 3.5b.2, a decision of the orchestrator).
//
// The cost (MP-8): a request finds its bag by the entity of its use (a hash); the states of a character look only at its own bags; the
// end of a bag is an entry of the due list; the interest of a session finds a bag in the grid of the interaction system. No look at each
// bag of the world. World thread only.

namespace soulwarden::backend::world {

class World;
class InteractionHandler;
struct InteractionContext;
struct InteractionReference;
enum class InteractionEnd : std::uint8_t;
enum class InteractionVerdict : std::uint8_t;

/** Rules of this project and values of the client for the loot bags (docs/systems/loot-bags.md). */
namespace loot_bag_rules {
/** The name of the loot container of the interaction data (format 3). */
inline constexpr const char* kPersonalContainer = "personal";
/** InstancedLootType values (client reflection 0x36FD750): 1 AILoot (a kill), 5 Discarded (a drop of a player). */
inline constexpr std::uint32_t kAiLoot = 1;
inline constexpr std::uint32_t kDiscarded = 5;
/** InstancedLootState values (client reflection 0x36FD370): Rolled (the bag shows), Looted (the client hides the bag of the owner). */
inline constexpr std::uint32_t kRolled = 1;
inline constexpr std::uint32_t kLooted = 2;
/** A full GlobalStorage map of the client takes 30 keys at most (research storage.md, the full map reader). */
inline constexpr std::size_t kStorageKeys = 30;
/**
 * The GlobalStorage selection of a character is made again after a walk of this horizontal distance (meters) from the place of the last
 * selection (code review of stage 3.5b.2, finding LB-R3; a decision of an agent: the prompt of a bag shows at less than 2 m).
 */
inline constexpr float kStorageMoveDistance = 2.0F;
/** The quantity of a take or a give: 1 to 10,000 (client check 0x2D93E30). */
inline constexpr std::uint32_t kLargestQuantity = 10000;
/**
 * The rarity of an item from its perk count (client 0x5C528D0; research itemization.md K6, playerbaseattributes.pbadb): the largest count of
 * each rarity 0 to 4. An approximation: the backend has no ForceRarity column and no gem adjustment (docs/systems/loot-bags.md).
 */
inline constexpr std::array<std::uint32_t, 5> kRarityPerkCounts{0, 2, 3, 4, 5};
/** The end of a bag that the store did not write comes again after this time. */
inline constexpr std::chrono::seconds kRetry{10};
} // namespace loot_bag_rules

/** The part of a bag of one eligible character: its own items. */
struct BagPart {
    /** The item instances: 500 stacks at most (kMaxContainerSlots, the Container of the client; finding LB-R4). */
    std::vector<ItemInstance> items;
    /**
     * The items of the roll with no instance (stage 2.2): the item catalog does not have them (the open point of 3.5b.2: the rule of the
     * chests, finding CH-R3), or the part had 500 stacks. The rows keep them; the client does not see them. A load makes their instances
     * when the catalog has the item and the part has a place. A part with only such items is Looted for the client, and the bag stays
     * until its lifetime ends (no fast end).
     */
    std::vector<GrantItem> kept;
    /** The rarity of its best item (the selector of its LootTracker entry). */
    std::uint32_t rarity{0};
};

/** A personal loot bag (a value of the world; the rows `loot_bags`, `loot_bag_items` of each part). */
struct LootBag {
    /** The durable key (a UUID): the key of the rows. */
    std::string key;
    /** The server child of the bag (0: none). Its generation in the interaction system. */
    EntityId entity{0};
    std::uint64_t generation{0};
    std::uint32_t kind{loot_bag_rules::kAiLoot};
    /** The kind of the creature of a kill, or "discard". */
    std::string source;
    state::WorldPosition position;
    /** Unix milliseconds of the rows. */
    std::int64_t createdAt{0};
    std::int64_t expiresAt{0};
    /** The end of the lifetime on the world clock. */
    WorldClock::time_point expires{};
    /** The end after the last item of the last part (FastKillSelfDelay). No value: a part has items. */
    std::optional<WorldClock::time_point> emptied;
    /** The parts: character id to its items. */
    std::map<std::string, BagPart> parts;
};

/** Why a request of a loot bag changed nothing (the log and the counts). */
enum class BagRefusal : std::uint8_t {
    none,
    /** The backend has no personal container in its interaction data (format 3). */
    noData,
    /** No live bag is the source of the request (it ended, or a new generation). */
    noBag,
    /** The character is not an eligible character of the bag. */
    notReceiver,
    /** The part of the character has no item. */
    empty,
    /** The character has no open bag (no committed use of a bag): a request of the panel. */
    notOpen,
    /** The reference of the request is not the selected Container of the character. */
    wrongReference,
    /** The part has no item with this instance id, or the bag of the character has none. */
    noItem,
    /** The quantity is 0, above the stack, or above 10,000. */
    badQuantity,
    /** The bag of the character has no place for any of it. */
    bagFull,
    /** The inventory refused the change (a locked item, a wrong slot or instance). */
    itemRefused,
    /** The character is dead, or out of the range of the bag. */
    notValid,
    /** The store did not write the change: nothing changed. */
    storeFailed,
    /**
     * The part would have more than 500 stacks (finding LB-R4): a give into a part of 500 stacks with no stack that takes the item, or a
     * discard of more than 500 stacks.
     */
    partFull,
};
inline constexpr std::size_t kBagRefusalCount = 14;

[[nodiscard]] const char* bag_refusal_text(BagRefusal refusal);

struct BagResult {
    BagRefusal refusal{BagRefusal::none};
    /** The key of the bag of the request. */
    std::string bag;
    /** The quantity that moved, and the quantity that stays with the source (a full bag). */
    std::uint32_t moved{0};
    std::uint32_t left{0};
    /** The inventory error of an `itemRefused`. */
    ItemError itemError{ItemError::none};

    [[nodiscard]] bool ok() const {
        return refusal == BagRefusal::none;
    }
};

/** The counts of the system: for the log, the commands, and the tests. */
struct LootBagCounts {
    std::uint64_t bags{0};
    std::uint64_t parts{0};
    std::uint64_t discards{0};
    std::uint64_t takes{0};
    std::uint64_t itemsTaken{0};
    std::uint64_t itemsGiven{0};
    std::uint64_t expired{0};
    std::uint64_t emptied{0};
    std::uint64_t loaded{0};
    std::uint64_t storeFailures{0};
    std::array<std::uint64_t, kBagRefusalCount> refusals{};
    /** The states of the characters made again, and the bags that they looked at (case GL7). */
    std::uint64_t refreshes{0};
    std::uint64_t refreshBags{0};
    /** The due ends that the tick looked at (case GL7). */
    std::uint64_t dueLooks{0};
    /** The GlobalStorage selections after a walk (finding LB-R3). */
    std::uint64_t moveSelections{0};
    /** The kept items of a roll (the catalog does not have them, or the part had 500 stacks), and those that a load made. */
    std::uint64_t keptItems{0};
    std::uint64_t keptMade{0};
};

/** A bag of a character, for a reader (a command). */
struct BagView {
    std::string key;
    std::uint32_t kind{loot_bag_rules::kAiLoot};
    std::string source;
    EntityId entity{0};
    state::WorldPosition position;
    float distance{0.0F};
    double secondsLeft{0.0};
    bool emptied{false};
    std::vector<ItemInstance> items;
    std::size_t receivers{0};
};

struct LootBagsView {
    bool inWorld{false};
    bool data{false};
    /** The key of the open bag (the committed use). Empty: none. */
    std::string open;
    std::vector<BagView> bags;
};

/** The part of a kill of one character before the commit of its reward (Rewards::killed). */
struct KillBag {
    std::string key;
    std::string source;
    state::WorldPosition position;
    std::int64_t createdAt{0};
    std::int64_t expiresAt{0};
    WorldClock::time_point expires{};
    std::map<std::string, BagPart> parts;
};

class LootBags {
public:
    LootBags(World& world);
    ~LootBags();
    LootBags(const LootBags&) = delete;
    LootBags& operator=(const LootBags&) = delete;

    /** @return The personal container of the interaction data, or null: no bag (a kill gives its items as in stage 3.5a). */
    [[nodiscard]] const gamedata::LootContainer* container() const;

    /** The world calls it at the entry of a character: the states of its bags. */
    void entered(Entity& character);
    /** The world calls it at the leave of a character (the bags and their rows stay). */
    void left(Entity& character);
    /**
     * The world calls it after a change of the place of a character (World::place): after a walk of kStorageMoveDistance from the place of
     * its last selection, the GlobalStorage selection of its own bags is made again (finding LB-R3). O(1) for a character with no bag or
     * a short walk; else the bags of the character only.
     */
    void moved(Entity& character);
    /** The world tick: the first one loads the bags of the store; the bags whose end came (the due list). */
    void tick();

    /** The start of the bag of a kill (Rewards::killed). */
    [[nodiscard]] KillBag begin_kill(const Entity& creature);
    /**
     * In the batch of the reward of a character: the instances of its items (made one time) and the row of its part. An item that the
     * catalog does not have, and a stack beyond 500, stay in the part as a kept roll (no instance; the client does not see it; a log line
     * names it). No item is lost.
     * @param kept Gets the kept items. @return False: the store refused the row.
     */
    [[nodiscard]] bool stage_part(KillBag& bag, const Entity& character, const std::vector<GrantItem>& items, std::vector<GrantItem>* kept);
    /** After the commit of the reward of a character: its part is in the bag. A rollback: forget_part. */
    void forget_part(KillBag& bag, const std::string& characterId);
    /** After the rewards of each character: the bag with each committed part goes into the world (its record, the states). */
    void finish_kill(KillBag& bag);

    /** The handler of the options of a bag (world/interactions.h). */
    [[nodiscard]] InteractionVerdict begin(Entity& character, const InteractionContext& context);
    void started(Entity& character, const InteractionContext& context);
    void ended(Entity& character, const InteractionContext& context, InteractionEnd cause);

    /** 2939: each item of the open bag that fits. `container`: the reference of the request (the selected Container of the character). */
    BagResult take_all(const std::string& characterId, const InteractionReference& container);
    /** 2942: a quantity of one item of the open bag. */
    BagResult take_item(const std::string& characterId, const InteractionReference& container, const InstanceId& instance,
                        std::uint32_t quantity);
    /** 2801: a quantity of an item of the bag of the character into the open bag. */
    BagResult give_item(const std::string& characterId, const InteractionReference& container, const InstanceId& instance,
                        std::uint32_t quantity);
    /** 1838: the discard of items of the bag: a new Discarded bag at the feet of the character. */
    BagResult discard(const std::string& characterId, const std::vector<DropEntry>& entries);
    /** 826: the discard of the item of a slot of the paperdoll. */
    BagResult discard_equipped(const std::string& characterId, std::uint32_t slot, std::uint32_t quantity);

    /** @return The bags of a character (its own parts only) and its open bag. */
    [[nodiscard]] LootBagsView view(const std::string& characterId) const;
    [[nodiscard]] const LootBagCounts& counts() const {
        return m_counts;
    }
    [[nodiscard]] std::size_t bag_count() const {
        return m_bags.size();
    }
    [[nodiscard]] std::size_t due_count() const {
        return m_due.size();
    }
    /** @return The bag of a key, or null (tests, commands). */
    [[nodiscard]] const LootBag* find(const std::string& key) const;
    /** @return The bag of a server child entity, or null. */
    [[nodiscard]] const LootBag* find_entity(EntityId entity) const;

private:
    struct Due {
        std::string bag;
    };

    void load();
    /** Makes the server child of a bag and puts the bag into the world (its indexes, its due ends, the states of its characters). */
    void place(LootBag bag);
    /** Ends a bag: its rows, its record, its indexes, the states of its characters. @return False: the store refused (it stays). */
    bool end_bag(const std::string& key, const char* cause);
    /** The bag of the open use of a character (its committed interaction on a bag), or null. */
    [[nodiscard]] LootBag* open_bag(const Entity& character);
    /**
     * The LootTracker entries, the storage map, and the selected Container of a character from its bags. The storage map: the open bag
     * first (it stays in the selected keys; finding LB-R3), then the nearest bags with items, 30 keys at most.
     * @param panel False: not the selected Container (a selection after a walk).
     */
    void refresh(Entity& character, bool panel = true);
    /** @return True at the end of the lifetime of a bag or at its fast end (finding LB-R2: one end check for each operation). */
    [[nodiscard]] bool ended(const LootBag& bag) const;
    /** @return The fast end of a bag that is empty now (FastKillSelfDelay of the data). */
    [[nodiscard]] WorldClock::time_point fast_end() const;
    /** The selected Container of a character: the items of its part of the open bag (a new full vector), or empty. */
    void set_panel(Entity& character, const LootBag* bag);
    /** @return The row of the part of a character. */
    [[nodiscard]] state::LootBagRow row_of(const LootBag& bag, const std::string& characterId) const;
    /** The checks of a request of the panel: the open bag of the character and the reference of its selected Container. */
    [[nodiscard]] LootBag* checked_open(Entity& character, const InteractionReference& container, BagRefusal& refusal);
    /** Moves items of a part into the bag of its character (a take): one transaction. `wanted`: (index into the part, quantity). */
    BagResult transfer(Entity& character, LootBag& bag, const std::vector<std::pair<std::size_t, std::uint32_t>>& wanted);
    /** After a change of a part: the empty end of the bag, the states of the character. */
    void part_changed(LootBag& bag, Entity& character);
    /** A new Discarded bag with the released items of a character (one transaction with the inventory change in `release`). */
    template <class Release>
    BagResult make_discard(Entity& character, Release&& release);
    BagResult refuse(BagRefusal refusal, const Entity& character, const char* what, const std::string& bag = {});
    [[nodiscard]] std::uint32_t rarity_of(const std::vector<ItemInstance>& items) const;
    [[nodiscard]] std::int64_t wall_ms(WorldClock::time_point time) const;

    World& m_world;
    std::unique_ptr<InteractionHandler> m_handler;
    std::map<std::string, LootBag> m_bags;
    std::unordered_map<EntityId, std::string> m_byEntity;
    std::unordered_map<std::string, std::set<std::string>> m_byCharacter;
    std::multimap<WorldClock::time_point, Due> m_due;
    /** The place of the last GlobalStorage selection of each character in the world (finding LB-R3). */
    std::unordered_map<std::string, state::WorldPosition> m_selectedAt;
    bool m_loaded{false};
    LootBagCounts m_counts;
};

} // namespace soulwarden::backend::world

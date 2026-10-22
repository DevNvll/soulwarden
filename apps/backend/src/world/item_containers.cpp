#include "world/item_containers.h"

#include <algorithm>

#include "core/logging/log.h"
#include "gamedata/item_catalog.h"
#include "gamedata/loot_tables.h"
#include "world/rewards.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

/** The open of one request opens this many units at most (the client sends 1: a guard of a command). */
constexpr std::uint32_t kLargestOpen = 100;

bool is_container(const gamedata::ItemDefinition& definition) {
    return gamedata::lower_text(definition.itemClass).find("lootcontainer") != std::string::npos;
}

} // namespace

const char* open_error_text(OpenError error) {
    switch (error) {
    case OpenError::none:
        return "opened";
    case OpenError::noData:
        return "the backend has no loot data or no item catalog";
    case OpenError::itemRefused:
        return "the inventory refused the open";
    case OpenError::notContainer:
        return "the item is not a loot container with a loot table of the loot data";
    case OpenError::emptyRoll:
        return "the roll of the loot table gave no item: the container stays";
    case OpenError::innerNotOpenable:
        return "the roll gave a loot container that cannot open (no loot table): the container stays";
    }
    return "?";
}

bool ItemContainers::openable(const std::string& itemId) const {
    const gamedata::LootTables* tables = m_world.rewards().tables();
    const gamedata::ItemCatalog* catalog = m_world.options().itemCatalog;
    const gamedata::ItemDefinition* definition = catalog != nullptr ? catalog->find(itemId) : nullptr;
    return tables != nullptr && definition != nullptr && is_container(*definition) && tables->container_table(itemId).has_value();
}

OpenResult ItemContainers::open(Entity& character, std::uint32_t slot, const InstanceId& instance, std::uint32_t quantity) {
    OpenResult result;
    const auto refuse = [&](OpenError error, ItemError itemError = ItemError::none) {
        result.error = error;
        result.itemError = itemError;
        ++m_counts.refused;
        log::write("world: the open of the container of bag slot %u of character %s is refused: %s%s%s", slot, character.characterId.c_str(),
                   open_error_text(error), itemError != ItemError::none ? ": " : "", itemError != ItemError::none ? item_error_text(itemError) : "");
        return result;
    };
    const gamedata::LootTables* tables = m_world.rewards().tables();
    const gamedata::ItemCatalog* catalog = m_world.options().itemCatalog;
    if (tables == nullptr || catalog == nullptr || !catalog->loaded()) {
        return refuse(OpenError::noData);
    }
    if (slot >= character.bag.slots.size() || !character.bag.slots[slot].item) {
        return refuse(OpenError::itemRefused, slot >= character.bag.slots.size() ? ItemError::badSlot : ItemError::emptySlot);
    }
    const ItemInstance& item = *character.bag.slots[slot].item;
    result.itemId = item.itemId;
    if (item.instance != instance) {
        return refuse(OpenError::itemRefused, ItemError::wrongInstance);
    }
    const gamedata::ItemDefinition* definition = catalog->find(item.itemId);
    const std::optional<std::uint32_t> table = definition != nullptr && is_container(*definition) ? tables->container_table(item.itemId) : std::nullopt;
    if (!table) {
        return refuse(OpenError::notContainer);
    }
    if (quantity == 0 || quantity > std::min(item.quantity, kLargestOpen)) {
        return refuse(OpenError::itemRefused, ItemError::badQuantity);
    }
    // One roll of the table for each unit, with the level of the character (the content level of the roll: the item row of the supported
    // build has no ContainerLevel for a quest container; a decision of an agent). Code review of stage 1.8d, finding QR8D-R2: the roll is the
    // roll of a loot container (the numeric container tags, and the container floor ContainerLootGSFloor of the game data for a table with
    // UseLevelGS), not the roll of a creature (AILootGSCeiling).
    LootContext context;
    context.characterLevel = character.progression.level;
    context.creatureLevel = character.progression.level;
    context.container = true;
    std::vector<LootDrop> drops;
    for (std::uint32_t unit = 0; unit < quantity; ++unit) {
        ++m_counts.rolls;
        for (LootDrop& drop : m_world.rewards().roll_root(*table, context)) {
            drops.push_back(std::move(drop));
        }
    }
    if (drops.empty()) {
        return refuse(OpenError::emptyRoll);
    }
    // Finding QR8D-R5: each rolled loot container must open, before the take (the rule of a hand-in reward: Quests::reward_join).
    for (const LootDrop& drop : drops) {
        const gamedata::ItemDefinition* rolled = catalog->find(drop.itemId);
        if (rolled != nullptr && is_container(*rolled) && !openable(rolled->id)) {
            log::write("world: the roll of %s gave the loot container %s, which has no loot table", item.itemId.c_str(), rolled->id.c_str());
            return refuse(OpenError::innerNotOpenable);
        }
    }
    const std::vector<GrantItem> items = m_world.rewards().grant_items(drops, character.progression.level);
    Inventory& inventory = m_world.inventory();
    Inventory::Plan plan = inventory.plan_open(character.characterId, slot, instance, quantity, items);
    if (!plan.ok()) {
        return refuse(OpenError::itemRefused, plan.error);
    }
    if (!inventory.write(plan, std::nullopt)) {
        return refuse(OpenError::itemRefused, ItemError::storeFailed);
    }
    (void)inventory.finish(plan, "the open of a loot container");
    ++m_counts.opens;
    result.opened = quantity;
    std::string text;
    for (const GrantItem& grant : items) {
        m_counts.items += grant.quantity;
        result.items.push_back(grant.itemId + " x" + std::to_string(grant.quantity));
        text += (text.empty() ? "" : ", ") + result.items.back();
    }
    log::write("world: character %s opened %u %s (bag slot %u): [%s]", character.characterId.c_str(), quantity, result.itemId.c_str(), slot,
               text.c_str());
    return result;
}

} // namespace soulwarden::backend::world

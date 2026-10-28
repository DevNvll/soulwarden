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

/**
 * Stage QG2a: @return The first reward of a salvage event that the open does not pay (the open pays CurrencyReward with CoinRewardChance),
 *         or an empty text.
 */
std::string unpaid_reward(const gamedata::QuestGameEvent& event) {
    if (!event.unpaid.empty()) {
        return event.unpaid.front();
    }
    if (!event.item.empty() && event.itemQty != 0) {
        return "ItemReward " + event.item;
    }
    if (event.experience > 0) {
        return "UniversalExpAmount " + std::to_string(event.experience);
    }
    if (event.azoth > 0) {
        return "AzothReward " + std::to_string(event.azoth);
    }
    if (event.standing > 0) {
        return "TerritoryStanding " + std::to_string(event.standing);
    }
    if (!event.achievement.empty()) {
        return "AchievementId " + event.achievement;
    }
    if (event.gearScoreRange) {
        return "GearScoreRange";
    }
    return {};
}

std::string coin_text(std::uint64_t coin) {
    return std::to_string(coin / 100) + "." + (coin % 100 < 10 ? "0" : "") + std::to_string(coin % 100) + " coin";
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
    case OpenError::eventNotPayable:
        return "the salvage event of the container has no row or a reward that the open does not pay: the container stays";
    }
    return "?";
}

bool ItemContainers::openable(const std::string& itemId) const {
    const gamedata::LootTables* tables = m_world.rewards().tables();
    const gamedata::ItemCatalog* catalog = m_world.options().itemCatalog;
    const gamedata::ItemDefinition* definition = catalog != nullptr ? catalog->find(itemId) : nullptr;
    if (tables == nullptr || definition == nullptr || !is_container(*definition)) {
        return false;
    }
    const std::string event = tables->container_event(itemId);
    if (!event.empty()) {
        const gamedata::QuestGameEvent* row = tables->salvage_event(event);
        return row != nullptr && unpaid_reward(*row).empty();
    }
    return tables->container_table(itemId).has_value();
}

std::string ItemContainers::open_refusal(const std::string& itemId) const {
    const gamedata::LootTables* tables = m_world.rewards().tables();
    const gamedata::ItemCatalog* catalog = m_world.options().itemCatalog;
    const gamedata::ItemDefinition* definition = catalog != nullptr ? catalog->find(itemId) : nullptr;
    if (definition == nullptr || !is_container(*definition)) {
        return {};
    }
    if (tables == nullptr) {
        return "the backend has no loot data";
    }
    const std::string event = tables->container_event(itemId);
    if (!event.empty()) {
        const gamedata::QuestGameEvent* row = tables->salvage_event(event);
        if (row == nullptr) {
            return "its salvage event " + event + " has no row in the loot data";
        }
        if (const std::string unpaid = unpaid_reward(*row); !unpaid.empty()) {
            return "its salvage event " + event + " has a reward with no owner in the open (" + unpaid + ")";
        }
        return {};
    }
    return tables->container_table(itemId) ? std::string{} : "it has no loot table and no salvage event in the loot data";
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
    const bool container = definition != nullptr && is_container(*definition);
    const std::optional<std::uint32_t> table = container ? tables->container_table(item.itemId) : std::nullopt;
    // Stage QG2a: the salvage event of the container (SalvageGameEventID): it pays its coin at the open.
    const std::string eventId = container ? tables->container_event(item.itemId) : std::string{};
    const gamedata::QuestGameEvent* event = eventId.empty() ? nullptr : tables->salvage_event(eventId);
    if (!table && eventId.empty()) {
        return refuse(OpenError::notContainer);
    }
    if (!eventId.empty()) {
        result.detail = event == nullptr ? "the salvage event " + eventId + " has no row" : unpaid_reward(*event);
        if (!result.detail.empty()) {
            log::write("world: the salvage event %s of %s: %s", eventId.c_str(), item.itemId.c_str(), result.detail.c_str());
            return refuse(OpenError::eventNotPayable);
        }
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
    std::uint64_t coin = 0;
    for (std::uint32_t unit = 0; unit < quantity; ++unit) {
        if (table) {
            ++m_counts.rolls;
            for (LootDrop& drop : m_world.rewards().roll_root(*table, context)) {
                drops.push_back(std::move(drop));
            }
        }
        // Stage QG2a: the coin of the salvage event for each unit, with its chance (a blank chance is 1: client loader 0x31E70B0, row +0x38).
        if (event != nullptr && event->coin > 0 && (event->coinChance >= 1.0 || m_world.random_unit() < event->coinChance)) {
            coin += event->coin;
        }
    }
    if (drops.empty() && coin == 0) {
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
    // The take, the items, and the coin of the event are one transaction (the coin up to the wallet limit, the rule of a hand-in).
    const std::optional<std::uint64_t> wallet =
        coin > 0 ? std::optional<std::uint64_t>(std::min(character.wallet.coin + coin, kWalletLimit)) : std::nullopt;
    if (!inventory.write(plan, wallet)) {
        return refuse(OpenError::itemRefused, ItemError::storeFailed);
    }
    const std::uint64_t coinBefore = character.wallet.coin;
    (void)inventory.finish(plan, "the open of a loot container");
    if (wallet) {
        inventory.coin_written(character.characterId, *wallet);
        result.coin = *wallet - coinBefore;
        ++m_counts.events;
        m_counts.coin += result.coin;
    }
    ++m_counts.opens;
    result.opened = quantity;
    std::string text;
    for (const GrantItem& grant : items) {
        m_counts.items += grant.quantity;
        result.items.push_back(grant.itemId + " x" + std::to_string(grant.quantity));
        text += (text.empty() ? "" : ", ") + result.items.back();
    }
    log::write("world: character %s opened %u %s (bag slot %u): [%s]%s%s", character.characterId.c_str(), quantity, result.itemId.c_str(), slot,
               text.c_str(), eventId.empty() ? "" : ", the salvage event ", eventId.empty() ? "" : (eventId + ": " + coin_text(result.coin)).c_str());
    return result;
}

} // namespace soulwarden::backend::world

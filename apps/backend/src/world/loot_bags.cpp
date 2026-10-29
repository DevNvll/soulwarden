#include "world/loot_bags.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/logging/log.h"
#include "world/interactions.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

/** The options of a bag (uidatabase.uidb: TakeAllOption action 28, OpenStorageOption action 26, both committed; live check L5). */
constexpr const char* kTakeAllOption = "TakeAllOption";
constexpr const char* kOpenOption = "OpenStorageOption";

std::string item_text(const std::vector<ItemInstance>& items) {
    std::string text;
    for (const ItemInstance& item : items) {
        text += (text.empty() ? "" : ", ") + std::to_string(item.quantity) + " " + item.itemId;
        if (item.gearScore != 0) {
            text += " (gear score " + std::to_string(item.gearScore) + ")";
        }
    }
    return text.empty() ? "no item" : text;
}

BagResult refused_result(BagRefusal refusal) {
    BagResult result;
    result.refusal = refusal;
    return result;
}

std::uint32_t quantity_of(const std::vector<ItemInstance>& items) {
    std::uint32_t total = 0;
    for (const ItemInstance& item : items) {
        total += item.quantity;
    }
    return total;
}

std::uint32_t quantity_of(const BagPart& part) {
    std::uint32_t total = quantity_of(part.items) + quantity_of(part.waiting);
    for (const GrantItem& item : part.kept) {
        total += item.quantity;
    }
    return total;
}

std::string kept_text(const std::vector<GrantItem>& items) {
    std::string text;
    for (const GrantItem& item : items) {
        text += (text.empty() ? "" : ", ") + std::to_string(item.quantity) + " " + item.itemId;
    }
    return text;
}

/** @return True when the part has no item: no instance (shown or waiting) and no kept roll. */
bool part_empty(const BagPart& part) {
    return part.items.empty() && part.waiting.empty() && part.kept.empty();
}

/** Moves the waiting instances into the free places of the 500 stacks of a part, in their order (finding WT-R2). */
void show_waiting(BagPart& part) {
    std::size_t moved = 0;
    while (moved < part.waiting.size() && part.items.size() < kMaxContainerSlots) {
        part.items.push_back(std::move(part.waiting[moved++]));
    }
    part.waiting.erase(part.waiting.begin(), part.waiting.begin() + static_cast<std::ptrdiff_t>(moved));
}

/** @return True when each part of the bag has no item (the fast end; a kept roll keeps the bag). */
bool bag_empty(const LootBag& bag) {
    return std::all_of(bag.parts.begin(), bag.parts.end(), [](const auto& part) { return part_empty(part.second); });
}

/** @return The kept roll of a new instance (a stack of a roll beyond the 500 stacks of a part): its item, quantity, gear score, and perks. */
GrantItem roll_of(const ItemInstance& item) {
    return GrantItem{item.itemId, item.quantity, item.gearScore != 0 ? std::optional<std::uint32_t>(item.gearScore) : std::nullopt, item.perks};
}

state::LootBagRow make_row(const std::string& key, std::uint32_t kind, const std::string& source, const state::WorldPosition& position,
                           const BagPart& part, std::int64_t createdAt, std::int64_t expiresAt, std::int64_t emptiedAt = 0) {
    state::LootBagRow row;
    row.bagKey = key;
    row.kind = kind;
    row.source = source;
    row.position = position;
    row.rarity = part.rarity;
    row.createdAt = createdAt;
    row.expiresAt = expiresAt;
    row.emptiedAt = emptiedAt;
    // The shown instances, then the waiting instances (finding WT-R2: all their fields), in their order.
    for (std::size_t index = 0; index < part.items.size() + part.waiting.size(); ++index) {
        const ItemInstance& item = index < part.items.size() ? part.items[index] : part.waiting[index - part.items.size()];
        state::ItemRow entry;
        entry.instanceId = instance_text(item.instance);
        entry.container = 0;
        entry.slot = static_cast<std::uint32_t>(index);
        entry.itemId = item.itemId;
        entry.quantity = item.quantity;
        entry.gearScore = item.gearScore;
        entry.durability = item.durability;
        entry.maxDurability = item.maxDurability;
        entry.flags = item.flags;
        entry.chrono = item.chrono;
        entry.perks = item.perks;
        row.items.push_back(std::move(entry));
    }
    // The kept rolls (stage 2.2): an empty instance id; the item, the quantity, the gear score (0: the value of the item table), the perks.
    for (const GrantItem& item : part.kept) {
        state::ItemRow entry;
        entry.container = 0;
        entry.slot = static_cast<std::uint32_t>(row.items.size());
        entry.itemId = item.itemId;
        entry.quantity = item.quantity;
        entry.gearScore = item.gearScore.value_or(0);
        entry.perks = item.perks;
        row.items.push_back(std::move(entry));
    }
    return row;
}

/** The handler of TakeAllOption and OpenStorageOption of an object of the kind "lootDrop": the loot bags (world/interactions.h). */
class LootBagHandler final : public InteractionHandler {
public:
    explicit LootBagHandler(LootBags& bags) : m_bags(bags) {}

    [[nodiscard]] const char* name() const override {
        return "loot bag";
    }
    [[nodiscard]] InteractionVerdict begin(Entity& character, const InteractionContext& context) override {
        return m_bags.begin(character, context);
    }
    void started(Entity& character, const InteractionContext& context) override {
        m_bags.started(character, context);
    }
    void end(Entity& character, const InteractionContext& context, InteractionEnd cause) override {
        m_bags.ended(character, context, cause);
    }

private:
    LootBags& m_bags;
};

} // namespace

const char* bag_refusal_text(BagRefusal refusal) {
    switch (refusal) {
    case BagRefusal::none:
        return "done";
    case BagRefusal::noData:
        return "the backend has no personal loot container (interaction data of format 3)";
    case BagRefusal::noBag:
        return "the bag is not there (it ended)";
    case BagRefusal::notReceiver:
        return "the character is not an eligible character of the bag";
    case BagRefusal::empty:
        return "the part of the character has no item";
    case BagRefusal::notOpen:
        return "the character has no open bag";
    case BagRefusal::wrongReference:
        return "the reference is not the selected Container of the character";
    case BagRefusal::noItem:
        return "no such item instance";
    case BagRefusal::badQuantity:
        return "the quantity is not 1 to the stack (10,000 at most)";
    case BagRefusal::bagFull:
        return "the bag of the character has no place for it";
    case BagRefusal::itemRefused:
        return "the inventory refused the item";
    case BagRefusal::notValid:
        return "the character is dead or out of the range of the bag";
    case BagRefusal::storeFailed:
        return "the store did not write the change";
    case BagRefusal::partFull:
        return "the part of the bag would have more than 500 stacks";
    }
    return "?";
}

LootBags::LootBags(World& world) : m_world(world), m_handler(std::make_unique<LootBagHandler>(*this)) {
    for (const char* option : {kTakeAllOption, kOpenOption}) {
        world.interactions().register_handler(gamedata::InteractionKind::lootDrop, gamedata::interaction_option_id(option), *m_handler);
    }
}

LootBags::~LootBags() = default;

const gamedata::LootContainer* LootBags::container() const {
    const gamedata::InteractionData* data = m_world.interactions().data();
    return data != nullptr ? data->loot_container(loot_bag_rules::kPersonalContainer) : nullptr;
}

std::int64_t LootBags::wall_ms(WorldClock::time_point time) const {
    return static_cast<std::int64_t>(m_world.clock().wall_time_ns(time) / 1000000ULL);
}

WorldClock::time_point LootBags::fast_end() const {
    const gamedata::LootContainer* data = container();
    return m_world.clock().now()
           + std::chrono::milliseconds(data != nullptr ? static_cast<long long>(std::lround(data->emptyLifetime * 1000.0F)) : 0LL);
}

bool LootBags::ended(const LootBag& bag) const {
    const WorldClock::time_point now = m_world.clock().now();
    return now >= bag.expires || (bag.emptied && now >= *bag.emptied);
}

std::uint32_t LootBags::rarity_of(const std::vector<ItemInstance>& items) const {
    // The rarity of the best item from its perk count (client 0x5C528D0: the largest count of each rarity; an approximation with no
    // ForceRarity and no gem adjustment).
    std::uint32_t best = 0;
    for (const ItemInstance& item : items) {
        std::uint32_t perks = 0;
        for (const std::string& perk : item.perks) {
            perks += perk.empty() ? 0U : 1U;
        }
        std::uint32_t rarity = 0;
        while (rarity + 1 < loot_bag_rules::kRarityPerkCounts.size() && perks > loot_bag_rules::kRarityPerkCounts[rarity]) {
            ++rarity;
        }
        best = std::max(best, rarity);
    }
    return best;
}

const LootBag* LootBags::find(const std::string& key) const {
    const auto found = m_bags.find(key);
    return found != m_bags.end() ? &found->second : nullptr;
}

const LootBag* LootBags::find_entity(EntityId entity) const {
    const auto found = m_byEntity.find(entity);
    return found != m_byEntity.end() ? find(found->second) : nullptr;
}

state::LootBagRow LootBags::row_of(const LootBag& bag, const std::string& characterId) const {
    const auto part = bag.parts.find(characterId);
    return make_row(bag.key, bag.kind, bag.source, bag.position, part != bag.parts.end() ? part->second : BagPart{}, bag.createdAt,
                    bag.expiresAt, bag.emptied ? wall_ms(*bag.emptied) : 0);
}

BagResult LootBags::refuse(BagRefusal refusal, const Entity& character, const char* what, const std::string& bag) {
    ++m_counts.refusals[static_cast<std::size_t>(refusal)];
    const std::string mark = log::session_mark(character.owner, character.characterId);
    const log::Context context(mark);
    log::write("world: loot bags: %s of character %s refused%s%s: %s", what, character.characterId.c_str(), bag.empty() ? "" : " (bag ",
               bag.empty() ? "" : (bag + ")").c_str(), bag_refusal_text(refusal));
    BagResult result;
    result.refusal = refusal;
    result.bag = bag;
    return result;
}

void LootBags::load() {
    m_loaded = true;
    const gamedata::LootContainer* data = container();
    if (data == nullptr) {
        return;
    }
    const gamedata::ItemCatalog* catalog = m_world.options().itemCatalog;
    const WorldClock::time_point now = m_world.clock().now();
    const std::int64_t wallNow = wall_ms(now);
    std::map<std::string, LootBag> bags;
    // The fast end of each bag: the largest value of its rows (finding LB-R9; the take that emptied the bag wrote it).
    std::map<std::string, std::int64_t> emptiedAt;
    std::size_t beyond = 0;
    std::size_t made = 0;
    std::size_t kept = 0;
    for (auto& [characterId, row] : m_world.store().list_loot_bags()) {
        LootBag& bag = bags[row.bagKey];
        if (bag.key.empty()) {
            bag.key = row.bagKey;
            bag.kind = row.kind;
            bag.source = row.source;
            bag.position = row.position;
            bag.createdAt = row.createdAt;
            bag.expiresAt = row.expiresAt;
        }
        bag.expiresAt = std::min(bag.expiresAt, row.expiresAt);
        emptiedAt[row.bagKey] = std::max(emptiedAt[row.bagKey], row.emptiedAt);
        BagPart& part = bag.parts[characterId];
        part.rarity = row.rarity;
        std::vector<GrantItem> rolls;
        for (const state::ItemRow& item : row.items) {
            if (item.quantity == 0) {
                continue;
            }
            if (item.instanceId.empty()) {
                // A kept roll (stage 2.2): the item catalog did not have the item, or the part had 500 stacks. Its whole quantity (finding
                // WT-R1: a roll can be more than one stack; the stack limit applies to each instance that a load makes of it).
                rolls.push_back(GrantItem{item.itemId, item.quantity,
                                          item.gearScore != 0 ? std::optional<std::uint32_t>(item.gearScore) : std::nullopt, item.perks});
                continue;
            }
            const auto instance = read_instance(item.instanceId);
            if (!instance) {
                continue;
            }
            ItemInstance value;
            value.instance = *instance;
            value.itemId = item.itemId;
            value.quantity = std::min(item.quantity, kMaxItemQuantity);
            value.gearScore = item.gearScore;
            value.durability = std::min(item.durability, kMaxItemDurability);
            value.maxDurability = std::min(item.maxDurability, kMaxItemDurability);
            value.flags = static_cast<std::uint8_t>(item.flags);
            value.chrono = item.chrono;
            value.perks = item.perks;
            const gamedata::ItemDefinition* definition = catalog != nullptr ? catalog->find(item.itemId) : nullptr;
            value.index = definition != nullptr ? definition->index : 0;
            m_world.inventory().resolve_perks(value);
            if (part.items.size() >= kMaxContainerSlots) {
                // The bound of a part (finding LB-R4): a stack beyond 500 (a part of a backend before stage 2.2) waits with all its fields
                // (finding WT-R2), and the client does not see it.
                part.waiting.push_back(std::move(value));
                ++beyond;
                continue;
            }
            part.items.push_back(std::move(value));
        }
        // A kept roll whose item the catalog has now gets its instances (made one time, with the values of the item table) when the part
        // has a place for all of them; the others stay kept, with their whole quantity (finding WT-R1). The count of the stacks comes
        // first: a roll that cannot fit makes no instance.
        bool changed = false;
        for (const GrantItem& roll : rolls) {
            const gamedata::ItemDefinition* definition = catalog != nullptr && catalog->loaded() ? catalog->find(roll.itemId) : nullptr;
            const std::uint64_t maxStack = definition != nullptr ? std::clamp<std::uint32_t>(definition->maxStack, 1, kMaxItemQuantity) : 1;
            const std::uint64_t stacks = (static_cast<std::uint64_t>(roll.quantity) + maxStack - 1) / maxStack;
            const bool fits = part.waiting.empty() && part.items.size() + stacks <= kMaxContainerSlots;
            std::vector<ItemInstance> instances = fits ? m_world.inventory().make_instances({roll}, nullptr) : std::vector<ItemInstance>{};
            if (!instances.empty() && part.items.size() + instances.size() <= kMaxContainerSlots) {
                for (ItemInstance& instance : instances) {
                    part.items.push_back(std::move(instance));
                }
                changed = true;
                ++made;
            } else {
                part.kept.push_back(roll);
                ++kept;
            }
        }
        if (changed) {
            part.rarity = rarity_of(part.items);
        }
    }
    std::size_t expired = 0;
    std::size_t emptied = 0;
    for (auto& [key, bag] : bags) {
        const bool empty = bag_empty(bag);
        const std::int64_t fastEnd = empty ? emptiedAt[key] : 0;
        if (bag.expiresAt <= wallNow || (fastEnd != 0 && fastEnd <= wallNow)) {
            // The end came while the backend was off (the wall clock of the rows): the lifetime, or the fast end of an empty bag
            // (finding LB-R9: a restart does not renew it).
            if (m_world.store().delete_loot_bag(key)) {
                const bool lifetime = bag.expiresAt <= wallNow;
                ++(lifetime ? expired : emptied);
                ++(lifetime ? m_counts.expired : m_counts.emptied);
            }
            continue;
        }
        bag.expires = now + std::chrono::milliseconds(bag.expiresAt - wallNow);
        if (empty) {
            // The stored fast end, unchanged. A bag of schema 10 has none: a new fast end from now.
            bag.emptied = fastEnd != 0 ? now + std::chrono::milliseconds(fastEnd - wallNow) : fast_end();
        }
        ++m_counts.loaded;
        place(std::move(bag));
    }
    m_counts.keptMade += made;
    if (!bags.empty()) {
        log::write("world: loot bags: %zu bag(s) of the store, %zu ended while the backend was off (%zu at the end after the last item)",
                   bags.size() - expired - emptied, expired + emptied, emptied);
    }
    if (made != 0 || kept != 0 || beyond != 0) {
        log::write("world: loot bags: %zu kept item(s) of the store got their instances, %zu stay kept (the item catalog does not have "
                   "them, or the part has 500 stacks; the client does not see them), %zu stored stack(s) beyond 500 stacks wait with all "
                   "their fields (the client does not see them)",
                   made, kept, beyond);
    }
}

void LootBags::place(LootBag bag) {
    const gamedata::LootContainer* data = container();
    const std::string key = bag.key;
    if (data != nullptr) {
        const Interactions::SpawnedContainer made =
            m_world.interactions().spawn_container(data->templateIndex, bag.position, 0.0F, "bag/" + bag.key);
        if (made.entity != nullptr) {
            bag.entity = made.entity->id;
            bag.generation = made.generation;
            ObjectBody& body = made.entity->object;
            for (const auto& [characterId, part] : bag.parts) {
                body.viewers.push_back(characterId);
            }
            body.lootDropSlot = data->lootDropSlot;
            // The LootDrop state is a state of the entity: the rarity of the only part (each eligible character observes the bag; a rarity
            // of an other part is a fact of its items: 0 for a bag of more than one character; a decision of an agent).
            body.rarity = bag.parts.size() == 1 ? bag.parts.begin()->second.rarity : 0;
            m_world.touch(made.entity->id, ComponentKey::lootDrop);
            m_byEntity[bag.entity] = key;
        } else {
            log::write("world: loot bags: the bag %s at %.2f %.2f %.2f has no server child (a place that is not of the world)", key.c_str(),
                       static_cast<double>(bag.position.x), static_cast<double>(bag.position.y), static_cast<double>(bag.position.z));
        }
    }
    m_due.emplace(bag.expires, Due{key});
    if (bag.emptied) {
        m_due.emplace(*bag.emptied, Due{key});
    }
    std::vector<std::string> characters;
    for (const auto& [characterId, part] : bag.parts) {
        m_byCharacter[characterId].insert(key);
        characters.push_back(characterId);
    }
    m_bags[key] = std::move(bag);
    for (const std::string& characterId : characters) {
        if (Entity* character = m_world.find_character(characterId)) {
            refresh(*character);
        }
    }
}

bool LootBags::end_bag(const std::string& key, const char* cause) {
    const auto found = m_bags.find(key);
    if (found == m_bags.end()) {
        return true;
    }
    if (!m_world.store().delete_loot_bag(key)) {
        ++m_counts.storeFailures;
        return false;
    }
    LootBag bag = std::move(found->second);
    m_bags.erase(found);
    if (bag.entity != 0) {
        m_byEntity.erase(bag.entity);
        // Each use of the bag ends (3746; live check L6: the panel closes), and each observer gets the stop of the record.
        m_world.interactions().remove_object(bag.entity);
    }
    std::uint32_t left = 0;
    for (const auto& [characterId, part] : bag.parts) {
        left += quantity_of(part);
        const auto own = m_byCharacter.find(characterId);
        if (own != m_byCharacter.end()) {
            own->second.erase(key);
            if (own->second.empty()) {
                m_byCharacter.erase(own);
            }
        }
        if (Entity* character = m_world.find_character(characterId)) {
            refresh(*character);
        }
    }
    log::write("world: loot bags: the bag %s (%s) ended: %s%s", key.c_str(), bag.source.c_str(), cause,
               left != 0 ? (", " + std::to_string(left) + " item(s) that no character took are gone").c_str() : "");
    return true;
}

void LootBags::entered(Entity& character) {
    if (character.kind != EntityKind::player) {
        return;
    }
    refresh(character);
}

void LootBags::left(Entity& character) {
    // The bags and their rows stay: the next entry gets their states.
    m_selectedAt.erase(character.characterId);
}

void LootBags::moved(Entity& character) {
    if (m_byCharacter.find(character.characterId) == m_byCharacter.end()) {
        return; // No bag of the character.
    }
    const auto last = m_selectedAt.find(character.characterId);
    if (last != m_selectedAt.end() && horizontal_distance(last->second, character.position) < loot_bag_rules::kStorageMoveDistance) {
        return;
    }
    // A walk from the place of the last selection (finding LB-R3): the nearest bags of now. The bags of the character only (MP-8).
    ++m_counts.moveSelections;
    refresh(character, false);
}

void LootBags::tick() {
    if (!m_loaded) {
        load();
    }
    const WorldClock::time_point now = m_world.clock().now();
    while (!m_due.empty() && m_due.begin()->first <= now) {
        const Due due = m_due.begin()->second;
        m_due.erase(m_due.begin());
        ++m_counts.dueLooks;
        const auto found = m_bags.find(due.bag);
        if (found == m_bags.end()) {
            continue;
        }
        const LootBag& bag = found->second;
        const bool expired = now >= bag.expires;
        const bool emptied = bag.emptied && now >= *bag.emptied;
        if (!expired && !emptied) {
            continue; // A due entry of a change before (a give into an empty bag).
        }
        if (!end_bag(due.bag, expired ? "its lifetime of the data passed" : "each part is empty")) {
            m_due.emplace(now + loot_bag_rules::kRetry, due);
            continue;
        }
        ++(expired ? m_counts.expired : m_counts.emptied);
    }
}

KillBag LootBags::begin_kill(const Entity& creature) {
    KillBag bag;
    const gamedata::LootContainer* data = container();
    const WorldClock::time_point now = m_world.clock().now();
    bag.key = state::new_uuid();
    bag.source = creature.creature.kind != nullptr ? creature.creature.kind->id : std::string("creature");
    bag.position = creature.position;
    bag.createdAt = wall_ms(now);
    const auto lifetime = std::chrono::milliseconds(data != nullptr ? static_cast<long long>(std::lround(data->lifetime * 1000.0F)) : 0LL);
    bag.expires = now + lifetime;
    bag.expiresAt = bag.createdAt + lifetime.count();
    return bag;
}

bool LootBags::stage_part(KillBag& bag, const Entity& character, const std::vector<GrantItem>& items, std::vector<GrantItem>* kept) {
    BagPart part;
    std::vector<GrantItem> unknown;
    part.items = m_world.inventory().make_instances(items, &unknown);
    // The Container of the client takes 500 stacks (finding LB-R4): a stack beyond it is a kept roll of the part (a load makes it when the
    // part has a place).
    while (part.items.size() > kMaxContainerSlots) {
        part.kept.push_back(roll_of(part.items.back()));
        part.items.pop_back();
    }
    // An item that the catalog does not have (the open point of 3.5b.2, a decision of the orchestrator; the rule of the chests, finding
    // CH-R3): the part keeps the roll in its rows, and the client does not see it. A load with a catalog that has the item makes it.
    for (GrantItem& grant : unknown) {
        part.kept.push_back(std::move(grant));
    }
    if (part_empty(part)) {
        return true; // Nothing to keep.
    }
    part.rarity = rarity_of(part.items);
    if (!m_world.store().write_loot_bag(character.characterId,
                                        make_row(bag.key, loot_bag_rules::kAiLoot, bag.source, bag.position, part, bag.createdAt, bag.expiresAt))) {
        return false;
    }
    if (!part.kept.empty()) {
        m_counts.keptItems += part.kept.size();
        const std::string mark = log::session_mark(character.owner, character.characterId);
        const log::Context logContext(mark);
        log::write("world: loot bags: the part of character %s of the bag %s keeps %s with no instance (the item catalog does not have it, "
                   "or the part has 500 stacks): the rows keep it, and the client does not see it",
                   character.characterId.c_str(), bag.key.c_str(), kept_text(part.kept).c_str());
    }
    if (kept != nullptr) {
        kept->insert(kept->end(), part.kept.begin(), part.kept.end());
    }
    bag.parts[character.characterId] = std::move(part);
    return true;
}

void LootBags::forget_part(KillBag& bag, const std::string& characterId) {
    bag.parts.erase(characterId);
}

void LootBags::finish_kill(KillBag& kill) {
    if (kill.parts.empty()) {
        return;
    }
    LootBag bag;
    bag.key = kill.key;
    bag.kind = loot_bag_rules::kAiLoot;
    bag.source = kill.source;
    bag.position = kill.position;
    bag.createdAt = kill.createdAt;
    bag.expiresAt = kill.expiresAt;
    bag.expires = kill.expires;
    bag.parts = std::move(kill.parts);
    ++m_counts.bags;
    m_counts.parts += bag.parts.size();
    std::string text;
    for (const auto& [characterId, part] : bag.parts) {
        text += (text.empty() ? "" : "; ") + characterId + ": " + item_text(part.items);
    }
    log::write("world: loot bags: the bag %s of the kill of %s at %.2f %.2f %.2f, %zu eligible character(s) (%s); it ends in %.0f s",
               bag.key.c_str(), bag.source.c_str(), static_cast<double>(bag.position.x), static_cast<double>(bag.position.y),
               static_cast<double>(bag.position.z), bag.parts.size(), text.c_str(),
               std::chrono::duration<double>(bag.expires - m_world.clock().now()).count());
    place(std::move(bag));
}

LootBag* LootBags::open_bag(const Entity& character) {
    if (!character.interaction || character.interaction->instance == 0) {
        return nullptr;
    }
    const auto entity = m_byEntity.find(character.interaction->instance);
    if (entity == m_byEntity.end()) {
        return nullptr;
    }
    const auto bag = m_bags.find(entity->second);
    if (bag == m_bags.end() || bag->second.generation != character.interaction->generation) {
        return nullptr;
    }
    return &bag->second;
}

InteractionVerdict LootBags::begin(Entity& character, const InteractionContext& context) {
    const char* what = context.option != nullptr ? context.option->name.c_str() : "use";
    if (container() == nullptr) {
        (void)refuse(BagRefusal::noData, character, what);
        return InteractionVerdict::refused;
    }
    const auto entity = m_byEntity.find(context.instance);
    const auto bag = entity != m_byEntity.end() ? m_bags.find(entity->second) : m_bags.end();
    // The end of the lifetime or the fast end of an empty bag (finding LB-R2): the bag is not there for a request.
    if (bag == m_bags.end() || bag->second.generation != context.generation || ended(bag->second)) {
        (void)refuse(BagRefusal::noBag, character, what);
        return InteractionVerdict::refused;
    }
    // The eligible characters only (MP-5): a known reference of the bag of a different character opens nothing.
    const auto part = bag->second.parts.find(character.characterId);
    if (part == bag->second.parts.end()) {
        (void)refuse(BagRefusal::notReceiver, character, what, bag->first);
        return InteractionVerdict::refused;
    }
    if (part->second.items.empty()) {
        (void)refuse(BagRefusal::empty, character, what, bag->first);
        return InteractionVerdict::refused;
    }
    return InteractionVerdict::accepted;
}

void LootBags::started(Entity& character, const InteractionContext& context) {
    LootBag* bag = open_bag(character);
    if (bag == nullptr || context.option == nullptr) {
        return;
    }
    if (context.option->name == kOpenOption) {
        // The panel: the client cleared its selected Container at its request; the full vector of the part goes after it (live check L5).
        // The open bag is the first of the selected storage keys (finding LB-R3): the selection is made again with the panel.
        refresh(character);
        const std::string mark = log::session_mark(character.owner, character.characterId);
        const log::Context logContext(mark);
        log::write("world: loot bags: character %s opened the bag %s: %s", character.characterId.c_str(), bag->key.c_str(),
                   item_text(bag->parts[character.characterId].items).c_str());
        return;
    }
    // Take All of the prompt (E): each item that fits. Looted: the client ends the use itself (live check L5: 2473 at once). Items left (a
    // full bag): the server ends the use (3746), and the bag stays with the rest.
    std::vector<std::pair<std::size_t, std::uint32_t>> wanted;
    const BagPart& part = bag->parts[character.characterId];
    for (std::size_t index = 0; index < part.items.size(); ++index) {
        wanted.emplace_back(index, part.items[index].quantity);
    }
    const BagResult result = transfer(character, *bag, wanted);
    if (!result.ok() || result.left != 0) {
        m_world.interactions().end_use(character, InteractionEnd::policyFailed);
    }
}

void LootBags::ended(Entity& character, const InteractionContext& context, InteractionEnd cause) {
    (void)context;
    (void)cause;
    set_panel(character, nullptr);
}

void LootBags::set_panel(Entity& character, const LootBag* bag) {
    Bag panel;
    if (bag != nullptr) {
        const auto part = bag->parts.find(character.characterId);
        if (part != bag->parts.end()) {
            for (std::size_t index = 0; index < part->second.items.size(); ++index) {
                BagSlot slot;
                slot.item = part->second.items[index];
                panel.slots.push_back(std::move(slot));
            }
        }
    }
    // An empty panel goes out only after a panel with items (no fragment in the first record of a character with no open bag).
    if (bag == nullptr && character.selectedContainer.slots.empty()) {
        return;
    }
    // Each change is a full vector (the reset revision): the client cleared its own one at the open.
    const Revision revision = m_world.touch(character.id, ComponentKey::selectedContainer);
    panel.reset = revision;
    for (BagSlot& slot : panel.slots) {
        slot.changed = revision;
    }
    character.selectedContainer = std::move(panel);
}

void LootBags::refresh(Entity& character, bool panel) {
    ++m_counts.refreshes;
    std::vector<LootTrackerEntry> entries;
    std::vector<std::pair<float, StorageKey>> keys;
    const gamedata::LootContainer* data = container();
    const LootBag* open = open_bag(character);
    const auto own = m_byCharacter.find(character.characterId);
    if (own != m_byCharacter.end()) {
        for (const std::string& key : own->second) {
            ++m_counts.refreshBags;
            const auto found = m_bags.find(key);
            const Entity* entity = found != m_bags.end() && found->second.entity != 0 ? m_world.find(found->second.entity) : nullptr;
            // Stage DG1: only the bags of the space of the character (each bag is in the main world).
            if (entity == nullptr || entity->space != character.space) {
                continue;
            }
            const LootBag& bag = found->second;
            const BagPart& part = bag.parts.at(character.characterId);
            LootTrackerEntry entry;
            entry.actor = entity->actor.bytes;
            entry.state = part.items.empty() ? loot_bag_rules::kLooted : loot_bag_rules::kRolled;
            entry.kind = bag.kind;
            entry.position = bag.position;
            // The rarity particle of the bag (client 0x36F6BC0 selects an entry of the list of the slice; live check: the light beam).
            const std::uint32_t particles = data != nullptr ? data->rarityParticles : 1U;
            entry.selector = static_cast<std::uint8_t>(std::min<std::uint32_t>(part.rarity, particles > 0 ? particles - 1 : 0));
            entries.push_back(entry);
            if (!part.items.empty()) {
                // The open bag first: it stays in the selected keys (finding LB-R3). Then the nearest bags.
                const float distance = &bag == open ? -1.0F : horizontal_distance(character.position, bag.position);
                keys.emplace_back(distance, StorageKey{entity->actor.bytes, part.items});
            }
        }
    }
    std::sort(entries.begin(), entries.end(), [](const LootTrackerEntry& a, const LootTrackerEntry& b) { return a.actor < b.actor; });
    if (entries != character.bagTracker) {
        character.bagTracker = std::move(entries);
        m_world.touch(character.id, ComponentKey::lootTracker);
    }
    // The GlobalStorage map: the nearest bags, 30 keys at most (the full map of the client).
    std::sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first < b.first : a.second.actor < b.second.actor; });
    if (keys.size() > loot_bag_rules::kStorageKeys) {
        keys.resize(loot_bag_rules::kStorageKeys);
    }
    std::vector<StorageKey> map;
    for (auto& [distance, key] : keys) {
        map.push_back(std::move(key));
    }
    if (!(map == character.storageMap)) {
        character.storageMap = std::move(map);
        m_world.touch(character.id, ComponentKey::globalStorage);
    }
    m_selectedAt[character.characterId] = character.position;
    // The panel of the open bag: the items of now.
    if (panel && open != nullptr) {
        set_panel(character, open);
    }
}

LootBag* LootBags::checked_open(Entity& character, const InteractionReference& container, BagRefusal& refusal) {
    // The reference names the selected Container of the character (live check L5): its actor and its group. The session checks the
    // facet target with the ids of the player slice. Never a bag: the bag is the open use of the character.
    if (container.actor != character.actor.bytes || container.group != character.id) {
        refusal = BagRefusal::wrongReference;
        return nullptr;
    }
    LootBag* bag = open_bag(character);
    // The end of the lifetime or an active fast end (finding LB-R2): refused before a transfer can clear the fast end, with either order of
    // the request and the due tick.
    if (bag == nullptr || ended(*bag)) {
        refusal = BagRefusal::notOpen;
        return nullptr;
    }
    InteractionEnd cause = InteractionEnd::policyFailed;
    if (!m_world.interactions().use_valid(character, cause)) {
        refusal = BagRefusal::notValid;
        return nullptr;
    }
    if (bag->parts.count(character.characterId) == 0) {
        refusal = BagRefusal::notReceiver;
        return nullptr;
    }
    refusal = BagRefusal::none;
    return bag;
}

BagResult LootBags::transfer(Entity& character, LootBag& bag, const std::vector<std::pair<std::size_t, std::uint32_t>>& wanted) {
    BagPart& part = bag.parts.at(character.characterId);
    // The stacks that go: the stored instance (a part of a stack: a copy with a new instance id; the rest keeps the id in the bag).
    std::vector<ItemInstance> offered;
    std::uint32_t asked = 0;
    for (const auto& [index, quantity] : wanted) {
        ItemInstance copy = part.items[index];
        if (quantity < copy.quantity) {
            copy.instance = make_instance_id();
            copy.quantity = quantity;
        }
        asked += quantity;
        offered.push_back(std::move(copy));
    }
    const Bag bagBefore = character.bag;
    std::vector<std::uint32_t> taken;
    BagPart next = part;
    state::Store::Batch batch(m_world.store());
    bool written = batch.open();
    std::uint32_t moved = 0;
    if (written) {
        const ItemResult given = m_world.inventory().receive(character.characterId, offered, taken);
        written = given.error != ItemError::storeFailed;
        if (given.error != ItemError::none && given.error != ItemError::storeFailed) {
            taken.assign(offered.size(), 0);
        }
    }
    if (written) {
        for (std::size_t entry = 0; entry < wanted.size(); ++entry) {
            next.items[wanted[entry].first].quantity -= taken[entry];
            moved += taken[entry];
        }
        next.items.erase(std::remove_if(next.items.begin(), next.items.end(), [](const ItemInstance& item) { return item.quantity == 0; }),
                         next.items.end());
        show_waiting(next);
        next.rarity = rarity_of(next.items);
        if (moved != 0) {
            // The take of the last item of the last part (finding LB-R9): the absolute fast end goes into the row, in the transaction of the
            // take. A restart keeps it.
            const bool last = container() != nullptr && part_empty(next)
                              && std::all_of(bag.parts.begin(), bag.parts.end(), [&](const auto& other) {
                                     return other.first == character.characterId || part_empty(other.second);
                                 });
            written = m_world.store().write_loot_bag(character.characterId,
                                                     make_row(bag.key, bag.kind, bag.source, bag.position, next, bag.createdAt, bag.expiresAt,
                                                              last ? wall_ms(fast_end()) : 0));
        }
    }
    written = written && batch.commit();
    if (!written) {
        character.bag = bagBefore;
        m_world.touch(character.id, ComponentKey::container);
        ++m_counts.storeFailures;
        return refuse(BagRefusal::storeFailed, character, "a pickup", bag.key);
    }
    if (moved == 0) {
        return refuse(BagRefusal::bagFull, character, "a pickup", bag.key);
    }
    part = std::move(next);
    ++m_counts.takes;
    m_counts.itemsTaken += moved;
    const std::string mark = log::session_mark(character.owner, character.characterId);
    const log::Context logContext(mark);
    log::write("world: loot bags: character %s took %u item(s) of the bag %s%s; the part has %s", character.characterId.c_str(), moved,
               bag.key.c_str(), moved < asked ? " (the bag of the character has no place for the rest)" : "", item_text(part.items).c_str());
    part_changed(bag, character);
    BagResult result;
    result.bag = bag.key;
    result.moved = moved;
    result.left = asked - moved;
    return result;
}

void LootBags::part_changed(LootBag& bag, Entity& character) {
    const gamedata::LootContainer* data = container();
    const bool empty = bag_empty(bag);
    if (empty && !bag.emptied && data != nullptr) {
        // The last item of the last part: FastKillSelfDelay of the slice (m_completelyLootedEvents of the server facet; game data). The
        // take wrote the same time into its row (finding LB-R9).
        bag.emptied = fast_end();
        m_due.emplace(*bag.emptied, Due{bag.key});
    } else if (!empty) {
        bag.emptied.reset();
    }
    if (bag.entity != 0 && bag.parts.size() == 1) {
        if (Entity* entity = m_world.find(bag.entity)) {
            const std::uint32_t rarity = bag.parts.begin()->second.rarity;
            if (entity->object.rarity != rarity) {
                entity->object.rarity = rarity;
                m_world.touch(entity->id, ComponentKey::lootDrop);
            }
        }
    }
    refresh(character);
}

BagResult LootBags::take_all(const std::string& characterId, const InteractionReference& container) {
    Entity* character = m_world.find_character(characterId);
    if (character == nullptr) {
        return refused_result(BagRefusal::notValid);
    }
    BagRefusal refusal = BagRefusal::none;
    LootBag* bag = checked_open(*character, container, refusal);
    if (bag == nullptr) {
        return refuse(refusal, *character, "Take All (2939)");
    }
    const BagPart& part = bag->parts.at(characterId);
    if (part.items.empty()) {
        return refuse(BagRefusal::empty, *character, "Take All (2939)", bag->key);
    }
    std::vector<std::pair<std::size_t, std::uint32_t>> wanted;
    for (std::size_t index = 0; index < part.items.size(); ++index) {
        wanted.emplace_back(index, part.items[index].quantity);
    }
    return transfer(*character, *bag, wanted);
}

BagResult LootBags::take_item(const std::string& characterId, const InteractionReference& container, const InstanceId& instance,
                              std::uint32_t quantity) {
    Entity* character = m_world.find_character(characterId);
    if (character == nullptr) {
        return refused_result(BagRefusal::notValid);
    }
    if (quantity == 0 || quantity > loot_bag_rules::kLargestQuantity) {
        return refuse(BagRefusal::badQuantity, *character, "the take of an item (2942)");
    }
    BagRefusal refusal = BagRefusal::none;
    LootBag* bag = checked_open(*character, container, refusal);
    if (bag == nullptr) {
        return refuse(refusal, *character, "the take of an item (2942)");
    }
    const BagPart& part = bag->parts.at(characterId);
    const auto item = std::find_if(part.items.begin(), part.items.end(), [&](const ItemInstance& stored) { return stored.instance == instance; });
    if (item == part.items.end()) {
        return refuse(BagRefusal::noItem, *character, "the take of an item (2942)", bag->key);
    }
    if (quantity > item->quantity) {
        return refuse(BagRefusal::badQuantity, *character, "the take of an item (2942)", bag->key);
    }
    return transfer(*character, *bag, {{static_cast<std::size_t>(item - part.items.begin()), quantity}});
}

BagResult LootBags::give_item(const std::string& characterId, const InteractionReference& container, const InstanceId& instance,
                              std::uint32_t quantity) {
    Entity* character = m_world.find_character(characterId);
    if (character == nullptr) {
        return refused_result(BagRefusal::notValid);
    }
    if (quantity == 0 || quantity > loot_bag_rules::kLargestQuantity) {
        return refuse(BagRefusal::badQuantity, *character, "a give into a bag (2801)");
    }
    BagRefusal refusal = BagRefusal::none;
    LootBag* bag = checked_open(*character, container, refusal);
    if (bag == nullptr) {
        return refuse(refusal, *character, "a give into a bag (2801)");
    }
    // The destination first (finding LB-R4): a part has 500 stacks at most (the Container of the client). A full part takes the item only
    // into a stack of the same item (same_stack) with a place for all of it; else nothing changes, and the item stays with the character.
    BagPart next = bag->parts.at(characterId);
    std::optional<std::size_t> merge;
    if (next.items.size() >= kMaxContainerSlots) {
        const ItemInstance* source = nullptr;
        for (const BagSlot& slot : character->bag.slots) {
            if (slot.item && slot.item->instance == instance) {
                source = &*slot.item;
                break;
            }
        }
        const gamedata::ItemCatalog* catalog = m_world.options().itemCatalog;
        const gamedata::ItemDefinition* definition = source != nullptr && catalog != nullptr ? catalog->find(source->itemId) : nullptr;
        const std::uint32_t maxStack = definition != nullptr ? std::clamp<std::uint32_t>(definition->maxStack, 1, kMaxItemQuantity) : 1;
        for (std::size_t index = 0; source != nullptr && index < next.items.size() && !merge; ++index) {
            if (same_stack(next.items[index], *source) && next.items[index].quantity + quantity <= maxStack) {
                merge = index;
            }
        }
        if (source != nullptr && !merge) {
            return refuse(BagRefusal::partFull, *character, "a give into a bag (2801)", bag->key);
        }
    }
    // One transaction: the item leaves the bag of the character (the lock rule of a drop, the protections of the item table) and goes into
    // its part of the open bag.
    const Bag bagBefore = character->bag;
    std::vector<ItemInstance> out;
    state::Store::Batch batch(m_world.store());
    bool written = batch.open();
    ItemResult released;
    if (written) {
        released = m_world.inventory().release_instance(characterId, instance, quantity, out);
        written = released.error != ItemError::storeFailed;
    }
    if (written && released.ok()) {
        for (ItemInstance& item : out) {
            if (merge) {
                next.items[*merge].quantity += item.quantity; // The stack of the part keeps its instance id.
            } else {
                next.items.push_back(std::move(item));
            }
        }
        next.rarity = rarity_of(next.items);
        written = m_world.store().write_loot_bag(characterId, make_row(bag->key, bag->kind, bag->source, bag->position, next, bag->createdAt,
                                                                       bag->expiresAt));
    }
    written = written && batch.commit();
    if (!written) {
        character->bag = bagBefore;
        m_world.touch(character->id, ComponentKey::container);
        ++m_counts.storeFailures;
        return refuse(BagRefusal::storeFailed, *character, "a give into a bag (2801)", bag->key);
    }
    if (!released.ok()) {
        BagResult result = refuse(released.error == ItemError::wrongInstance ? BagRefusal::noItem : BagRefusal::itemRefused, *character,
                                  "a give into a bag (2801)", bag->key);
        result.itemError = released.error;
        return result;
    }
    bag->parts[characterId] = std::move(next);
    m_counts.itemsGiven += quantity;
    log::write("world: loot bags: character %s put %u item(s) into the bag %s (it ends at its time)", characterId.c_str(), quantity,
               bag->key.c_str());
    part_changed(*bag, *character);
    BagResult result;
    result.bag = bag->key;
    result.moved = quantity;
    return result;
}

template <class Release>
BagResult LootBags::make_discard(Entity& character, Release&& release) {
    const gamedata::LootContainer* data = container();
    if (data == nullptr) {
        return refuse(BagRefusal::noData, character, "a discard");
    }
    if (character.space != kMainSpace) {
        // Stage DG1: a bag is an object of the main world (the bags of an instance come with its content); a discard in an instance
        // changes nothing (rule MP-5).
        return refuse(BagRefusal::notValid, character, "a discard in an instance");
    }
    const WorldClock::time_point now = m_world.clock().now();
    LootBag bag;
    bag.key = state::new_uuid();
    bag.kind = loot_bag_rules::kDiscarded;
    bag.source = "discard";
    bag.position = character.position;
    bag.createdAt = wall_ms(now);
    const auto lifetime = std::chrono::milliseconds(static_cast<long long>(std::lround(data->lifetime * 1000.0F)));
    bag.expires = now + lifetime;
    bag.expiresAt = bag.createdAt + lifetime.count();
    // One transaction: the items leave the character (its bag or its paperdoll) and go into its part of the new bag.
    const Bag bagBefore = character.bag;
    const Equipment equipmentBefore = character.equipment;
    BagPart part;
    state::Store::Batch batch(m_world.store());
    bool written = batch.open();
    ItemResult released;
    if (written) {
        released = release(part.items);
        written = released.error != ItemError::storeFailed;
    }
    // A part has 500 stacks at most (finding LB-R4): a discard of more stacks (1838 has 501 entries at most) changes nothing.
    const bool fits = part.items.size() <= kMaxContainerSlots;
    if (written && released.ok() && fits) {
        part.rarity = rarity_of(part.items);
        written = m_world.store().write_loot_bag(character.characterId,
                                                 make_row(bag.key, bag.kind, bag.source, bag.position, part, bag.createdAt, bag.expiresAt));
    }
    written = written && fits && batch.commit();
    if (!written) {
        const bool equipped = !(equipmentBefore.slots.size() == character.equipment.slots.size()
                                && std::equal(equipmentBefore.slots.begin(), equipmentBefore.slots.end(), character.equipment.slots.begin(),
                                              [](const auto& a, const auto& b) {
                                                  return a.first == b.first && a.second.item.has_value() == b.second.item.has_value();
                                              }));
        character.bag = bagBefore;
        character.equipment = equipmentBefore;
        m_world.touch(character.id, ComponentKey::container);
        m_world.touch(character.id, ComponentKey::paperdoll);
        if (equipped) {
            m_world.gear().refresh(character);
        }
        if (released.ok() && !fits) {
            return refuse(BagRefusal::partFull, character, "a discard"); // The batch is not committed: no row changed.
        }
        ++m_counts.storeFailures;
        return refuse(BagRefusal::storeFailed, character, "a discard");
    }
    if (!released.ok()) {
        BagResult result = refuse(BagRefusal::itemRefused, character, "a discard");
        result.itemError = released.error;
        return result;
    }
    const std::uint32_t moved = quantity_of(part.items);
    const std::string items = item_text(part.items);
    bag.parts[character.characterId] = std::move(part);
    ++m_counts.discards;
    ++m_counts.bags;
    ++m_counts.parts;
    const std::string mark = log::session_mark(character.owner, character.characterId);
    const log::Context logContext(mark);
    log::write("world: loot bags: character %s discarded %s: the Discarded bag %s at %.2f %.2f %.2f ends in %.0f s",
               character.characterId.c_str(), items.c_str(), bag.key.c_str(), static_cast<double>(bag.position.x),
               static_cast<double>(bag.position.y), static_cast<double>(bag.position.z),
               std::chrono::duration<double>(bag.expires - now).count());
    BagResult result;
    result.bag = bag.key;
    result.moved = moved;
    place(std::move(bag));
    return result;
}

BagResult LootBags::discard(const std::string& characterId, const std::vector<DropEntry>& entries) {
    Entity* character = m_world.find_character(characterId);
    if (character == nullptr) {
        return refused_result(BagRefusal::notValid);
    }
    return make_discard(*character, [&](std::vector<ItemInstance>& out) { return m_world.inventory().release(characterId, entries, out); });
}

BagResult LootBags::discard_equipped(const std::string& characterId, std::uint32_t slot, std::uint32_t quantity) {
    Entity* character = m_world.find_character(characterId);
    if (character == nullptr) {
        return refused_result(BagRefusal::notValid);
    }
    return make_discard(*character,
                        [&](std::vector<ItemInstance>& out) { return m_world.inventory().release_equipped(characterId, slot, quantity, out); });
}

LootBagsView LootBags::view(const std::string& characterId) const {
    LootBagsView view;
    view.data = container() != nullptr;
    const Entity* character = m_world.find_character(characterId);
    view.inWorld = character != nullptr;
    const WorldClock::time_point now = m_world.clock().now();
    if (character != nullptr && character->interaction && character->interaction->instance != 0) {
        if (const LootBag* bag = find_entity(character->interaction->instance); bag != nullptr && bag->generation == character->interaction->generation) {
            view.open = bag->key;
        }
    }
    const auto own = m_byCharacter.find(characterId);
    if (own == m_byCharacter.end()) {
        return view;
    }
    for (const std::string& key : own->second) {
        const LootBag* bag = find(key);
        if (bag == nullptr) {
            continue;
        }
        BagView entry;
        entry.key = bag->key;
        entry.kind = bag->kind;
        entry.source = bag->source;
        entry.entity = bag->entity;
        entry.position = bag->position;
        entry.distance = character != nullptr ? horizontal_distance(character->position, bag->position) : 0.0F;
        entry.secondsLeft = std::chrono::duration<double>(bag->expires - now).count();
        entry.emptied = bag->emptied.has_value();
        entry.items = bag->parts.at(characterId).items;
        entry.receivers = bag->parts.size();
        view.bags.push_back(std::move(entry));
    }
    std::sort(view.bags.begin(), view.bags.end(), [](const BagView& a, const BagView& b) { return a.secondsLeft < b.secondsLeft; });
    return view;
}

} // namespace soulwarden::backend::world

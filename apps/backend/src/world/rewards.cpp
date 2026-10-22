#include "world/rewards.h"

#include <algorithm>
#include <cmath>

#include "core/logging/log.h"
#include "world/combat.h"
#include "world/perk_roll.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

unsigned long long id_number(EntityId id) {
    return static_cast<unsigned long long>(id);
}

/** @return True when two texts are equal with no regard to the case of the letters A to Z. */
bool same_text(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto lower = [](char letter) { return letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter - 'A' + 'a') : letter; };
        if (lower(left[index]) != lower(right[index])) {
            return false;
        }
    }
    return true;
}

std::string drop_list(const std::vector<LootDrop>& drops) {
    std::string text;
    for (const LootDrop& drop : drops) {
        text += (text.empty() ? "" : ", ") + std::to_string(drop.quantity) + " " + drop.itemId;
        if (drop.gearScore) {
            text += " (gear score " + std::to_string(*drop.gearScore) + ")";
        }
    }
    return text.empty() ? "no item" : text;
}

} // namespace

std::chrono::milliseconds respawn_time(const gamedata::SpawnPoint& point, float unit) {
    float seconds = point.respawnMin;
    if (point.respawnMax > point.respawnMin) {
        seconds = point.respawnMin + std::clamp(unit, 0.0F, 1.0F) * (point.respawnMax - point.respawnMin);
    }
    if (!std::isfinite(seconds)) {
        seconds = 0.0F;
    }
    const auto time = std::chrono::milliseconds(static_cast<long long>(std::lround(std::max(seconds, 0.0F) * 1000.0F)));
    // A time of 0 (two values of 0) is no respawn at once: the body would leave in the next scan.
    return time.count() > 0 ? time : std::chrono::milliseconds(kShortestRespawn);
}

Rewards::Rewards(World& world, const gamedata::LootTables* tables) : m_world(world), m_tables(tables) {}

void Rewards::set_forced(const std::string& characterId, bool forced) {
    if (forced) {
        m_forced.insert(characterId);
    } else {
        m_forced.erase(characterId);
    }
}

void Rewards::hit(Entity& creature, const Entity& character, float damage) {
    if (character.kind != EntityKind::player || !(damage > 0.0F)) {
        return;
    }
    std::vector<CreditEntry>& credit = creature.creature.credit;
    auto entry = std::find_if(credit.begin(), credit.end(), [&](const CreditEntry& other) { return other.character == character.id; });
    if (entry == credit.end()) {
        credit.push_back(CreditEntry{character.id, character.characterId, 0.0F, {}});
        entry = credit.end() - 1;
    }
    entry->damage += damage;
    entry->last = m_world.clock().now();
}

std::uint32_t Rewards::uniform(std::uint32_t low, std::uint32_t high) {
    if (high <= low) {
        return low;
    }
    const std::uint64_t span = static_cast<std::uint64_t>(high) - low + 1;
    const auto offset = static_cast<std::uint64_t>(static_cast<double>(m_world.random_unit()) * static_cast<double>(span));
    return low + static_cast<std::uint32_t>(std::min<std::uint64_t>(offset, span - 1));
}

std::optional<std::uint32_t> Rewards::numeric(const std::string& tag, const LootContext& context) const {
    const std::string name = gamedata::lower_text(tag);
    if (name == reward_rules::kLevelTag) {
        return context.characterLevel;
    }
    if (name == reward_rules::kEnemyLevelTag) {
        return context.creatureLevel;
    }
    if (name == reward_rules::kContentLevelTag || name == reward_rules::kEnemyContentLevelTag) {
        // The content level of a kill: the lower one of the two levels (the lower applicable level
        // of the client, image RVA 0x368EBE0; loot.md 2.1).
        return std::min(context.characterLevel, context.creatureLevel);
    }
    if (name == reward_rules::kGlobalModTag) {
        return 0U; // No global roll modifier in this backend.
    }
    // A loot container (stage 3.5b.1, a rule of this project from the data: docs/systems/chests.md): the content level of the chest.
    if (context.container) {
        if (name == reward_rules::kPoiLevelTag || name == reward_rules::kContainerLevelTag) {
            return context.creatureLevel;
        }
        if (name == reward_rules::kPoiContentLevelTag) {
            return std::min(context.characterLevel, context.creatureLevel);
        }
    }
    return std::nullopt;
}

bool Rewards::has_tag(const std::string& tag, const LootContext& context) const {
    return std::any_of(context.tags.begin(), context.tags.end(), [&](const std::string& own) { return same_text(own, tag); });
}

std::vector<LootDrop> Rewards::roll(const gamedata::LootVitals& vitals, const LootContext& context, bool forced, float creditLoot) {
    std::vector<LootDrop> drops;
    if (!has_data() || !vitals.canDropLoot || !vitals.table) {
        return drops;
    }
    // The outer drop chance: the chance of the vitals row, the loot factor of the creature type, and the credit.
    float chance = vitals.dropChance * creditLoot;
    if (!vitals.ignoreDropModifier) {
        if (const gamedata::RewardModifier* type = m_tables->modifier(vitals.creatureType)) {
            chance *= type->lootDrop;
        }
    }
    if (!forced && !(m_world.random_unit() < chance)) {
        return drops;
    }
    ++m_counts.lootGates;
    ++m_counts.rolls;
    std::vector<std::uint32_t> path;
    roll_table(*vitals.table, context, Inherited{}, path, drops);
    return drops;
}

std::vector<LootDrop> Rewards::roll_root(std::uint32_t table, const LootContext& context) {
    std::vector<LootDrop> drops;
    if (!has_data() || table >= m_tables->data().tables.size()) {
        return drops;
    }
    ++m_counts.rolls;
    std::vector<std::uint32_t> path;
    roll_table(table, context, Inherited{}, path, drops);
    return drops;
}

std::vector<GrantItem> Rewards::grant_items(const std::vector<LootDrop>& drops, std::uint32_t characterLevel) {
    std::vector<GrantItem> grants;
    grants.reserve(drops.size());
    const gamedata::PerkData* perks = m_world.perk_data();
    const gamedata::ItemCatalog* catalog = m_world.options().itemCatalog;
    for (const LootDrop& drop : drops) {
        const gamedata::ItemDefinition* definition = catalog != nullptr ? catalog->find(drop.itemId) : nullptr;
        if (perks == nullptr || definition == nullptr || !can_roll_perks(*perks, *definition)) {
            grants.push_back(GrantItem{drop.itemId, drop.quantity, drop.gearScore});
            continue;
        }
        // An item with perk places: each instance gets its own roll (stage 1.3b, I3; world/perk_roll.h).
        // The gear score of the instance: the rule of the inventory for a loot item (Inventory::new_item).
        std::uint32_t gearScore = 0;
        if (definition->maxGearScore != 0) {
            const std::uint32_t standard = definition->gearScoreOverride != 0 ? definition->gearScoreOverride : definition->minGearScore;
            gearScore = std::clamp(drop.gearScore.value_or(standard), 1U, std::max(definition->maxGearScore, 1U));
        }
        for (std::uint32_t unit = 0; unit < drop.quantity; ++unit) {
            const PerkIds rolled = roll_perks(*perks, *definition, gearScore, characterLevel, [this] { return m_world.random_unit(); });
            grants.push_back(GrantItem{drop.itemId, 1, drop.gearScore, rolled});
        }
    }
    return grants;
}

void Rewards::roll_table(std::uint32_t index, const LootContext& context, Inherited inherited, std::vector<std::uint32_t>& path,
                         std::vector<LootDrop>& drops) {
    const gamedata::LootTable& table = m_tables->table(index);
    // The guard of the client (image RVA 0x368B070): a table that a path visits a second time gives nothing.
    if (std::find(path.begin(), path.end(), index) != path.end() || path.size() >= reward_rules::kLargestDepth) {
        ++m_counts.guards;
        return;
    }
    ++m_counts.tableVisits;
    // The conditions: a numeric tag of the roll adds its value to the bonus; of the other conditions
    // (the tags) one must be a tag of the roll.
    std::uint32_t bonus = context.rollBonus;
    bool tagCondition = false;
    bool tagPassed = false;
    for (const std::string& condition : table.conditions) {
        if (const std::optional<std::uint32_t> value = numeric(condition, context)) {
            bonus += *value;
        } else {
            tagCondition = true;
            tagPassed = tagPassed || has_tag(condition, context);
        }
    }
    if (tagCondition && !tagPassed) {
        return;
    }
    // The roll: from 1 to MaxRoll with the bonus setting of the table (loot.md 1.3).
    std::uint32_t roll = bonus;
    if (!table.conditionOverridesRoll) {
        const std::uint32_t top = table.maxRoll;
        switch (table.bonus) {
        case gamedata::RollBonus::addToRoll:
            roll = (top == 0 ? 1U : uniform(1, top)) + bonus;
            break;
        case gamedata::RollBonus::clampMax: {
            // ClampMax changes the lower end of the roll, and it does not add the bonus.
            const std::uint32_t low = std::min(bonus + 1, top);
            roll = top == 0 ? 0U : uniform(low, top);
            break;
        }
        case gamedata::RollBonus::ignoreBonus:
            roll = top == 0 ? 1U : uniform(1, top);
            break;
        }
    }
    // The entries that the roll reaches: the entries are in the sequence of their thresholds.
    std::size_t reached = 0;
    while (reached < table.entries.size() && table.entries[reached].threshold <= roll) {
        ++reached;
    }
    if (reached == 0) {
        return;
    }
    Inherited own = inherited;
    own.useLevelGearScore = own.useLevelGearScore || table.useLevelGearScore;
    own.gearScoreBonus += table.gearScoreBonus;
    path.push_back(index);
    if (table.all) {
        // AND: each entry that the roll reached, from the highest one down (the sequence of the client).
        for (std::size_t entry = reached; entry-- > 0;) {
            visit(table.entries[entry], context, own, path, drops);
        }
    } else {
        // OR: one entry with the highest reached threshold; one of equal thresholds at random.
        const std::uint32_t highest = table.entries[reached - 1].threshold;
        std::size_t first = reached - 1;
        while (first > 0 && table.entries[first - 1].threshold == highest) {
            --first;
        }
        const std::size_t chosen = first + uniform(0, static_cast<std::uint32_t>(reached - 1 - first));
        visit(table.entries[chosen], context, own, path, drops);
    }
    path.pop_back();
}

void Rewards::visit(const gamedata::LootEntry& entry, const LootContext& context, const Inherited& inherited,
                    std::vector<std::uint32_t>& path, std::vector<LootDrop>& drops) {
    // The quantity: a count of the item, the visits of a table, the selections of a bucket. A
    // largest quantity of 0 gives 0.
    const std::uint32_t quantity = entry.quantityMax == 0 ? 0U : uniform(entry.quantityMin, entry.quantityMax);
    Inherited own = inherited;
    if (entry.gearScoreMax != 0) {
        own.gearScoreMin = entry.gearScoreMin;
        own.gearScoreMax = entry.gearScoreMax;
    }
    switch (entry.kind) {
    case gamedata::LootEntryKind::item:
        add_item(entry.id, quantity, context, own, drops);
        break;
    case gamedata::LootEntryKind::table:
        for (std::uint32_t visit = 0; visit < quantity && drops.size() < reward_rules::kLargestDrops; ++visit) {
            roll_table(entry.index, context, own, path, drops);
        }
        break;
    case gamedata::LootEntryKind::bucket:
        for (std::uint32_t selection = 0; selection < quantity && drops.size() < reward_rules::kLargestDrops; ++selection) {
            roll_bucket(entry.index, context, own, drops);
        }
        break;
    }
}

void Rewards::roll_bucket(std::uint32_t index, const LootContext& context, const Inherited& inherited, std::vector<LootDrop>& drops) {
    const gamedata::LootBucket& bucket = m_tables->bucket(index);
    // The candidates whose tags fit the roll (image RVA 0x3693F10): a number range includes its two
    // ends; MatchOne: one tag is enough, else each tag. Then the odds of each candidate (0x368A3A0),
    // and one of the candidates that are left, each with the same chance (loot.md 1.4).
    std::vector<const gamedata::BucketCandidate*> left;
    for (const gamedata::BucketCandidate& candidate : bucket.candidates) {
        bool all = true;
        bool any = false;
        for (const gamedata::LootTag& tag : candidate.tags) {
            bool fits = false;
            if (tag.numeric) {
                const std::optional<std::uint32_t> value = numeric(tag.name, context);
                fits = value && *value >= tag.low && *value <= tag.high;
            } else {
                fits = has_tag(tag.name, context);
            }
            all = all && fits;
            any = any || fits;
        }
        if (!candidate.tags.empty() && !(candidate.matchOne ? any : all)) {
            continue;
        }
        if (candidate.odds < 1.0F && !(m_world.random_unit() < candidate.odds)) {
            continue;
        }
        left.push_back(&candidate);
    }
    if (left.empty()) {
        return;
    }
    const gamedata::BucketCandidate& chosen = *left[uniform(0, static_cast<std::uint32_t>(left.size() - 1))];
    const std::uint32_t quantity = chosen.quantityMax == 0 ? 0U : uniform(chosen.quantityMin, chosen.quantityMax);
    add_item(chosen.item, quantity, context, inherited, drops);
}

void Rewards::add_item(const std::string& itemId, std::uint32_t quantity, const LootContext& context, const Inherited& inherited,
                       std::vector<LootDrop>& drops) {
    if (quantity == 0 || drops.size() >= reward_rules::kLargestDrops) {
        return;
    }
    LootDrop drop;
    drop.itemId = itemId;
    drop.quantity = quantity;
    // The gear score of the item (a model of the client generation, loot.md 2.1; image RVA 0x367F590
    // and 0x368EBE0): the range of the entry; or, with UseLevelGS, the range of the tier of the item
    // at the lower one of the two levels, not above the loot ceiling of the creature level; or the
    // range of the item table. GSBonus adds to it. Not above the largest gear score of the item.
    const gamedata::ItemCatalog* catalog = m_world.options().itemCatalog;
    const gamedata::ItemDefinition* definition = catalog != nullptr ? catalog->find(itemId) : nullptr;
    if (definition != nullptr && definition->maxGearScore != 0) {
        std::uint32_t low = definition->minGearScore;
        std::uint32_t high = std::max(definition->maxGearScore, low);
        if (definition->gearScoreOverride != 0) {
            low = high = definition->gearScoreOverride;
        } else if (inherited.gearScoreMax != 0) {
            low = inherited.gearScoreMin;
            high = inherited.gearScoreMax;
        } else if (inherited.useLevelGearScore) {
            const std::uint32_t level = std::min(context.characterLevel, context.creatureLevel);
            if (const auto range = m_tables->gear_score(level, definition->tier)) {
                low = range->first;
                high = range->second;
                if (context.container) {
                    // A loot container (stage 3.5b.1): the container floor of the level is the smallest gear score (research
                    // chests-ground-loot.md, "Loot and gear score": ContainerLootGSFloor; the creature ceiling does not apply).
                    if (const auto floor = m_tables->container_floor(level); floor && *floor != 0) {
                        low = std::max(low, *floor);
                        high = std::max(high, low);
                    }
                } else if (const auto ceiling = m_tables->loot_ceiling(context.creatureLevel); ceiling && *ceiling != 0) {
                    high = std::min(high, *ceiling);
                    low = std::min(low, high);
                }
            }
        }
        const auto value = static_cast<std::int64_t>(uniform(low, std::max(low, high))) + inherited.gearScoreBonus;
        drop.gearScore = static_cast<std::uint32_t>(std::clamp<std::int64_t>(value, 1, definition->maxGearScore));
    }
    drops.push_back(std::move(drop));
}

std::vector<KillReward> Rewards::killed(Entity& creature) {
    std::vector<KillReward> rewards;
    if (!has_data()) {
        return rewards;
    }
    const LoadTimer timer(m_load);
    ++m_counts.kills;
    const Creature& body = creature.creature;
    const gamedata::LootVitals* vitals = m_tables->vitals_of(body.kind->vitals);
    const gamedata::CreditData& credit = m_tables->data().credit;
    const WorldClock::time_point now = m_world.clock().now();
    const auto timeLimit = std::chrono::milliseconds(static_cast<long long>(credit.timeLimit * 1000.0F));
    // The characters with the credit (a rule of this project with the credit data of the game).
    struct Creditor {
        Entity* entity;
        float damage;
    };
    std::vector<Creditor> creditors;
    for (const CreditEntry& entry : body.credit) {
        Entity* character = m_world.find(entry.character);
        const char* refused = nullptr;
        if (character == nullptr || character->kind != EntityKind::player || character->characterId != entry.characterId) {
            refused = "it is not in the world";
        } else if (entry.damage < credit.healthShare * creature.vitals.healthMax) {
            refused = "its damage is less than the part of the health of the credit";
        } else if (now - entry.last > timeLimit) {
            refused = "its last hit is older than the time of the credit";
        } else if (character->life.dead) {
            refused = "it is dead";
        } else if (m_world.creatures().behavior().ignores(character->characterId)) {
            refused = "the creatures ignore it";
        } else if (horizontal_distance(character->position, creature.position) > credit.range) {
            refused = "it is too far from the creature";
        }
        if (refused != nullptr) {
            log::write("world: reward: character %s gets nothing for the kill of creature %016llx (%s): %s",
                       entry.characterId.c_str(),
                       id_number(creature.id),
                       body.kind->id.c_str(),
                       refused);
            continue;
        }
        creditors.push_back(Creditor{character, entry.damage});
    }
    std::stable_sort(creditors.begin(), creditors.end(), [](const Creditor& left, const Creditor& right) { return left.damage > right.damage; });
    if (creditors.size() > credit.receivers) {
        creditors.resize(credit.receivers);
    }
    if (creditors.empty()) {
        ++m_counts.killsWithNoCredit;
        log::write("world: reward: the kill of creature %016llx (%s) gives nothing: no character has the credit",
                   id_number(creature.id),
                   body.kind->id.c_str());
        return rewards;
    }
    const gamedata::CreditData::Modifier& share = credit.modifiers[std::min(creditors.size(), credit.modifiers.size()) - 1];
    const std::string& creatureType = vitals != nullptr && !vitals->creatureType.empty() ? vitals->creatureType : body.kind->creatureType;
    const gamedata::RewardModifier* type = m_tables->modifier(creatureType);
    // The vitals categories of the victim (stage 1.8c: the VitalsCategories of its base vitals row, in lower case; a file of the loot tool
    // of before: the vitals row and the category row of the kind, the two categories that the backend knows).
    std::vector<std::string> categories = vitals != nullptr ? vitals->categories : std::vector<std::string>{};
    if (categories.empty()) {
        categories = {gamedata::lower_text(body.kind->vitals), gamedata::lower_text(body.kind->category)};
    }
    const gamedata::CombatTables* combat = m_world.options().combatTables;
    // The personal loot bag of the kill (stage 3.5b.2, docs/systems/loot-bags.md): the items of each character go into its own part of one
    // bag at the place of the creature. With no personal container in the interaction data (a file of format 1 or 2) the items go into
    // the bag of the character as in stage 3.5a.
    const bool bagged = m_world.loot_bags().container() != nullptr;
    KillBag bag = bagged ? m_world.loot_bags().begin_kill(creature) : KillBag{};
    for (const Creditor& creditor : creditors) {
        Entity& character = *creditor.entity;
        KillReward reward;
        reward.characterId = character.characterId;
        reward.character = character.id;
        const bool forced = this->forced(character.characterId);
        // The level of the reward: the level difference, and its largest reward level.
        const std::uint32_t own = character.progression.level;
        const auto [lowest, highest] = m_tables->disparity_range();
        const std::int64_t difference =
            std::clamp<std::int64_t>(static_cast<std::int64_t>(body.level) - static_cast<std::int64_t>(own), lowest, highest);
        const gamedata::LevelDisparity* disparity = m_tables->disparity(difference);
        std::uint32_t level = body.level;
        if (disparity != nullptr && disparity->rewardLevelDelta > 0 && own < level) {
            level = std::min(level, own + disparity->rewardLevelDelta);
        }
        level = std::clamp<std::uint32_t>(level, 1, static_cast<std::uint32_t>(m_tables->data().rewards.size()));
        const gamedata::KillReward& row = *m_tables->reward(level);
        if (vitals == nullptr || !vitals->noExperienceNoCoin) {
            float experience = static_cast<float>(row.experience) * share.experience * (type != nullptr ? type->experience : 1.0F);
            experience *= 1.0F + (disparity != nullptr ? disparity->experience : 0.0F);
            reward.experience = static_cast<std::uint32_t>(std::max(experience, 0.0F));
            // The coin: the chance of the creature type, or of the kill event.
            const float chance = type != nullptr && type->coinChance > 0.0F ? type->coinChance
                                                                             : m_tables->coin_chance(vitals != nullptr ? vitals->event : "");
            if (forced || m_world.random_unit() < chance) {
                const float coin = static_cast<float>(row.coin) * (type != nullptr ? type->coin : 1.0F) * share.coin;
                reward.coin = static_cast<std::uint64_t>(std::max(coin, 0.0F));
            }
        }
        // The loot: the tags of the vitals row and the two levels.
        if (vitals != nullptr) {
            LootContext context;
            context.characterLevel = own;
            context.creatureLevel = body.level;
            context.tags = vitals->tags;
            const std::uint64_t gates = m_counts.lootGates;
            reward.items = roll(*vitals, context, forced, share.loot);
            reward.lootGate = m_counts.lootGates != gates;
        }
        // The quest drops of the character (stage 1.8c, world/quests.h): an item of a possession task whose ItemDropVC names a vitals
        // category of the victim. They go with the loot (its part of the loot bag), in the transaction of the reward.
        const std::vector<QuestDrop> drops = m_world.quests().plan_drops(character, false, categories);
        for (const QuestDrop& drop : drops) {
            reward.items.push_back(LootDrop{drop.itemId, drop.quantity, std::nullopt});
        }
        // The reward of one character is one change (code review of stage 3.5a, finding F1; roadmap
        // rule MP-7): the experience, the mastery, and the items and the coin are written in one
        // transaction of the store (Store::Batch). A write that the store refuses gives no part of
        // the reward: the transaction writes nothing, and the entity gets its values of before
        // again (each system changed it after its own write).
        const Progression progressionBefore = character.progression;
        const Attributes attributesBefore = character.attributes;
        const Vitals vitalsBefore = character.vitals;
        const SkillSet skillsBefore = character.skills;
        const Bag bagBefore = character.bag;
        const Wallet walletBefore = character.wallet;
        state::Store::Batch batch(m_world.store());
        bool written = batch.open();
        std::uint32_t itemsLost = 0;
        std::size_t itemsGiven = 0;
        bool gained = false;
        bool granted = false;
        // The experience of the character, and the same amount for the mastery of the weapon in its
        // hand. A part that a rule refuses (a mastery that the tables do not have) is not given,
        // and the other parts are; a write that the store refuses ends the whole reward.
        if (written && reward.experience != 0) {
            const ProgressResult experience = m_world.progress().add_experience(character.characterId, reward.experience);
            written = experience.error != ProgressError::storeFailed;
            gained = experience.ok();
            const ItemInstance* weapon = Combat::weapon_in_hand(character);
            const gamedata::WeaponRow* row = weapon != nullptr && combat != nullptr ? combat->weapon_of(weapon->itemId) : nullptr;
            if (written && row != nullptr && !row->mastery.empty()) {
                const SkillResult mastered = m_world.skills().add_experience(character.characterId, row->mastery, reward.experience);
                written = mastered.error != ProgressError::storeFailed;
                if (mastered.ok()) {
                    reward.mastery = row->mastery;
                }
            }
        }
        // The items and the coin. Stage 3.5b.2: the items into the part of the character of the loot bag (a row of the batch), the coin
        // into its wallet.
        std::size_t itemsBagged = 0;
        if (written && (!reward.items.empty() || reward.coin != 0)) {
            std::vector<GrantItem> grants = grant_items(reward.items, character.progression.level);
            std::vector<GrantItem> lost;
            if (bagged && !grants.empty()) {
                // An item that the catalog does not have stays in the part as a kept roll (stage 2.2, the open point of 3.5b.2): not lost.
                std::vector<GrantItem> kept;
                written = m_world.loot_bags().stage_part(bag, character, grants, &kept);
                const auto part = bag.parts.find(character.characterId);
                itemsBagged = part != bag.parts.end() ? part->second.items.size() : 0;
                granted = written;
                if (written && reward.coin != 0) {
                    const ItemResult given = m_world.inventory().grant(character.characterId, {}, reward.coin);
                    written = given.error != ItemError::storeFailed;
                    granted = given.ok();
                }
            } else {
                const ItemResult given = m_world.inventory().grant(character.characterId, grants, reward.coin, &lost);
                written = given.error != ItemError::storeFailed;
                granted = given.ok();
                itemsGiven = grants.size() - std::min(grants.size(), lost.size());
            }
            for (const GrantItem& item : lost) {
                itemsLost += item.quantity;
            }
        }
        written = written && m_world.quests().write_drops(character, drops);
        written = written && batch.commit();
        if (!written) {
            m_world.loot_bags().forget_part(bag, character.characterId);
            character.progression = progressionBefore;
            character.attributes = attributesBefore;
            character.vitals = vitalsBefore;
            character.skills = skillsBefore;
            character.bag = bagBefore;
            character.wallet = walletBefore;
            for (const ComponentKey key : {ComponentKey::progression, ComponentKey::attribute, ComponentKey::vitals,
                                           ComponentKey::mana, ComponentKey::categorical, ComponentKey::points,
                                           ComponentKey::container, ComponentKey::currency}) {
                m_world.touch(character.id, key);
            }
            ++m_counts.refused;
            log::write("world: reward: the store did not write the reward of %s for the kill of creature %016llx: no part of it "
                       "is given",
                       character.name.c_str(),
                       id_number(creature.id));
            continue;
        }
        m_world.quests().drops_done(character, drops);
        m_counts.experience += gained ? reward.experience : 0;
        m_counts.masteryExperience += reward.mastery.empty() ? 0 : reward.experience;
        if (!reward.items.empty() || reward.coin != 0) {
            if (granted) {
                reward.itemsLost = itemsLost;
                reward.itemsInBag = static_cast<std::uint32_t>(itemsBagged);
                m_counts.items += itemsGiven;
                m_counts.itemsInBags += itemsBagged;
                m_counts.coin += reward.coin;
                m_counts.coinRewards += reward.coin != 0 ? 1 : 0;
            } else {
                reward.itemsLost = static_cast<std::uint32_t>(reward.items.size());
                reward.coin = 0;
            }
            m_counts.itemsLost += reward.itemsLost;
        }
        ++m_counts.credited;
        // The log line names the session that plays the character (the context keeps a reference to the text).
        const std::string mark = log::session_mark(character.owner, character.characterId);
        const log::Context context(mark);
        log::write("world: reward: %s gets %u experience%s%s, %llu.%02llu coin, and %s (%s) for the kill of creature %016llx "
                   "(%s, level %u; the reward level %u, %zu character(s) with the credit)%s",
                   character.name.c_str(),
                   reward.experience,
                   reward.mastery.empty() ? "" : " and the same for the mastery ",
                   reward.mastery.c_str(),
                   static_cast<unsigned long long>(reward.coin / 100),
                   static_cast<unsigned long long>(reward.coin % 100),
                   drop_list(reward.items).c_str(),
                   reward.lootGate ? "the loot chance passed" : "no loot this time",
                   id_number(creature.id),
                   body.kind->id.c_str(),
                   body.level,
                   level,
                   creditors.size(),
                   reward.itemsLost != 0 ? ", some items did not go into the bag" : "");
        if (reward.itemsInBag != 0) {
            log::write("world: reward: the items of %s go into its part of the loot bag %s (%u stack(s))", character.name.c_str(),
                       bag.key.c_str(), reward.itemsInBag);
        }
        rewards.push_back(std::move(reward));
    }
    // The bag with the committed parts goes into the world (its record for its eligible characters).
    if (bagged) {
        m_world.loot_bags().finish_kill(bag);
    }
    // The kill of the quests (stage 1.8c, finding G10 of research quest-engine-3.md): each character of the credit list gets the kill with
    // each vitals category of the victim and the kill event of its vitals row (TaskKillContribution, TaskGameEvent). A character that is
    // not in the list gets nothing. The result id is the creature and the revision of its death: a second delivery counts one time.
    std::vector<Entity*> credited;
    for (const Creditor& creditor : creditors) {
        credited.push_back(creditor.entity);
    }
    m_world.quests().killed(creature, credited, categories, vitals != nullptr && !vitals->event.empty() ? vitals->event : std::string(gamedata::kKillEvent),
                            (static_cast<std::uint64_t>(id_number(creature.id)) << 20) ^ m_world.revision());
    return rewards;
}

} // namespace soulwarden::backend::world

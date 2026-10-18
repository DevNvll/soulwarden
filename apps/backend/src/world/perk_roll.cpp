#include "world/perk_roll.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace soulwarden::backend::world {
namespace {

/** The client stops a flatten of nested buckets at this depth (0x448B950). */
constexpr unsigned kFlattenDepth = 50;
/** The smallest sum of weights that the flatten scales (FLT_EPSILON). */
constexpr float kSmallestSum = 1.19e-7F;
/**
 * The chance bonus of a bucket roll of an ordinary bucket: the chance of a nested bucket and of the bucket roll is PerkChance + 1 (always;
 * GeneratePerks 0x5C345F0 passes 1.0). A resource of a craft rolls with the bonus 0 (stage 3.7c: 0x5C345F0 passes 0 to 0x5CD1A20).
 */
constexpr float kBonus = 1.0F;
constexpr float kResourceBonus = 0.0F;

std::string lower(std::string_view text) {
    std::string result(text);
    for (char& letter : result) {
        if (letter >= 'A' && letter <= 'Z') {
            letter = static_cast<char>(letter + 32);
        }
    }
    return result;
}

/** @return The tags of a text with "+" between them, in lower case (or as they are, with `keepCase`). */
std::vector<std::string> tags_of(std::string_view text, bool keepCase = false) {
    std::vector<std::string> tags;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('+', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        if (end > start) {
            tags.push_back(keepCase ? std::string(text.substr(start, end - start)) : lower(text.substr(start, end - start)));
        }
        start = end + 1;
    }
    return tags;
}

using Weighted = std::vector<std::pair<const gamedata::PerkRow*, float>>;

void add(Weighted& list, const gamedata::PerkRow* perk, float weight) {
    for (auto& [other, value] : list) {
        if (other == perk) {
            value += weight;
            return;
        }
    }
    list.emplace_back(perk, weight);
}

/** Flatten of a bucket (client code 0x448B950): its perks with their weights; a nested bucket gets the weight of its entry. */
Weighted flatten(const gamedata::PerkData& data, const gamedata::PerkBucket& bucket, float scaleIn, unsigned depth, float bonus,
                 const RandomUnit& random) {
    Weighted entries;
    float sum = 0.0F;
    for (const gamedata::BucketEntry& entry : bucket.entries) {
        if (!entry.bucket) {
            if (const gamedata::PerkRow* perk = data.find(entry.name)) {
                add(entries, perk, entry.weight);
            }
            sum += entry.weight;
            continue;
        }
        const gamedata::PerkBucket* nested = data.bucket(entry.name);
        if (nested != nullptr && depth + 1 < kFlattenDepth && random() <= bucket.chance + bonus) {
            for (const auto& [perk, weight] : flatten(data, *nested, entry.weight, depth + 1, bonus, random)) {
                add(entries, perk, weight);
            }
            sum += entry.weight;
        }
    }
    if (sum >= kSmallestSum && scaleIn > 0.0F) {
        const float scale = scaleIn / sum;
        for (auto& [perk, weight] : entries) {
            weight *= scale;
        }
    }
    return entries;
}

/** One bucket roll (client code 0x448E4F0). @return The perk, or null. */
const gamedata::PerkRow* roll_bucket(const gamedata::PerkData& data,
                                     const gamedata::PerkBucket& bucket,
                                     std::uint32_t maxChannel,
                                     const std::vector<std::string>& itemClass,
                                     const std::vector<const gamedata::PerkRow*>& onItem,
                                     float bonus,
                                     const RandomUnit& random) {
    const std::string classText = [&] {
        std::string text;
        for (const std::string& tag : itemClass) {
            text += (text.empty() ? "" : "+") + tag;
        }
        return text;
    }();
    std::vector<std::string> taken;
    for (const gamedata::PerkRow* perk : onItem) {
        for (const std::string& label : tags_of(perk->labels)) {
            taken.push_back(label);
        }
    }
    Weighted candidates;
    for (const auto& [perk, weight] : flatten(data, bucket, -1.0F, 0, bonus, random)) {
        if (perk->channel > maxChannel || perk->deprecated || !gamedata::PerkData::allowed(*perk, classText)) {
            continue;
        }
        const std::vector<std::string> labels = tags_of(perk->labels);
        if (std::any_of(labels.begin(), labels.end(),
                        [&](const std::string& label) { return std::find(taken.begin(), taken.end(), label) != taken.end(); })) {
            continue;
        }
        candidates.emplace_back(perk, weight);
    }
    if (candidates.empty() || random() > bucket.chance + bonus) {
        return nullptr;
    }
    // The roll of an exclusive label, by the weight of the label (the override of the first item class that has one).
    if (!bucket.ignoreLabelWeights) {
        std::vector<std::pair<std::string, float>> labels;
        for (const auto& [perk, weight] : candidates) {
            for (const std::string& original : tags_of(perk->labels, true)) {
                const std::string label = lower(original);
                const auto known = std::find_if(labels.begin(), labels.end(), [&](const auto& entry) { return entry.first == label; });
                const gamedata::ExclusiveLabel* row = data.label(original);
                if (known != labels.end() || row == nullptr) {
                    continue;
                }
                float value = row->weight;
                for (const auto& [className, classWeight] : row->itemClasses) {
                    if (std::find(itemClass.begin(), itemClass.end(), lower(className)) != itemClass.end()) {
                        value = classWeight;
                        break;
                    }
                }
                labels.emplace_back(label, value);
            }
        }
        if (!labels.empty()) {
            float total = 0.0F;
            for (const auto& [label, weight] : labels) {
                total += weight;
            }
            const float pick = random() * total;
            float sum = 0.0F;
            std::string chosen = labels.back().first;
            for (const auto& [label, weight] : labels) {
                sum += weight;
                if (pick <= sum) {
                    chosen = label;
                    break;
                }
            }
            std::erase_if(candidates, [&](const auto& entry) {
                const std::vector<std::string> own = tags_of(entry.first->labels);
                return std::find(own.begin(), own.end(), chosen) == own.end();
            });
            if (candidates.empty()) {
                return nullptr;
            }
        }
    }
    float total = 0.0F;
    for (const auto& [perk, weight] : candidates) {
        total += weight;
    }
    const float pick = random() * total;
    float sum = 0.0F;
    for (const auto& [perk, weight] : candidates) {
        sum += weight;
        if (pick <= sum) {
            return perk;
        }
    }
    return candidates.back().first;
}

} // namespace

std::vector<const gamedata::PerkRow*> bucket_candidates(const gamedata::PerkData& data,
                                                        const gamedata::PerkBucket& bucket,
                                                        const gamedata::ItemDefinition& definition,
                                                        const std::vector<const gamedata::PerkRow*>& onItem,
                                                        std::uint64_t* visits) {
    const std::uint32_t maxChannel = definition.tier < data.tables().tiers.size() ? data.tables().tiers[definition.tier].maxChannel : 0;
    std::vector<std::string> taken;
    for (const gamedata::PerkRow* perk : onItem) {
        for (const std::string& label : tags_of(perk->labels)) {
            taken.push_back(label);
        }
    }
    std::vector<const gamedata::PerkRow*> found;
    std::vector<const gamedata::PerkBucket*> pending{&bucket};
    std::vector<const gamedata::PerkBucket*> seen;
    while (!pending.empty() && seen.size() < kFlattenDepth * 4) {
        const gamedata::PerkBucket* next = pending.back();
        pending.pop_back();
        if (std::find(seen.begin(), seen.end(), next) != seen.end()) {
            continue;
        }
        seen.push_back(next);
        if (visits != nullptr) {
            ++*visits;
        }
        for (const gamedata::BucketEntry& entry : next->entries) {
            if (entry.bucket) {
                if (const gamedata::PerkBucket* nested = data.bucket(entry.name)) {
                    pending.push_back(nested);
                }
                continue;
            }
            const gamedata::PerkRow* perk = data.find(entry.name);
            if (perk == nullptr || perk->channel > maxChannel || perk->deprecated || !gamedata::PerkData::allowed(*perk, definition.itemClass)
                || std::find(found.begin(), found.end(), perk) != found.end()) {
                continue;
            }
            const std::vector<std::string> labels = tags_of(perk->labels);
            if (std::any_of(labels.begin(), labels.end(),
                            [&](const std::string& label) { return std::find(taken.begin(), taken.end(), label) != taken.end(); })) {
                continue;
            }
            found.push_back(perk);
        }
    }
    return found;
}

bool can_roll_perks(const gamedata::PerkData& data, const gamedata::ItemDefinition& definition) {
    const gamedata::PerkLayout* layout = data.layout_of(definition.id);
    if (layout == nullptr) {
        return false;
    }
    const auto filled = [](const std::array<std::string, gamedata::kPerkPlaces>& places) {
        return std::any_of(places.begin(), places.end(), [](const std::string& place) { return !place.empty(); });
    };
    return filled(layout->fixed) || (layout->canHavePerks && filled(layout->buckets));
}

namespace {

/** The bucket types that the type gate of a slot roll lets roll (client 0x5C03DA0: Generated 0, Gem 1, Inherent 2, the charms 3 to 5). */
bool known_type(const std::string& type) {
    return type == "Generated" || type == "Gem" || type == "Inherent" || type == "OffensiveCharm" || type == "DefensiveCharm"
           || type == "SkillCharm";
}

/**
 * The generator of the client (GeneratePerks 0x5C345F0, one slot 0x5CD1A20 and 0x5CD1E70) for a loot item and a crafted item: the fixed
 * perks, the resources of a craft, the chances of the ordinary buckets, the count roll, and the slot rolls.
 * @param rollPerks The roll flag of the parameters (+0x28): false gives the fixed perks only (a loot item below MinLevelRollPerks).
 */
CraftPerkRoll generate(const gamedata::PerkData& data,
                       const gamedata::ItemDefinition& definition,
                       std::uint32_t gearScore,
                       bool rollPerks,
                       const CraftPerkInputs* craft,
                       const RandomUnit& random) {
    CraftPerkRoll roll;
    const gamedata::PerkLayout* layout = data.layout_of(definition.id);
    if (layout == nullptr) {
        return roll;
    }
    std::vector<const gamedata::PerkRow*> perks;
    std::size_t fixedCount = 0;
    for (const std::string& name : layout->fixed) {
        fixedCount += name.empty() ? 0 : 1;
        if (const gamedata::PerkRow* perk = name.empty() ? nullptr : data.find(name)) {
            perks.push_back(perk);
        }
    }
    std::vector<const gamedata::PerkBucket*> buckets;
    std::vector<std::string> bucketNames;
    for (const std::string& name : layout->buckets) {
        if (!name.empty()) {
            buckets.push_back(data.bucket(name));
            bucketNames.push_back(name);
        }
    }
    const std::uint32_t maxPerks = craft != nullptr ? craft->maxPerks : data.max_perks(definition.tier, gearScore);
    if (definition.tier == 5 && perks.size() == gamedata::kPerkPlaces && maxPerks < gamedata::kPerkPlaces && layout->forceRarity < 0) {
        perks.pop_back();
    }
    const auto finish = [&] {
        for (std::size_t place = 0; place < perks.size() && place < gamedata::kPerkPlaces; ++place) {
            roll.perks[place] = perks[place]->id;
        }
        return roll;
    };
    if (!rollPerks) {
        return finish();
    }
    const std::uint32_t maxChannel = definition.tier < data.tables().tiers.size() ? data.tables().tiers[definition.tier].maxChannel : 0;
    const std::vector<std::string> itemClass = tags_of(definition.itemClass);
    const auto inherent = [](const gamedata::PerkRow* perk) { return perk->type == "Inherent"; };
    bool hasInherent = std::any_of(perks.begin(), perks.end(), inherent);
    const auto has_gem = [&] { return std::any_of(perks.begin(), perks.end(), [](const gamedata::PerkRow* perk) { return perk->gem(); }); };
    // One slot (0x5CD1A20): CanAddPerk (0x5C03E20: the item can have perks, fewer than 5 and fewer than the largest count), the type gate
    // (0x5C03DA0: one Inherent perk; a type that is not a type of the client gives none), then the bucket roll.
    enum class Slot { stop, none, added };
    const auto slot = [&](const gamedata::PerkBucket& bucket, float bonus) {
        if (!layout->canHavePerks || perks.size() >= gamedata::kPerkPlaces || perks.size() >= maxPerks) {
            return Slot::stop;
        }
        if ((bucket.type == "Inherent" && hasInherent) || !known_type(bucket.type) || (craft != nullptr && bucket.type == "Gem" && has_gem())) {
            return Slot::none;
        }
        const gamedata::PerkRow* perk = roll_bucket(data, bucket, maxChannel, itemClass, perks, bonus, random);
        if (perk == nullptr || std::find(perks.begin(), perks.end(), perk) != perks.end()) {
            return Slot::none;
        }
        perks.push_back(perk);
        hasInherent = hasInherent || inherent(perk);
        return Slot::added;
    };
    const std::size_t ordinaryCount = buckets.size();
    // Stage 3.7c: the resources of a craft, in their order, with the chance bonus 0, before the count roll. With +0x2B false a resource
    // that gave a perk takes the first valid ordinary bucket of its type out.
    if (craft != nullptr) {
        for (const gamedata::PerkBucket* resource : craft->resources) {
            if (resource == nullptr) {
                roll.resourcePerks.emplace_back();
                continue;
            }
            const std::size_t before = perks.size();
            const Slot result = slot(*resource, kResourceBonus);
            roll.resourcePerks.push_back(result == Slot::added && perks.size() > before ? perks.back()->id : std::string());
            if (result == Slot::added && !craft->keepBuckets) {
                for (std::size_t index = 0; index < buckets.size(); ++index) {
                    if (buckets[index] != nullptr && buckets[index]->type == resource->type) {
                        buckets.erase(buckets.begin() + static_cast<std::ptrdiff_t>(index));
                        bucketNames.erase(bucketNames.begin() + static_cast<std::ptrdiff_t>(index));
                        break;
                    }
                }
            }
        }
    }
    if (buckets.empty()) {
        return finish();
    }
    // The count target: the largest count of the tier at the gear score, not above the fixed perks of the definition and its ordinary
    // buckets (before the resources).
    const std::size_t limit = fixedCount + ordinaryCount;
    const std::size_t target = std::min<std::size_t>(maxPerks, limit);
    roll.target = static_cast<std::uint32_t>(target);
    // The chance of each bucket while the count is below the target (an Inherent bucket only when the item has no Inherent perk and no
    // earlier Inherent bucket got a chance); the first G of them get the chance modifier of their type (an upgrade level of a craft).
    std::vector<float> chances(buckets.size(), 0.0F);
    const std::size_t first = perks.size();
    std::size_t count = first;
    bool inherentChance = hasInherent;
    for (std::size_t index = 0; index < buckets.size(); ++index) {
        const gamedata::PerkBucket* bucket = buckets[index];
        if (count >= target) {
            break;
        }
        const bool bucketInherent = bucket != nullptr && bucket->type == "Inherent";
        if (bucket == nullptr || (bucketInherent && inherentChance)) {
            continue;
        }
        float modifier = 0.0F;
        if (craft != nullptr && count - first < craft->guaranteed) {
            modifier = bucket->type == "Generated" ? craft->modifiers[0] : bucket->type == "Inherent" ? craft->modifiers[1]
                       : bucket->type == "Gem"     ? craft->modifiers[2]
                                                   : 0.0F;
        }
        chances[index] = modifier + bucket->chance;
        ++count;
        inherentChance = inherentChance || bucketInherent;
    }
    for (std::size_t index = 0; index < buckets.size(); ++index) {
        roll.chances.emplace_back(bucketNames[index], chances[index]);
    }
    // One count roll from the last bucket to the first.
    std::optional<std::size_t> last;
    for (std::size_t index = buckets.size(); index-- > 0;) {
        if (random() < chances[index]) {
            last = index;
            break;
        }
    }
    for (std::size_t index = 0; last && index <= *last; ++index) {
        if (chances[index] == 0.0F) {
            continue;
        }
        if (slot(*buckets[index], kBonus) == Slot::stop) {
            break;
        }
    }
    return finish();
}

} // namespace

PerkIds roll_perks(const gamedata::PerkData& data,
                   const gamedata::ItemDefinition& definition,
                   std::uint32_t gearScore,
                   std::uint32_t level,
                   const RandomUnit& random) {
    return generate(data, definition, gearScore, level >= data.tables().minLevelRollPerks, nullptr, random).perks;
}

CraftPerkRoll roll_craft_perks(const gamedata::PerkData& data,
                               const gamedata::ItemDefinition& definition,
                               std::uint32_t gearScore,
                               const CraftPerkInputs& inputs,
                               const RandomUnit& random) {
    return generate(data, definition, gearScore, true, &inputs, random);
}

} // namespace soulwarden::backend::world

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
/** The bonus of a roll of a loot item: the chance of a nested bucket and of a bucket roll is PerkChance + 1 (always). */
constexpr float kBonus = 1.0F;

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
Weighted flatten(const gamedata::PerkData& data, const gamedata::PerkBucket& bucket, float scaleIn, unsigned depth, const RandomUnit& random) {
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
        if (nested != nullptr && depth + 1 < kFlattenDepth && random() <= bucket.chance + kBonus) {
            for (const auto& [perk, weight] : flatten(data, *nested, entry.weight, depth + 1, random)) {
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
    for (const auto& [perk, weight] : flatten(data, bucket, -1.0F, 0, random)) {
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
    if (candidates.empty() || random() > bucket.chance + kBonus) {
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

PerkIds roll_perks(const gamedata::PerkData& data,
                   const gamedata::ItemDefinition& definition,
                   std::uint32_t gearScore,
                   std::uint32_t level,
                   const RandomUnit& random) {
    PerkIds result{};
    const gamedata::PerkLayout* layout = data.layout_of(definition.id);
    if (layout == nullptr) {
        return result;
    }
    std::vector<const gamedata::PerkRow*> perks;
    for (const std::string& name : layout->fixed) {
        if (const gamedata::PerkRow* perk = name.empty() ? nullptr : data.find(name)) {
            perks.push_back(perk);
        }
    }
    std::vector<const gamedata::PerkBucket*> buckets;
    for (const std::string& name : layout->buckets) {
        if (!name.empty()) {
            buckets.push_back(data.bucket(name));
        }
    }
    const std::uint32_t maxPerks = data.max_perks(definition.tier, gearScore);
    if (definition.tier == 5 && perks.size() == gamedata::kPerkPlaces && maxPerks < gamedata::kPerkPlaces && layout->forceRarity < 0) {
        perks.pop_back();
    }
    if (level >= data.tables().minLevelRollPerks && !buckets.empty()) {
        const std::size_t limit = perks.size() + buckets.size();
        const std::size_t target = std::min<std::size_t>(maxPerks, limit);
        const auto inherent = [](const gamedata::PerkRow* perk) { return perk->type == "Inherent"; };
        bool hasInherent = std::any_of(perks.begin(), perks.end(), inherent);
        // The chance of each bucket while the count is below the target.
        std::vector<float> chances(buckets.size(), 0.0F);
        std::size_t count = perks.size();
        bool inherentChance = hasInherent;
        for (std::size_t index = 0; index < buckets.size(); ++index) {
            const gamedata::PerkBucket* bucket = buckets[index];
            const bool bucketInherent = bucket != nullptr && bucket->type == "Inherent";
            if (bucket == nullptr || count >= target || (bucketInherent && inherentChance)) {
                continue;
            }
            chances[index] = bucket->chance;
            ++count;
            inherentChance = inherentChance || bucketInherent;
        }
        // One count roll from the last bucket to the first.
        std::optional<std::size_t> last;
        for (std::size_t index = buckets.size(); index-- > 0;) {
            if (random() < chances[index]) {
                last = index;
                break;
            }
        }
        const std::uint32_t maxChannel = definition.tier < data.tables().tiers.size() ? data.tables().tiers[definition.tier].maxChannel : 0;
        const std::vector<std::string> itemClass = tags_of(definition.itemClass);
        for (std::size_t index = 0; last && index <= *last; ++index) {
            if (chances[index] == 0.0F) {
                continue;
            }
            if (!layout->canHavePerks || perks.size() >= maxPerks) {
                break;
            }
            if (buckets[index]->type == "Inherent" && hasInherent) {
                continue;
            }
            if (const gamedata::PerkRow* perk = roll_bucket(data, *buckets[index], maxChannel, itemClass, perks, random)) {
                if (std::find(perks.begin(), perks.end(), perk) == perks.end()) {
                    perks.push_back(perk);
                    hasInherent = hasInherent || inherent(perk);
                }
            }
        }
    }
    for (std::size_t place = 0; place < perks.size() && place < gamedata::kPerkPlaces; ++place) {
        result[place] = perks[place]->id;
    }
    return result;
}

} // namespace soulwarden::backend::world

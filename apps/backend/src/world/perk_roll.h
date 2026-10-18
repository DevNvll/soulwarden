#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "gamedata/item_catalog.h"
#include "gamedata/perk_data.h"
#include "world/item.h"

// The perks of a new loot item (stage 1.3b, I3): the procedure of the client code, which has the
// complete perk generator (GeneratePerks at image RVA 0x5C345F0; research of stage 1.3b,
// perk-roll). The server of the game did the roll; the backend does it with the same steps and
// the cells of the game data (perk-data.json), and with the random numbers of the world.
//
//   1. A character below MinLevelRollPerks (7) gets the fixed perks of the item only.
//   2. The fixed perks (Perk1..5) and the buckets (PerkBucket1..5) of the item, in their
//      sequence (the legacy columns: the client setting javelin.perk-slots-enabled is off).
//   3. The largest perk count of the tier at the gear score (PerkData::max_perks). A tier 5 item
//      with five fixed perks, a smaller count, and no ForceRarity loses its last fixed perk.
//   4. target = min(largest count, fixed perks + buckets). Each bucket gets its PerkChance while
//      the count is below the target (an Inherent bucket only when the item has no Inherent perk
//      and no earlier Inherent bucket got a chance); the others get 0.
//   5. One count roll from the last bucket to the first: the first bucket whose random number is
//      below its chance is the last bucket that rolls. Each bucket to it with a chance rolls.
//   6. A bucket roll: the nested buckets flattened with their weights; the candidates with a
//      channel not above the MaxPerkChannel of the tier, no exclusive label of a perk of the
//      item, a class of the ItemClass of the perk, no class of its ExcludeItemClass, and not
//      deprecated; a roll of an exclusive label by its weight (item class overrides), unless the
//      bucket has IgnoreExclusiveLabelWeights; then a weighted pick of the candidates.
//
// Stage 3.7c: the same generator makes the perks of a crafted item (roll_craft_perks; research crafting-stage.md section 4, K8; live check
// L5b), with the inputs of the generation parameters of the client (constructor 0x5BAAEB0, GeneratePerks 0x5C345F0):
//   - the resources of the request (params +0x08: the perk items, in the order of the request): before the count roll, each one rolls its
//     PerkBucket with the chance bonus 0 (0x5CD1A20 with 0); a perk that it gives is appended. When the flag +0x2B is false, a resource that
//     gave a perk takes the first valid ordinary bucket of the type of its bucket out of the list (PerkItemsBucketPush of the recipe: the
//     backend passes the flag of the recipe as +0x2B; a decision of an agent from the research mapping and live check L5b);
//   - G (params +0x00, the upgrade level of Azoth): the first G ordinary buckets that get a chance get also the chance modifier of their
//     type (Perk, Attribute, Gem Slot Chance Modifier: 1 in the supported build), so that their chance is at least 1 (live check L5b:
//     "Very High Chance" for the first G rows of the preview);
//   - the crafting flag (+0x2A): the largest perk count comes from the list Crafting of the perk generation data (CraftingData).
// A crafted item has no level rule (the loot path reads the level of the player; research: do not copy it into crafting). The limit of the
// perk count is the fixed perks of the definition and its ordinary buckets before the resources. A Gem bucket gives no perk to an item that
// has a gem (a decision of an agent: an instance has one gem at most, the rule of stage 1.3b; a cut gem as a resource and the empty gem slot
// of an ordinary bucket would give two).
//
// Rules of this project (research [G]): no perk biasing (the client setting
// loot-biasing-perks-enabled needs the bias flag of an item descriptor, which a loot item does
// not have here), the labels in the sequence in which the candidates name them (the client walks
// a hash map), and no loot table override (no loot table of the data that the backend reads has
// PerkOverrides). The cost: the perks of the buckets of one item (a few hundred at most).

namespace soulwarden::backend::world {

/** The random numbers of a roll: a number in [0, 1). */
using RandomUnit = std::function<float()>;

/**
 * @return The perks of a new instance of an item, in their places (the fixed perks first). Empty places: no perk.
 * @param level The level of the character that gets the item.
 */
[[nodiscard]] PerkIds roll_perks(const gamedata::PerkData& data,
                                 const gamedata::ItemDefinition& definition,
                                 std::uint32_t gearScore,
                                 std::uint32_t level,
                                 const RandomUnit& random);

/** The inputs of the roll of a crafted item (stage 3.7c): the parameters of a craft of the client generator. */
struct CraftPerkInputs {
    /** The PerkBucket of each resource of the request, in the order of the request (null: a resource with no bucket: no roll). */
    std::vector<const gamedata::PerkBucket*> resources;
    /** G: the upgrade level of the request (0 to 5). */
    std::uint32_t guaranteed{0};
    /** The chance modifier of the types Generated, Inherent, and Gem (the perk generation data). */
    std::array<float, 3> modifiers{1.0F, 1.0F, 1.0F};
    /** +0x2B: true keeps the ordinary bucket of a resource that gave a perk; false takes the first one of its type out. */
    bool keepBuckets{true};
    /** The largest perk count of the tier at the gear score (the list Crafting). */
    std::uint32_t maxPerks{0};
};

/** The perks of a crafted item before the swap, and what the roll did (for a preview and a test). */
struct CraftPerkRoll {
    PerkIds perks{};
    /** The perks that the resources gave, in their order (empty: the resource gave none). */
    std::vector<std::string> resourcePerks;
    /** The ordinary buckets after the resources, with their chance of the count roll (0: no chance). */
    std::vector<std::pair<std::string, float>> chances;
    /** The count target: min(largest count, fixed perks + ordinary buckets before the resources). */
    std::uint32_t target{0};
};

/** @return The perks of a new crafted item (stage 3.7c): the fixed perks, the resources, and the ordinary buckets (see above). */
[[nodiscard]] CraftPerkRoll roll_craft_perks(const gamedata::PerkData& data,
                                             const gamedata::ItemDefinition& definition,
                                             std::uint32_t gearScore,
                                             const CraftPerkInputs& inputs,
                                             const RandomUnit& random);

/**
 * @return The perks that a bucket can give an item (stage 3.7c, the check of a perk item of a craft request): the perks of the bucket and of
 *         its nested buckets (each nested bucket, with no draw), with the filters of a bucket roll: the channel of the tier, the classes of
 *         the item, not deprecated, and no exclusive label of a perk of `onItem`. `visits` counts the buckets that the walk looked at.
 */
[[nodiscard]] std::vector<const gamedata::PerkRow*> bucket_candidates(const gamedata::PerkData& data,
                                                                      const gamedata::PerkBucket& bucket,
                                                                      const gamedata::ItemDefinition& definition,
                                                                      const std::vector<const gamedata::PerkRow*>& onItem,
                                                                      std::uint64_t* visits = nullptr);

/** @return True when an item has perk places in the data: a roll can give it perks. */
[[nodiscard]] bool can_roll_perks(const gamedata::PerkData& data, const gamedata::ItemDefinition& definition);

} // namespace soulwarden::backend::world

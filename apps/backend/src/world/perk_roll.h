#pragma once

#include <cstdint>
#include <functional>

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

/** @return True when an item has perk places in the data: a roll can give it perks. */
[[nodiscard]] bool can_roll_perks(const gamedata::PerkData& data, const gamedata::ItemDefinition& definition);

} // namespace soulwarden::backend::world

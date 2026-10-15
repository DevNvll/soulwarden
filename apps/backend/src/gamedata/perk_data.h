#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "gamedata/cell_values.h"

// The perk data: what the perks of an item instance need from the game files (stage 1.3b,
// protocol notes 4.23).
//
// A perk of an item on the wire is not an id: it is an index into a list that the client makes at
// its start (ReplicationDataManager::BuildPerkList, image RVA 0x448E310): entry 0 is "no perk",
// then the rows of the seven perk tables in a sequence that the tool computes (live check I1:
// the list of two starts of the client is equal to the list of the tool in each of its 1,822
// entries). The database has the PerkID text of a perk, and the index comes from this data at
// each load, as the index of an item comes from the item catalog.
//
// The file also has the affixes and the ability rows that the perks name (their cells), the perk
// buckets of a roll, the perk places of each item definition, the consumables, and the equip
// load data. The data has the cells of the game files and no rule: the rules are in the world
// (world/gear.h, world/consumables.h).
//
// The data is game data: the tool tools/gamedata/perks.sh makes the file from the game copy of
// the user, and the file stays outside the repository. Without the file the backend starts and
// works: an item has no perk, and a perk has no effect.

namespace soulwarden::backend::gamedata {

/** File name of the perk data in the game data directory of the backend. */
inline constexpr const char* kPerkDataFile = "perk-data.json";

/** Number of perk places of an item instance on the wire (a gem is one of them). */
inline constexpr std::size_t kPerkPlaces = 5;

/** One step of the gear score scaling of a perk: the slope from this gear score to the next step. */
struct ScalingStep {
    float gearScore{0.0F};
    float slope{0.0F};
};

/** One perk (a row of a perk table). */
struct PerkRow {
    /** The index of the perk in the list of the client (1 or more). */
    std::uint16_t index{0};
    /** The PerkID text: the key of a perk in the database. */
    std::string id;
    /** PerkType: "Inherent" (the attribute perk), "Generated", "Gem", ... */
    std::string type;
    /** ConditionEvent: "OnEquip", "OnActive", "OnUnsheathed", ... */
    std::string condition;
    /** ItemClass and ExcludeItemClass: tags with "+" between them. */
    std::string itemClass;
    std::string excludeItemClass;
    /** ExclusiveLabels: tags with "+" between them. Two perks of one item have no label in common. */
    std::string labels;
    /** ScalingPerGearScore. Empty: the potency of the perk is 1. */
    std::vector<ScalingStep> scaling;
    /** The affixes (AffixStatData) and the ability rows (AbilityData) of the perk. */
    std::vector<std::string> affixes;
    std::vector<std::string> abilities;
    /** Channel: a roll keeps the perk only when its channel is not above the largest channel of the tier of the item. */
    std::uint32_t channel{0};
    /** True when the row has a DeprecatedPerkId: no roll gives the perk. */
    bool deprecated{false};

    [[nodiscard]] bool gem() const {
        return type == "Gem";
    }
};

/** One entry of a perk bucket: a perk, or a bucket (the text "[PBID]" and its id in the game). */
struct BucketEntry {
    std::string name;
    bool bucket{false};
    float weight{1.0F};
};

/** One perk bucket (PerkBucketData). */
struct PerkBucket {
    std::string id;
    std::string type;
    /** PerkChance: the chance that the place of the bucket gets a perk. */
    float chance{0.0F};
    std::vector<BucketEntry> entries;
    /** IgnoreExclusiveLabelWeights and DisablePerkBiasing. */
    bool ignoreLabelWeights{false};
    bool disableBiasing{false};
};

/** One exclusive label (PerkExclusiveLabelData): its weight in the label roll, and the weights of some item classes. */
struct ExclusiveLabel {
    float weight{0.0F};
    std::vector<std::pair<std::string, float>> itemClasses;
};

/** One row of the perk swap table of the game (ItemPerkSwapData), in the sequence of its ID. */
struct PerkSwap {
    std::uint32_t id{0};
    /** True: the key is an item id. False: the key is item class tags with "+" between them. */
    bool byItem{false};
    std::string key;
    std::string oldPerk;
    std::string newPerk;
};

/** The largest perk count of a tier for the gear score ranges (PerkGenerationData of the base attributes file). */
struct PerkTier {
    std::uint32_t maxChannel{0};
    /** For each perk count: its gear score ranges. */
    std::vector<std::pair<std::uint32_t, std::vector<std::pair<std::uint32_t, std::uint32_t>>>> counts;
};

/** The perk places of an item definition (the item tables). */
struct PerkLayout {
    /** PerkSlot1..5: a perk (a fixed perk) or a bucket for each place. */
    std::array<std::string, kPerkPlaces> slots;
    /** Perk1..5 and PerkBucket1..5 of the row. */
    std::array<std::string, kPerkPlaces> fixed;
    std::array<std::string, kPerkPlaces> buckets;
    /** ForceRarity: -1 for a blank cell (the client computes the rarity from the perks). */
    int forceRarity{-1};
    bool canHavePerks{false};
};

/** One consumable (ConsumableItemDefinitions; its id is the ItemID of the item). */
struct ConsumableRow {
    std::string id;
    /** AddStatusEffects, and DurationOverrides for each one (0: the duration of the status row). */
    std::vector<std::string> effects;
    std::vector<float> durations;
    /** CooldownId and CooldownDuration (seconds). An empty id: no cooldown. */
    std::string cooldown;
    float cooldownTime{0.0F};
    /** RemoveStatusEffects and RemoveStatusEffectCategories. */
    std::vector<std::string> removeEffects;
    std::vector<std::string> removeCategories;
    /** DisableUseInCombat. */
    bool noCombat{false};
    /** True when the row has a SharedCooldownLocString: the client tells that a use puts the other items of the cooldown id on a part of the cooldown. */
    bool shared{false};
    /** RequiredLevel of the master item row of the consumable (file format 2; code review of stage 1.3b, finding F5). 0: none. */
    std::uint32_t level{0};
};

/** The equip load data of a player (the row Player of the encumbrance table). */
struct EquipLoadData {
    /** EquipLoadBaseMax (tenths), and the percents at which the medium, the heavy, and the overburdened load start. */
    float max{0.0F};
    float ratioMedium{0.0F};
    float ratioHeavy{0.0F};
    float ratioOverburdened{0.0F};
    /** The part of the damage (EquipLoadDamageMult*) and the factor of the heal (EquipLoadHealMult*) of each load. */
    std::array<float, 3> damage{};
    std::array<float, 3> heal{1.0F, 1.0F, 1.0F};
};

/** The data as plain values: the content of the file, and what a test sets. */
struct PerkTables {
    /** The perks in the sequence of the list of the client: perk K has the index K + 1. */
    std::vector<PerkRow> perks;
    std::unordered_map<std::string, CellValues> affixes;
    std::unordered_map<std::string, CellValues> abilities;
    std::vector<PerkBucket> buckets;
    std::vector<PerkLayout> layouts;
    /** Lower-case item id to the index of its layout. */
    std::unordered_map<std::string, std::size_t> items;
    std::vector<ConsumableRow> consumables;
    EquipLoadData equipLoad;
    /** Lower-case item id to the equip load of the item (tenths). */
    std::unordered_map<std::string, float> itemLoads;
    /** The exclusive labels by their id text. */
    std::unordered_map<std::string, ExclusiveLabel> labels;
    /** The perk swaps of the game, in the sequence of their ID. */
    std::vector<PerkSwap> swaps;
    /** The weapon id of each heart rune (lower case) to its EquipAbility rows (RuneDetonateT1: Ability_Rune_Detonate). */
    std::unordered_map<std::string, std::vector<std::string>> runes;
    /** The perk generation data: one entry for each tier (0 to 5), and the level from which a loot item gets perks. */
    std::vector<PerkTier> tiers;
    std::uint32_t minLevelRollPerks{7};
};

class PerkData {
public:
    /**
     * Reads the file of the tool. A file that is not complete is not used, as a whole: a member of a
     * wrong form, a number that is not finite, two perks with one id, a list of more than 65,535
     * entries, a bucket or a layout that names a perk or a bucket that the file does not have.
     * @return False when the file is not there or is not complete. `error()` then has the cause.
     */
    [[nodiscard]] bool load(const std::string& path);
    /** Sets the data with no file (tests), with the same check. The index of each perk is its position + 1. */
    [[nodiscard]] bool set(PerkTables tables);

    [[nodiscard]] bool loaded() const {
        return !m_tables.perks.empty();
    }
    [[nodiscard]] const std::string& error() const {
        return m_error;
    }
    [[nodiscard]] const PerkTables& tables() const {
        return m_tables;
    }
    /** @return The length of the list of the client, with entry 0. */
    [[nodiscard]] std::uint32_t list_size() const {
        return static_cast<std::uint32_t>(m_tables.perks.size() + 1);
    }

    /** @return The perk with this id (the compare ignores the case of the letters A to Z), or null. */
    [[nodiscard]] const PerkRow* find(std::string_view id) const;
    /** @return The perk with this index on the wire, or null. */
    [[nodiscard]] const PerkRow* by_index(std::uint32_t index) const;
    [[nodiscard]] const CellValues* affix(std::string_view id) const;
    [[nodiscard]] const CellValues* ability(std::string_view id) const;
    [[nodiscard]] const PerkBucket* bucket(std::string_view id) const;
    /** @return The perk places of an item, or null for an item with none. */
    [[nodiscard]] const PerkLayout* layout_of(std::string_view itemId) const;
    [[nodiscard]] const ConsumableRow* consumable(std::string_view itemId) const;
    /** @return The equip load of an item (tenths); 0 for an item with none. */
    [[nodiscard]] float item_load(std::string_view itemId) const;
    /** @return The EquipAbility rows of a heart rune by the id of its weapon row; empty for a weapon that is not a rune. */
    [[nodiscard]] const std::vector<std::string>& rune_abilities(std::string_view weaponId) const;
    /** @return The exclusive label with this id, or null. */
    [[nodiscard]] const ExclusiveLabel* label(std::string_view id) const;
    /**
     * @return The swaps whose old perk is this perk, in the sequence of their ID (the perk swap table of the game). Empty
     *         for a perk with no swap.
     */
    [[nodiscard]] const std::vector<const PerkSwap*>& swaps_of(std::string_view perk) const;
    /**
     * @return The new perk of the swap table of the game for a perk on an item, or null for no swap (client code at image
     *         RVA 0x44407E0, research of stage 1.3b): an ItemId row with the id of the item first; else the first ItemClass
     *         row, in the sequence of the ID, whose key has only classes of the item. The client loads a class row only when
     *         its classes are not a part of, and do not contain, the classes of an earlier row of the same old perk
     *         (loader 0x4431E80); `build_keys` keeps the same rows.
     */
    [[nodiscard]] const PerkRow* swap_of(const PerkRow& perk, std::string_view itemId, std::string_view itemClass) const;
    /**
     * Applies the swap table to the perk places of an item as the client does with each item of the bag and of the
     * paperdoll: a pass changes each perk one time at most, and the client makes up to three passes. A perk whose new
     * perk the item has already (not a gem and not a charm) goes out of its place. The other perks move forward and keep
     * their sequence.
     * @return The number of perks that changed or went out.
     */
    unsigned apply_swaps(std::array<std::string, kPerkPlaces>& perks, std::string_view itemId, std::string_view itemClass) const;
    /**
     * @return True when an item of this item class can have the perk (client code at image RVA 0x5CF5750): the perk has no
     *         ItemClass or the item has one class of it, and the item has no class of its ExcludeItemClass.
     */
    [[nodiscard]] static bool allowed(const PerkRow& perk, std::string_view itemClass);
    /**
     * @return The largest perk count of an item of a tier at a gear score (client code GetMaxPerksForGearScore, image RVA
     *         0x5C4D730): the largest count with a range that has the gear score; with none, the count of the nearest range
     *         (the client setting javelin.choose-next-valid-range-for-perk-count, default on); the gear score 0 gives 0.
     */
    [[nodiscard]] std::uint32_t max_perks(std::uint32_t tier, std::uint32_t gearScore) const;

    /**
     * @return The potency of a perk at a gear score: 1 plus, for each step of its scaling, the slope
     *         times the part of the gear score range of the step that the gear score covers, from the
     *         lowest gear score (client code at image RVA 0x3692600: the multiplier starts at 1 and adds
     *         each covered span times its slope). A perk with no scaling has the potency 1.
     * @param lowest The lowest gear score of a perk scaling (the minimum weapon gear score of the
     *        base attributes file, 100).
     */
    [[nodiscard]] static float potency(const PerkRow& perk, std::uint32_t gearScore, std::uint32_t lowest = 100);

private:
    void build_keys();

    PerkTables m_tables;
    std::unordered_map<std::string, std::size_t> m_byId;
    std::unordered_map<std::string, std::size_t> m_buckets;
    std::unordered_map<std::string, std::size_t> m_consumables;
    std::unordered_map<std::string, std::vector<const PerkSwap*>> m_swaps;
    /** The ItemClass rows that the client loads, by the old perk, and the ItemId rows by the old perk and the item. */
    std::unordered_map<std::string, std::vector<const PerkSwap*>> m_classSwaps;
    std::unordered_map<std::string, const PerkSwap*> m_itemSwaps;
    std::string m_error;
};

/** @return True when an item class text (tags with "+") has each tag of a key (tags with "+"); the compare ignores the case. */
[[nodiscard]] bool class_has_tags(std::string_view itemClass, std::string_view key);
/** @return True when an item class text has at least one tag of a list (tags with "+" or ","); an empty list: true. */
[[nodiscard]] bool class_has_any(std::string_view itemClass, std::string_view tags);

} // namespace soulwarden::backend::gamedata

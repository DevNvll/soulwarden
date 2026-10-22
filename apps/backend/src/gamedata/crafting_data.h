#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// The crafting data (stages 3.7a to 3.7c, docs/systems/crafting.md): the recipes of the CraftingRecipeData tables, the rows of the crafting
// station database, the ingredient categories of the items, the events of the recipes, and the resource items; from format 2 (stage 3.7b)
// also the RollBonus of each rank of each trade skill, the bit of each first-craft achievement, and the bonus item values of the items; from
// format 3 (stage 3.7c) the craft gear score values (the gear score bonus of each rank for each tier, the gear score values of an
// ingredient, the MaxCraftGS of an output), the currencies with the cap of their balance, the resource items with their perk bucket (the
// perk items of a request), and the crafting part of the perk generation data (the perk counts and the chance modifiers). The
// tool tools/gamedata/crafting.sh makes the file from the game copy of the user; the file is game data and stays outside the repository.
// Without the file each craft request is refused with no change, and the rest of the backend works.
//
// A recipe has its causes ("unsupported"): the rules of the data that the backend has no code for yet (a procedural result, perk items,
// an unlock, a currency ingredient). The crafting system refuses a recipe with a cause. The loader refuses the file as a whole when a part
// is not complete or not consistent (two recipes with one CRC-32, an ingredient type that is not a type of the client, a quantity of 0 for
// an item ingredient of a recipe with no cause, a station row with no type, a recipe with no cause and a rule of the data that the backend
// does not apply: code review of stage 3.7a, finding CR7A-R2). A file of format 1 (stage 3.7a) is read with the rules of stage 3.7a: its
// recipes with no cause have no event, no first-craft achievement, no cooldown, and no refining.
//
// The cost of a lookup does not depend on the size of the data (MP-8): the recipes by their CRC-32 (the id of request 2997), the stations
// by their name, and the members of a category by their CRC-32 are hash maps. Any thread after the load (read only).

namespace soulwarden::backend::gamedata {

class ItemCatalog;

/** File name of the crafting data in the game data directory of the backend. */
inline constexpr const char* kCraftingDataFile = "crafting-data.json";

/** The ingredient types of the client (enum 0x4F43EE7): the type of a recipe ingredient and of an entry of request 2997. */
enum class IngredientType : std::uint8_t {
    invalid = 0,
    item = 1,
    currency = 2,
    categoryAny = 3,
    categoryOnly = 4,
};

struct RecipeIngredient {
    IngredientType type{IngredientType::invalid};
    /** An item id (Item), a category id (Category_Only), or a currency id (Currency). */
    std::string id;
    std::uint32_t quantity{0};
};

/** One recipe of the CraftingRecipeData tables. */
struct Recipe {
    std::string id;
    /** The CRC-32 of the lower-case id: the recipe of request 2997 (client 0x4485AF0). */
    std::uint32_t crc{0};
    std::string table;
    std::string category;
    std::string group;
    bool known{false};
    bool listed{false};
    /** The trade skill (empty: none) and the rank that the recipe needs (RecipeLevel; client IsRecipeKnown 0x2E5E720). */
    std::string skill;
    std::uint32_t level{0};
    /** The station keys (lower case, StationType1 to 4): a station row must permit one of them (client 0x2E5ED00). */
    std::vector<std::string> stations;
    std::vector<RecipeIngredient> ingredients;
    /** The output item and its quantity for one job (0: a blank cell). */
    std::string item;
    std::uint32_t outputQty{0};
    bool procedural{false};
    bool refining{false};
    bool craftAll{false};
    bool temporary{false};
    /** CraftingFee (wire hundredths of a coin for one job) and UseCraftingTax (the tax path of the fee, client 0x2E513A0). */
    std::uint32_t fee{0};
    bool tax{false};
    std::uint32_t baseGearScore{0};
    std::uint32_t baseTier{0};
    /** Stage 3.7c (format 3): the MaxCraftGS of the output item (0: none): it replaces the cap of the expansion (client 0x2E57A70). */
    std::uint32_t maxCraftGS{0};
    /** DisallowBonusesToGS (client recipe +0xD): no rank bonus and no status bonus of the trade skill (the bonus items: client 0x2E57A70). */
    bool disallowBonuses{false};
    /** The perk options with the loader defaults of the client (client 0x4481730, research K4). */
    std::uint32_t perkItems{0};
    std::uint32_t attributeSlots{0};
    bool push{true};
    std::uint32_t perkCost{0};
    std::string event;
    /**
     * The achievement rules of the recipe (RequiredAchievementID, HiddenByAchievementID, UnlockedAchievementID, FirstCraftAchievementId)
     * and its world tags (RequiredWorldTags). The backend has no rule for them yet: a recipe with one of them has a cause (code review of
     * stage 3.7a, finding CR7A-R2: the loader compares them with the causes).
     */
    std::string requiredAchievement;
    std::string hiddenByAchievement;
    std::string unlockedAchievement;
    std::string firstCraftAchievement;
    std::string worldTags;
    std::string expansion;
    std::uint32_t cooldownSeconds{0};
    std::uint32_t cooldownQuantity{0};
    bool skipGrant{false};
    /**
     * The bonus item values of the recipe (stage 3.7b; client 0x5C52A40): BonusItemChance, and the bonus of a category material whose tier
     * is 1 to N above (BonusItemChanceIncrease) or below (BonusItemChanceDecrease) the base tier of the recipe.
     */
    float bonusChance{0.0F};
    std::vector<float> bonusIncrease;
    std::vector<float> bonusDecrease;
    /** The causes of the tool: the rules of the data that the backend has no code for. Empty: the backend crafts the recipe. */
    std::vector<std::string> unsupported;
};

/** The reward of a game event of a recipe (GameEventData; stage 3.7b). */
struct CraftEvent {
    /** The categorical progression of the trade skill experience (CategoricalProgressionId; empty: none) and the reward of one unit. */
    std::string progression;
    float reward{0.0F};
    /** UniversalExpAmount (the character experience) and TerritoryStanding: the backend gives neither (docs/systems/crafting.md). */
    float experience{0.0F};
    float standing{0.0F};
};

/** The bonus item values of an item (master items; stage 3.7b; client 0x5C52A40, 0x2E57A70). */
struct MaterialBonus {
    /** ExtraBonusItemChance (an integer of the client: it adds it as a chance of 1.0 for each unit). */
    std::int32_t extraChance{0};
    /** IngredientBonusPrimary (the first ingredient of a craft) and IngredientBonusSecondary (each other one): roll bonus points. */
    std::int32_t primary{0};
    std::int32_t secondary{0};
    /**
     * Stage 3.7c (format 3): IngredientGearScoreBaseBonus and IngredientGearScoreMaxBonus (client 0x2E57A70, the item +0x48 and +0x4C): a
     * selected member of a category ingredient of a procedural recipe adds them to the low and the high gear score of the craft.
     */
    std::int32_t gearScoreBase{0};
    std::int32_t gearScoreMax{0};
};

/** A resource item (ResourceItemDefinitions; stage 3.7c): the perk item of a craft request names it by the CRC-32 of its id. */
struct CraftResource {
    std::string id;
    /** PerkBucket: the bucket that the craft rolls for the resource (client 0x5C345F0: chance bonus 0, before the ordinary buckets). */
    std::string bucket;
    std::string gemPerk;
    std::string socket;
};

/** A currency of the recipes (stage 3.7c; a categorical progression of rank 0: Azoth_Currency) and the cap of its balance. */
struct CraftCurrency {
    std::string id;
    std::uint64_t cap{0};
};

/** One row of the crafting station database (sharedassets/genericassets/craftingstations.craftstationdb). */
struct CraftingStation {
    std::string name;
    /** The station keys that the row permits (CraftingTypes, lower case). */
    std::vector<std::string> types;
    float discount{0.0F};
    std::string mannequin;
};

class CraftingData {
public:
    /** Reads GAMEDATA_DIR/crafting-data.json. @return False (with error()) when the file is not there or is not complete. */
    bool load(const std::string& directory);
    /** Reads a document (the tests). @return False when it is not complete or not consistent. */
    bool set(const std::string& document);

    [[nodiscard]] bool loaded() const {
        return m_loaded;
    }
    [[nodiscard]] const std::string& error() const {
        return m_error;
    }
    [[nodiscard]] const std::vector<Recipe>& recipes() const {
        return m_recipes;
    }
    [[nodiscard]] const std::vector<CraftingStation>& stations() const {
        return m_stations;
    }
    /** @return The recipe of a CRC-32 (request 2997), or null. */
    [[nodiscard]] const Recipe* recipe(std::uint32_t crc) const;
    /** @return The recipe of an id text (any case; a command), or null. */
    [[nodiscard]] const Recipe* recipe_of(std::string_view id) const;
    /** @return The station row of a name (any case: the Assembly key), or null. */
    [[nodiscard]] const CraftingStation* station(std::string_view name) const;
    /** @return True when the station row permits a station key of the recipe (client 0x2E5ED00: membership, not the last digit). */
    [[nodiscard]] static bool permits(const CraftingStation& station, const Recipe& recipe);
    /** @return The item id of the member of a category with this CRC-32 (the item of an entry of request 2997), or an empty text. */
    [[nodiscard]] std::string member(std::string_view category, std::uint32_t itemCrc) const;
    /** @return The first member of a category in the byte order of the item ids (a command), or an empty text. */
    [[nodiscard]] std::string first_member(std::string_view category) const;
    /** @return The number of members of a category (0: no such category). */
    [[nodiscard]] std::size_t member_count(std::string_view category) const;
    /** Stage 1.8f: @return The members of a category (the CRC-32 of each item id to the id), or null: no such category. */
    [[nodiscard]] const std::unordered_map<std::uint32_t, std::string>* members(std::string_view category) const;
    /** @return The authored Assembly keys of the station slices with no row (KEY -> slices): the diagnostic of the tool. */
    [[nodiscard]] const std::map<std::string, std::vector<std::string>>& missing_stations() const {
        return m_missing;
    }
    /** @return The number of recipes with no cause (the recipes that the backend crafts). */
    [[nodiscard]] std::size_t supported_count() const {
        return m_supported;
    }
    [[nodiscard]] std::size_t category_count() const {
        return m_categories.size();
    }
    /** @return The format of the file (1: stage 3.7a; 2: stage 3.7b; 3: stage 3.7c). */
    [[nodiscard]] std::uint64_t format() const {
        return m_format;
    }
    /** @return The event of a recipe (any case), or null. */
    [[nodiscard]] const CraftEvent* event(std::string_view id) const;
    /** @return The RollBonus of a rank of a trade skill (any case): a rank above the table has the last row; no table: no value. */
    [[nodiscard]] std::optional<float> roll_bonus(std::string_view tradeskill, std::uint32_t rank) const;
    /** @return True when the file has the rank table of a trade skill (any case). */
    [[nodiscard]] bool has_ranks(std::string_view tradeskill) const;
    /** @return The bit (AchievementIndex) of a first-craft achievement of a recipe (any case), or no value. */
    [[nodiscard]] std::optional<std::uint32_t> achievement_index(std::string_view id) const;
    /** @return The bonus item values of an item (any case): 0 for an item with no row. */
    [[nodiscard]] MaterialBonus material(std::string_view itemId) const;
    /**
     * Code review of stage U4, finding U4-R1: the outputs of the recipes with no cause against the item catalog (after the load of both
     * files). An output with a gear score range and no override (MaxGearScore above 0, GearScoreOverride 0) needs the gear score rule of
     * stage 3.7c: the crafting file and the catalog do not agree (the tool gives such a recipe a cause). The file is then refused as a whole
     * (each craft request is refused, error() has the cause).
     * @return True when the outputs agree with the catalog (also for a file that is not loaded).
     */
    bool match_catalog(const ItemCatalog& catalog);
    /**
     * Code review of stage 3.7b, finding CR7B-R3: the first-craft achievements of the file against an other source of achievement bits (the
     * progression tables): an id with a different bit in the other source would give two bits of one fact. The file is then refused as a
     * whole (error() has the cause). @param other The bit of an achievement id in the other source, or no value.
     * @return True when the sources agree (also for a file that is not loaded).
     */
    bool match_achievements(const std::function<std::optional<std::uint32_t>(std::string_view)>& other);

    // Stage 3.7c (format 3).
    /** @return The resource item of a perk item of a request (the CRC-32 of its lower-case id), or null. */
    [[nodiscard]] const CraftResource* resource(std::uint32_t crc) const;
    /** @return The resource item of an id text (any case), or null. */
    [[nodiscard]] const CraftResource* resource_of(std::string_view id) const;
    /**
     * @return The largest gear score bonus of a rank of a trade skill for an output of a tier (MaxGearScoreBonusT2 to T5, else
     *         MaxGearScoreBonus: client 0x3CB2AA0); a rank above the table has the last row; no table: 0.
     */
    [[nodiscard]] std::uint32_t rank_gear_score(std::string_view tradeskill, std::uint32_t rank, std::uint32_t tier) const;
    /** @return The currency of an id (any case), or null. */
    [[nodiscard]] const CraftCurrency* currency(std::string_view id) const;
    /**
     * @return The largest perk count of a crafted item of a tier at a gear score (the list Crafting of the perk generation data; client
     *         0x5C4D730 with the crafting flag): the largest count with a range that has the gear score, else the count of the nearest
     *         range; the gear score 0 gives 0. No value: the file has no such data (format 2).
     */
    [[nodiscard]] std::optional<std::uint32_t> craft_max_perks(std::uint32_t tier, std::uint32_t gearScore) const;
    /** @return The chance that an upgrade level adds to an ordinary bucket of a type (Generated, Inherent, Gem; else 0: client 0x5C345F0). */
    [[nodiscard]] float chance_modifier(std::string_view bucketType) const;

private:
    bool parse(const std::string& text);

    bool m_loaded{false};
    std::string m_error;
    std::vector<Recipe> m_recipes;
    std::unordered_map<std::uint32_t, std::size_t> m_byCrc;
    std::vector<CraftingStation> m_stations;
    std::unordered_map<std::string, std::size_t> m_stationByName;
    /** The members of each category (lower case): the CRC-32 of each item id to the id. */
    std::unordered_map<std::string, std::unordered_map<std::uint32_t, std::string>> m_categories;
    /** The first member of each category (the members of the file are in the byte order of their ids). */
    std::unordered_map<std::string, std::string> m_firstMembers;
    std::map<std::string, std::vector<std::string>> m_missing;
    std::size_t m_supported{0};
    std::uint64_t m_format{0};
    /** Format 2: the events (lower case id), the RollBonus of each rank (lower case trade skill), the first-craft bits, the materials. */
    std::unordered_map<std::string, CraftEvent> m_events;
    std::unordered_map<std::string, std::vector<float>> m_ranks;
    std::unordered_map<std::string, std::uint32_t> m_achievements;
    std::unordered_map<std::string, MaterialBonus> m_materials;
    /** Format 3: the resources by the CRC-32 of their lower-case id, the rank gear score of each trade skill, the currencies, the perk counts. */
    std::vector<CraftResource> m_resources;
    std::unordered_map<std::uint32_t, std::size_t> m_resourceByCrc;
    std::unordered_map<std::string, std::vector<std::array<std::uint32_t, 5>>> m_rankGearScore;
    std::unordered_map<std::string, CraftCurrency> m_currencies;
    /** For each tier: the gear score ranges of each perk count (empty: no data). */
    std::vector<std::vector<std::pair<std::uint32_t, std::vector<std::pair<std::uint32_t, std::uint32_t>>>>> m_perkTiers;
    std::array<float, 3> m_chanceModifiers{1.0F, 1.0F, 1.0F};
};

} // namespace soulwarden::backend::gamedata

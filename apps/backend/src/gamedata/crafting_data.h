#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// The crafting data (stage 3.7a, docs/systems/crafting.md): the recipes of the CraftingRecipeData tables, the rows of the crafting station
// database, the ingredient categories of the items, the events of the recipes, and the resource items. The tool tools/gamedata/crafting.sh
// makes the file from the game copy of the user; the file is game data and stays outside the repository. Without the file each craft
// request is refused with no change, and the rest of the backend works.
//
// A recipe has its causes ("unsupported"): the rules of the data that the backend has no code for yet (a reward event, a procedural
// result, refining, a cooldown, perk items, an unlock). The crafting system refuses a recipe with a cause. The loader refuses the file as
// a whole when a part is not complete or not consistent (two recipes with one CRC-32, an ingredient type that is not a type of the
// client, a quantity of 0 for an item ingredient of a recipe with no cause, a station row with no type).
//
// The cost of a lookup does not depend on the size of the data (MP-8): the recipes by their CRC-32 (the id of request 2997), the stations
// by their name, and the members of a category by their CRC-32 are hash maps. Any thread after the load (read only).

namespace soulwarden::backend::gamedata {

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
    /** The perk options with the loader defaults of the client (client 0x4481730, research K4). */
    std::uint32_t perkItems{0};
    std::uint32_t attributeSlots{0};
    bool push{true};
    std::uint32_t perkCost{0};
    std::string event;
    std::string expansion;
    std::uint32_t cooldownSeconds{0};
    std::uint32_t cooldownQuantity{0};
    bool skipGrant{false};
    /** The causes of the tool: the rules of the data that the backend has no code for. Empty: the backend crafts the recipe. */
    std::vector<std::string> unsupported;
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
};

} // namespace soulwarden::backend::gamedata

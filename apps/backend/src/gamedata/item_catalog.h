#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// The item catalog: each item of the item tables of the game, with its index on the wire.
//
// An item on the wire is not an id. It is an index into a list that the client makes from
// its data tables at its start (ItemDataManager::m_indexToIdMap, protocol notes 4.10): entry 0
// is "no item", then each row of the 43 item tables in the byte order of the table names, then
// the rows of the 2 house item tables. Entry N of the list is the CRC-32 of the lower-case id
// of the item with the index N.
//
// The catalog also has the items of each backstory of the game (the table of the archetypes of
// the character creation): the items that a new character has. The table has backstories of
// tests and of later parts of the game too: the catalog has the list of the backstories that
// the character creation of the client can send, and a new character gets items only for one
// of them.
//
// The catalog is game data: the tool tools/gamedata/items.sh makes the file from the game copy
// of the user, and the file stays outside the repository. Without the file the backend starts
// and works, and it gives no item.

namespace soulwarden::backend::gamedata {

/** File name of the catalog in the game data directory of the backend. */
inline constexpr const char* kItemCatalogFile = "item-catalog.json";

/** One row of an item table. */
struct ItemDefinition {
    /** Index of the item in the list of the client. 0: no item. */
    std::uint32_t index{0};
    /** The ItemID text of the row. The key of an item in the database. */
    std::string id;
    /** Text key of the display name ("@1hSwordT1_MasterName"). */
    std::string nameKey;
    /** ItemType of the row ("Weapon", "Resource", ...). The client has no item class for an empty type. */
    std::string type;
    /** ItemClass of the row: tags with "+" between them. */
    std::string itemClass;
    std::uint32_t tier{0};
    /** Weight of one item in tenths. */
    std::uint32_t weight{0};
    /** Largest quantity of one stack. 1: the item does not stack. */
    std::uint32_t maxStack{1};
    /** Start value of the durability and of the maximum durability. 0: the item has no durability. */
    std::uint32_t durability{0};
    /** Gear score range. A maximum of 0: the item has no gear score. */
    std::uint32_t minGearScore{0};
    std::uint32_t maxGearScore{0};
    std::uint32_t gearScoreOverride{0};
    bool bindOnPickup{false};
};

/** One item of a backstory: one entry of the column InventoryItem of the backstory table. */
struct BackstoryItem {
    /** The ItemID text of the item. */
    std::string itemId;
    /** The quantity. 0: the character creation screen shows the item, and a new character does not get it. */
    std::uint32_t quantity{0};
    /** Gear score of the entry. 0: the value of the item table. */
    std::uint32_t gearScore{0};
};

/** @return The CRC-32 of an item id as the client computes it: only the letters A to Z go to lower case. */
[[nodiscard]] std::uint32_t item_id_crc(std::string_view id);

class ItemCatalog {
public:
    /** Reads the file of the tool. @return False when the file is not there or has a different form. */
    [[nodiscard]] bool load(const std::string& path);

    /** Sets the catalog with no file (tests). Item number K of the list gets the index K + 1. */
    void set(std::vector<ItemDefinition> items);

    [[nodiscard]] bool loaded() const {
        return !m_items.empty();
    }
    [[nodiscard]] std::size_t item_count() const {
        return m_items.size();
    }
    /** @return The length of the list of the client, with entry 0. */
    [[nodiscard]] std::uint32_t list_size() const {
        return static_cast<std::uint32_t>(m_items.size() + 1);
    }

    /** @return The item with this id (the compare ignores the case of the letters A to Z), or null. */
    [[nodiscard]] const ItemDefinition* find(std::string_view id) const;
    /** @return The item with this index on the wire, or null. */
    [[nodiscard]] const ItemDefinition* by_index(std::uint32_t index) const;

    /**
     * Finds the items for a search text. Each word of the text must be in the id, the type,
     * or the class of an item (the compare ignores the case of the letters A to Z). An item
     * with no type is not in the result: the client has no item class for it, and the world
     * gives no such item. An empty text finds each item.
     * @param offset Number of found items that the result does not have at its start.
     * @param limit Largest number of items of the result.
     * @param total When not null: the number of all items that the text finds.
     * @return The items in the sequence of the catalog.
     */
    [[nodiscard]] std::vector<const ItemDefinition*> search(std::string_view text,
                                                            std::size_t offset,
                                                            std::size_t limit,
                                                            std::size_t* total = nullptr) const;

    /**
     * @return The items of a backstory (the compare ignores the case of the letters A to Z), or
     *         null when the file has no such backstory. The name of an archetype of the
     *         character creation is the id of a backstory, for example "Archetype_Soldier".
     */
    [[nodiscard]] const std::vector<BackstoryItem>* backstory_items(std::string_view backstory) const;
    /** Sets the items of one backstory with no file (tests). */
    void set_backstory(std::string_view backstory, std::vector<BackstoryItem> items);
    [[nodiscard]] std::size_t backstory_count() const {
        return m_backstories.size();
    }

    /**
     * @return True when the catalog has the list of the backstories of the character creation.
     *         A file from before stage 1.5 does not have it: a new character then gets no item.
     */
    [[nodiscard]] bool has_creation_backstories() const {
        return m_hasCreationBackstories;
    }
    /**
     * @return True when the character creation of the client can send this backstory (the
     *         compare ignores the case of the letters A to Z): an archetype of the creation
     *         screen, or the default backstory of the client.
     */
    [[nodiscard]] bool creation_backstory(std::string_view backstory) const;
    /** Sets the backstories of the character creation with no file (tests). */
    void set_creation_backstories(const std::vector<std::string>& backstories);
    [[nodiscard]] std::size_t creation_backstory_count() const {
        return m_creationBackstories.size();
    }

private:
    void build_keys();

    std::vector<ItemDefinition> m_items;
    /** For each item: its id, its type, and its class in lower case, with a line end between them (the search text). */
    std::vector<std::string> m_searchTexts;
    /** Lower-case id to the position in the list. */
    std::unordered_map<std::string, std::size_t> m_byId;
    /** Lower-case backstory id to its items. */
    std::unordered_map<std::string, std::vector<BackstoryItem>> m_backstories;
    /** Lower-case ids of the backstories of the character creation. */
    std::unordered_set<std::string> m_creationBackstories;
    bool m_hasCreationBackstories{false};
};

} // namespace soulwarden::backend::gamedata

#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace soulwarden::backend::state {

/** Position of a character in the main world (meters). */
struct WorldPosition {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
};

/** One player character. Dates are ISO 8601 UTC text. */
struct Character {
    std::string characterId;
    std::string personaId;
    std::string worldId;
    std::string name;
    std::string createdDate;
    std::string modifiedDate;
    bool ftueCompleted{false};
    /**
     * Creation data that the client supplied in its create request (state/creation_data.h).
     * Kept unchanged. Empty for a character that the backend seeded.
     */
    std::string creationParams;
    /** Archetype of the creation data, for example "Archetype_Ranger". Empty when not known. */
    std::string archetype;
    /** Last known position in the world. Empty until the character was in the world. */
    std::optional<WorldPosition> position;
};

/** One player account. The Steam id of a login selects it. */
struct Account {
    /** Key of all player data. The client sends it in each service path and in its tokens. */
    std::string personaId;
    std::string accountId;
    /** Steam id 64 in decimal text. Empty until a login takes the account. */
    std::string steamId;
    /** Name that the Steam account had at its last login. UTF-8 text, can be empty. */
    std::string personaName;
    std::string createdDate;
    std::string lastLoginDate;
};

/** How a login got its account. */
enum class AccountOrigin {
    /** The Steam id had an account. */
    found,
    /** An account from before the account support had no Steam id. This login took it. */
    adopted,
    /** A new account was made. */
    created,
};

/** Result of an insert of a character. */
enum class InsertResult {
    inserted,
    /** A character of some account has the name. */
    nameInUse,
    /** The database did not accept the row for a different cause. */
    failed,
};

/** Result of the delete of a character. */
enum class DeleteResult {
    deleted,
    /** No active row has this character id. Nothing changed. */
    notFound,
    /** The database did not accept the delete. No row changed: the character and its rows are there. */
    failed,
};

/**
 * Containers of a character that hold items. The numbers are the container types of the client
 * (`eItemContainerType`, protocol notes 4.10): 1 is the bag, 2 is the paperdoll (the equipment;
 * the slot of such a row is a slot number of the paperdoll).
 */
inline constexpr int kContainerBag = 1;
inline constexpr int kContainerPaperdoll = 2;

/** Id of the coin in the table of the currencies. The amount is in hundredths of a coin. */
inline constexpr const char* kCurrencyCoin = "coin";

/** One item instance of a character: one stack in one slot of a container. */
struct ItemRow {
    /** 32 hexadecimal digits: the 16 bytes of the instance id on the wire. The key of the row. */
    std::string instanceId;
    int container{kContainerBag};
    std::uint32_t slot{0};
    /** The ItemID text of the item table, not the index on the wire. */
    std::string itemId;
    std::uint32_t quantity{0};
    std::uint32_t gearScore{0};
    std::uint32_t durability{0};
    std::uint32_t maxDurability{0};
    /** Bit 0: bound to the player. Bit 1: locked. */
    std::uint32_t flags{0};
    /** Order counter of the item in its container. */
    std::uint32_t chrono{0};
};

/**
 * The level, the experience, and the attribute points of a character (table
 * `character_progress`). A character with no row has these start values.
 */
struct ProgressRow {
    /** The level as the screen shows it: 1 is the first level. */
    std::uint32_t level{1};
    /** Total experience points. */
    std::uint32_t experience{0};
    /** The points that the character spent on each attribute: constitution, focus, strength, dexterity, intelligence. */
    std::array<std::uint32_t, 5> spent{};
};

/**
 * The weapon masteries, the trade skills, the nodes of the weapon trees, and the ability bars
 * of a character (tables `progressions`, `ability_nodes`, `ability_bars`). An id is the id on
 * the wire: the CRC-32 of the lower-case id text of the game table. The name is that text when
 * the backend knows it: it is for a reader of the database, and the backend does not read it.
 */
struct SkillRows {
    struct Rank {
        std::uint32_t id{0};
        std::string name;
        std::uint32_t rank{0};
        /** Experience in the rank. */
        std::uint64_t points{0};
    };
    struct Node {
        /** The point pool of the weapon. */
        std::uint32_t pool{0};
        std::uint32_t node{0};
        std::string name;
        std::uint32_t level{1};
    };
    struct Bar {
        /** The ability table of the weapon. */
        std::uint32_t table{0};
        /** 0 to 2. */
        std::uint32_t slot{0};
        std::uint32_t ability{0};
        std::string name;
    };
    std::vector<Rank> ranks;
    std::vector<Node> nodes;
    std::vector<Bar> bars;
};

/** Durable player state in one SQLite database. One writer connection, serialized by a mutex. */
class Store {
public:
    /**
     * One transaction for the writes of more than one system (code review of stage 3.5a, finding
     * F1: the reward of a kill is one change, roadmap rule MP-7). While a batch is open, each write
     * of `set_progress`, `write_skills`, and `write_items` of its thread is a savepoint of its
     * transaction, and a different thread waits for the end of the batch. `commit` writes all of
     * them; a batch that ends with no commit writes none of them.
     */
    class Batch {
    public:
        explicit Batch(Store& store);
        ~Batch();
        Batch(const Batch&) = delete;
        Batch& operator=(const Batch&) = delete;
        /** @return True when the transaction of the batch started. */
        [[nodiscard]] bool open() const {
            return m_open;
        }
        /** @return True when each write of the batch is in the database. False: none of them is. */
        [[nodiscard]] bool commit();

    private:
        Store& m_store;
        bool m_open{false};
        bool m_done{false};
    };

    Store() = default;
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    /** Opens the database and applies migrations. @return False on any failure. */
    [[nodiscard]] bool open(const std::string& path);

    /**
     * Finds the account of a Steam id and records the login. Without an account, the oldest
     * account with no Steam id gets this Steam id when `adoptOldAccount` permits it. Without
     * such an account, a new account is made. One transaction.
     * @param personaName New name of the account. An empty text keeps the stored name.
     * @param adoptOldAccount True lets this login take an account from before the account
     *        support. A login that names no account of its own must not take player data.
     * @return No value on a database failure.
     */
    [[nodiscard]] std::optional<Account> login_account(const std::string& steamId,
                                                       const std::string& personaName,
                                                       bool adoptOldAccount,
                                                       AccountOrigin& origin);

    [[nodiscard]] std::optional<Account> find_account(const std::string& personaId);
    /**
     * @return The account that a Steam id selects, or no value. It records no login and makes
     *         no account: the admin interface reads the account of its token with it.
     */
    [[nodiscard]] std::optional<Account> find_account_of_steam_id(const std::string& steamId);

    [[nodiscard]] std::vector<Character> list_characters(const std::string& personaId);
    [[nodiscard]] std::optional<Character> find_character(const std::string& characterId);

    /**
     * @return True when a character of any account has this name. The compare ignores the
     *         letter case (state/character_name.h).
     */
    [[nodiscard]] bool character_name_exists(const std::string& name);

    /**
     * Inserts a character when no character of any account has its name. The name check, the
     * insert, and the rows of the items that the character starts with are one transaction. A
     * database failure goes to the log.
     * @param items The items of the new character (world/inventory.h, start_items).
     */
    [[nodiscard]] InsertResult insert_character(const Character& character, const std::vector<ItemRow>& items = {});

    /**
     * Removes the row of a character, and its items and currencies, in one transaction. The
     * column deleted_date is from schema version 1: a row that an earlier backend marked with
     * it stays hidden. A database failure goes to the log.
     */
    [[nodiscard]] DeleteResult delete_character(const std::string& characterId);

    [[nodiscard]] bool set_ftue_completed(const std::string& characterId, bool completed);

    /** Keeps the last position of a character. @return False when no active row matched. */
    [[nodiscard]] bool set_character_position(const std::string& characterId, const WorldPosition& position);

    /** @return The item instances of a character, in the sequence of container and slot. */
    [[nodiscard]] std::vector<ItemRow> list_items(const std::string& characterId);

    /**
     * Changes the item instances of a character in one transaction: removes the rows with the
     * instance ids of `removed`, then writes each row of `rows` (a row with the instance id of
     * a row that is there replaces it). The character id is the owner key: a row of a different
     * character is not removed and not replaced.
     * @param coin When it has a value: the new coin of the character, in the same transaction
     *        (the loot of a kill).
     * @return False when the database did not accept the change. Then no row changed.
     */
    [[nodiscard]] bool write_items(const std::string& characterId,
                                   const std::vector<std::string>& removed,
                                   const std::vector<ItemRow>& rows,
                                   std::optional<std::uint64_t> coin = {});

    /** @return The amount of one currency of a character. 0 when the character has no row for it. */
    [[nodiscard]] std::uint64_t currency(const std::string& characterId, const std::string& currencyId);
    [[nodiscard]] bool set_currency(const std::string& characterId, const std::string& currencyId, std::uint64_t amount);

    /** @return The level, the experience, and the attribute points of a character: the start values when it has no row. */
    [[nodiscard]] ProgressRow progress(const std::string& characterId);
    /**
     * Writes the level, the experience, and the attribute points of a character.
     * @param coin When it has a value: the new coin of the character (the cost of an attribute
     *        reset). The two rows are one transaction.
     * @return False when the database did not accept the change. Then no row changed.
     */
    [[nodiscard]] bool set_progress(const std::string& characterId,
                                    const ProgressRow& row,
                                    std::optional<std::uint64_t> coin = {});

    /** @return The rows of the masteries, the trade skills, the tree nodes, and the ability bars of a character. */
    [[nodiscard]] SkillRows list_skills(const std::string& characterId);
    /**
     * Replaces the rows of the masteries, the trade skills, the tree nodes, and the ability
     * bars of a character with these rows, in one transaction.
     * @return False when the database did not accept the change. Then no row changed.
     */
    [[nodiscard]] bool write_skills(const std::string& characterId, const SkillRows& rows);

private:
    /** Starts a write: a transaction, or a savepoint in a batch. */
    bool begin_write();
    /** Ends a write: commits it when `ok`, if not rolls it back. @return True when it is written (in a batch: in the batch). */
    bool end_write(bool ok);

    [[nodiscard]] bool execute(const char* sql);
    [[nodiscard]] bool migrate();
    /** Inserts one item row of a character. The caller holds the mutex and has a transaction. */
    [[nodiscard]] bool insert_item(const std::string& characterId, const ItemRow& row);
    /** The caller holds the mutex. */
    [[nodiscard]] bool name_in_use(const std::string& name);

    /** Recursive: a batch holds it, and the writes of its thread lock it again (Store::Batch). */
    std::recursive_mutex m_mutex;
    /** Number of open batches (0 or 1). */
    unsigned m_batch{0};
    sqlite3* m_database{nullptr};
};

/** @return A random version-4 UUID in lower-case text. */
[[nodiscard]] std::string new_uuid();

/** @return The current UTC time as ISO 8601 text with milliseconds. */
[[nodiscard]] std::string now_iso8601();

} // namespace soulwarden::backend::state

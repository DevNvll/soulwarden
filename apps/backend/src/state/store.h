#pragma once

#include <array>
#include <atomic>
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
    /** The PerkID text of each of the five perk places (stage 1.3b, schema version 7). Empty: no perk. */
    std::array<std::string, 5> perks{};
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

/** One item instance of a rolled loot claim (stage 3.5b.1, table `loot_claim_items`): a roll that the character did not get yet. */
struct ClaimItemRow {
    std::string itemId;
    std::uint32_t quantity{0};
    /** The gear score of the instance. No value: the item has none. */
    std::optional<std::uint32_t> gearScore;
    /** The PerkID text of each of the five perk places. Empty: no perk. */
    std::array<std::string, 5> perks{};
};

/**
 * The loot claim of a character on a chest (stage 3.5b.1, table `loot_claims`; docs/systems/chests.md). The character id is the owner
 * key; the object key is the placement UUID of the chest. The times are Unix milliseconds (the wall clock: a reset goes on while the
 * backend is off).
 */
/**
 * A depleted gathering node of the world (stage 3.6a, table `gathering_nodes`; docs/systems/gathering.md). The node is a value of the
 * world, not of a character: one row for each node that waits for its respawn.
 */
struct GatheringNodeRow {
    /** The durable key of the node: a placement UUID, or "REGION#RECORD" of a distribution file. */
    std::string key;
    /** The source of its identity and its version ("capital", or "distribution:" and the version of the region file). */
    std::string source;
    /** The gatherable row (its id text). */
    std::string row;
    std::uint64_t generation{0};
    /** The respawn on the run clock of the world (nanoseconds; world_time.run_ns): the downtime of the backend does not count. */
    std::uint64_t respawnRunNs{0};
    /** Unix milliseconds of the depletion. */
    std::int64_t depletedAt{0};
};

/**
 * The cooldown of a recipe of a character (stage 3.7b, table `craft_cooldowns`; docs/systems/crafting.md): the crafts in the window of the
 * recipe (CooldownQuantity at most) and the end of the window on the run clock of the world (nanoseconds; world_time.run_ns: the downtime
 * of the backend does not count). The character id is the owner key: the rows go with the character.
 */
struct CraftCooldownRow {
    /** The recipe id text (RecipeID of the game table). */
    std::string recipe;
    std::uint32_t used{0};
    std::uint64_t endRunNs{0};
};

/**
 * One task of a saved quest (stage 1.8a, table `character_quest_tasks`; docs/systems/quests.md): the index of the task in the tree of the
 * objective, its id and CRC-32 (a check against the game data at the load), its progress, its state, and the start of a timer (Unix
 * nanoseconds; 0: none).
 */
struct QuestTaskRow {
    std::uint32_t index{0};
    std::string task;
    std::uint32_t crc{0};
    std::uint32_t progress{0};
    std::uint32_t state{0};
    std::uint64_t startNs{0};
};

/**
 * One saved quest of a character (stage 1.8a, table `character_quests`): the instance id (a full u64: an 8-byte blob), the objective id
 * and its CRC-32, the time of the grant (Unix nanoseconds), the territory of the grant, the parent instance, the source of the grant (a
 * text: "territory", "backstory", "command"), and its tasks. The character id is the owner key: the rows go with the character.
 */
struct QuestRow {
    std::uint64_t instance{0};
    std::string objective;
    std::uint32_t objectiveCrc{0};
    std::uint64_t acceptNs{0};
    std::uint32_t originTerritory{0};
    std::uint64_t parent{0};
    std::string source;
    std::vector<QuestTaskRow> tasks;
};

/**
 * One grant of a quest of a source that comes one time (stage 1.8a, table `character_quest_grants`): the start objective of a territory
 * (source "territory", key the territory id) or of a backstory template (source "backstory", key the template id). `state` is "pending"
 * (the next world entry gives it) or "granted" (with the instance).
 */
struct QuestGrantRow {
    std::string source;
    std::string key;
    std::string objective;
    std::string state;
    std::uint64_t instance{0};
};

/**
 * The undo of an achievement action of a quest task (stage 1.8a, table `character_quest_undo`): the action of a reset, an abandon, or the
 * end of the quest ("lock": a temporary unlock and an unlock with no StayActive; "unlock": a lock with no StayActive; research
 * quest-engine.md Q1). Stage 1.8a only writes the rows: the abandon and the reset are stage 1.8c.
 */
struct QuestUndoRow {
    std::uint64_t instance{0};
    std::uint32_t index{0};
    std::string action;
    std::string achievement;
};

/**
 * The items that an AwardOnlyOnce task of an objective gave to a character (stage 1.8c, table `character_quest_awards`; client 0x825EEF0,
 * 0x825F8F0): the objective (its id, not an instance: the row survives a reset), the task index, and the number of items.
 */
struct QuestAwardRow {
    std::string objective;
    std::uint32_t index{0};
    std::uint32_t amount{0};
};

/**
 * Stage 1.8d (table `character_quest_repeats`, schema 17): a completed repeatable objective with a schedule of the data. Its completion
 * achievement goes at `resetNs` (the start of the next cycle of its schedule, on the world clock): then the person offers it again.
 */
struct QuestRepeatRow {
    std::string objective;
    /** The completions of the objective (the generation of the repeat). */
    std::uint32_t completions{0};
    std::uint64_t completedNs{0};
    std::uint64_t resetNs{0};
};

/** The rows of a new character that the create route writes in the transaction of the character (stage 1.8a: the start of the quests). */
struct NewCharacterRows {
    /** The achievements of the character (the archetype achievement of its backstory row). */
    std::vector<std::string> achievements;
    /** The pending grants (the start objective of the start territory). */
    std::vector<QuestGrantRow> grants;
};

struct LootClaimRow {
    std::string objectKey;
    /** 1 Rolled (the roll is kept: `items`), 2 Looted (the InstancedLootState of the client). */
    std::uint32_t state{0};
    /** The number of the claim of the character on this object (each new claim takes the next one). */
    std::uint64_t generation{0};
    std::int64_t rolledAt{0};
    /** The time of the open and the time of the reset of a looted claim. 0: none. */
    std::int64_t lootedAt{0};
    std::int64_t resetAt{0};
    std::vector<ClaimItemRow> items;
};

/**
 * The part of a loot bag of one receiver (stage 3.5b.2, tables `loot_bags` and `loot_bag_items`; docs/systems/loot-bags.md). The character
 * id is the owner key; the bag key is the durable id of the bag (a UUID), the same for each receiver of one bag. The times are Unix
 * milliseconds (the wall clock: the end of a bag goes on while the backend is off). A row with no item: the receiver took each item.
 */
struct LootBagRow {
    std::string bagKey;
    /** InstancedLootType of the client: 1 AILoot (the items of a kill), 5 Discarded (the drop of a player). */
    std::uint32_t kind{1};
    /** The kind of the creature of a kill, or "discard". For a reader of the database and the log. */
    std::string source;
    WorldPosition position;
    /** The rarity of the bag for this receiver (the selector of its LootTracker entry). */
    std::uint32_t rarity{0};
    std::int64_t createdAt{0};
    std::int64_t expiresAt{0};
    /**
     * The fast end of an empty bag (FastKillSelfDelay; schema 11, code review of stage 3.5b.2, finding LB-R9): the absolute time that the
     * last take of the bag wrote in the row of its character, in the transaction of the take. 0: none (a part with items, or a row of
     * schema 10). The loader takes the largest value of the rows of a bag whose parts are all empty.
     */
    std::int64_t emptiedAt{0};
    /**
     * The item instances of the receiver in the bag, in their sequence (`slot` is the place in the list; `container` is not used). A row
     * with an empty instance id is an item of the roll that the item catalog did not have (stage 2.2, the open point of 3.5b.2): no
     * instance, only the item id, the quantity, the gear score (0: the value of the item table), and the perks.
     */
    std::vector<ItemRow> items;
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
    /**
     * Inserts a new character with its start items and, from stage 1.8a, its position (when the value has one) and the rows of `extra`
     * (its archetype achievement, its pending start grant), in one transaction.
     */
    [[nodiscard]] InsertResult insert_character(const Character& character, const std::vector<ItemRow>& items = {},
                                                const NewCharacterRows& extra = {});

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
     * @return The item instances of a character in the slots `firstSlot` to `lastSlot` of one container, in the sequence of the slots
     *         (stage U4: the armor of the paperdoll for the menu document). One query on the index of the places.
     */
    [[nodiscard]] std::vector<ItemRow> list_slots(const std::string& characterId, int container, std::uint32_t firstSlot, std::uint32_t lastSlot);
    /** @return The number of item queries (list_items, list_slots) of this store: the cost check of a route (test case ME7). */
    [[nodiscard]] std::uint64_t item_queries() const {
        return m_itemQueries.load();
    }

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

    /** @return The entitlement aliases of an account (stage 1.1b, table `entitlements`), the oldest first. */
    [[nodiscard]] std::vector<std::string> list_entitlements(const std::string& personaId);
    /**
     * Gives an account the entitlements that it does not have, in one write (in a batch: a savepoint of it). A row that
     * is there stays: a grant again changes nothing.
     * @param source A text for a reader of the database, for example "policy".
     * @param added When not null: the aliases that the account did not have.
     * @return False when the database did not accept the change. Then no row changed.
     */
    [[nodiscard]] bool grant_entitlements(const std::string& personaId,
                                          const std::vector<std::string>& aliases,
                                          const std::string& source,
                                          std::vector<std::string>* added = nullptr);
    /** @return The achievement ids of a character (stage 1.1b, table `achievements`), the oldest first. */
    [[nodiscard]] std::vector<std::string> list_achievements(const std::string& characterId);
    /** Gives a character the achievements that it does not have, in one write. The same rules as grant_entitlements. */
    [[nodiscard]] bool grant_achievements(const std::string& characterId,
                                          const std::vector<std::string>& ids,
                                          std::vector<std::string>* added = nullptr);
    /** @return The entitlements whose reward a character got (stage 1.1b, table `entitlement_rewards`). */
    [[nodiscard]] std::vector<std::string> list_entitlement_rewards(const std::string& characterId);
    /**
     * Records that a character got the reward of an entitlement. In a batch it is a savepoint of the transaction of the
     * reward (the item rows and this row commit together).
     * @return True when the row is new. False when the character had it already or the database did not accept it.
     */
    [[nodiscard]] bool record_entitlement_reward(const std::string& characterId, const std::string& alias);

    /** @return The loot claims of a character (stage 3.5b.1), with the items of each rolled claim, in the sequence of their keys. */
    [[nodiscard]] std::vector<LootClaimRow> list_loot_claims(const std::string& characterId);
    /**
     * Writes the loot claim of a character on an object: the row and its items replace a row of the same key (in a batch: a savepoint of
     * its transaction; the claim and the items of the bag commit together).
     * @return False when the database did not accept the change. Then no row changed.
     */
    [[nodiscard]] bool write_loot_claim(const std::string& characterId, const LootClaimRow& claim);
    /** Removes the loot claims of a character on these objects (a reset), with their items. @return False: no change. */
    [[nodiscard]] bool delete_loot_claims(const std::string& characterId, const std::vector<std::string>& objectKeys);
    /** @return The parts of each loot bag (stage 3.5b.2) with their items: (character id, part), in the sequence of the bag keys. */
    [[nodiscard]] std::vector<std::pair<std::string, LootBagRow>> list_loot_bags();
    /**
     * Writes the part of a loot bag of a character: the row and its items replace the rows of the same bag key of the character (in a
     * batch: a savepoint of its transaction; the items of the bag of the character and the bag commit together).
     * @return False when the database did not accept the change. Then no row changed.
     */
    [[nodiscard]] bool write_loot_bag(const std::string& characterId, const LootBagRow& bag);
    /** Removes each part of a loot bag (the end of the bag), with its items. @return False: no change. */
    [[nodiscard]] bool delete_loot_bag(const std::string& bagKey);

    /**
     * @return The stored position of the day cycle of a world (stage 2.2, table `world_time`): nanoseconds from 00:00 of the cycle.
     *         No value: the world has no row (a new world), or the read failed (a log line).
     */
    [[nodiscard]] std::optional<std::uint64_t> world_time(const std::string& worldKey);
    /**
     * Writes the position of the day cycle of a world (the row of the world key: the world time is not a value of a character), and
     * the run clock of the world when it has a value (stage 3.6a).
     * @return False when the database did not accept the row. Then the row did not change.
     */
    [[nodiscard]] bool set_world_time(const std::string& worldKey, std::uint64_t position, std::optional<std::uint64_t> run = std::nullopt);
    /**
     * @return The stored run clock of a world (stage 3.6a, column `run_ns` of `world_time`): the time in nanoseconds that the backend
     *         ran with this world. No value: no row.
     */
    [[nodiscard]] std::optional<std::uint64_t> world_run_time(const std::string& worldKey);
    /** @return The recipe cooldowns of a character (stage 3.7b, table `craft_cooldowns`), in the sequence of the recipe ids. */
    // The quests of a character (stage 1.8a; docs/systems/quests.md). Each write is a savepoint in a batch.
    /** @return The saved quests of a character with their tasks, in the order of their instances. */
    [[nodiscard]] std::vector<QuestRow> list_quests(const std::string& characterId);
    /** Writes a quest of a character: the row and each task row (the task rows of the instance are replaced). */
    [[nodiscard]] bool write_quest(const std::string& characterId, const QuestRow& quest);
    /** @return The pinned instances of a character, in their order. */
    [[nodiscard]] std::vector<std::uint64_t> list_quest_pins(const std::string& characterId);
    /** Replaces the pinned instances of a character. */
    [[nodiscard]] bool write_quest_pins(const std::string& characterId, const std::vector<std::uint64_t>& pins);
    /** @return The one-time grants of a character, in the order of their source and key. */
    [[nodiscard]] std::vector<QuestGrantRow> list_quest_grants(const std::string& characterId);
    /** Writes (inserts or replaces) a one-time grant of a character. */
    [[nodiscard]] bool write_quest_grant(const std::string& characterId, const QuestGrantRow& grant);
    /** @return The undo rows of a character. */
    [[nodiscard]] std::vector<QuestUndoRow> list_quest_undo(const std::string& characterId);
    /** Writes the undo row of an achievement action. */
    [[nodiscard]] bool write_quest_undo(const std::string& characterId, const QuestUndoRow& row);
    /** Stage 1.8c: removes a quest of a character with its task rows and its undo rows (an abandon, a reset, a failure). */
    [[nodiscard]] bool delete_quest(const std::string& characterId, std::uint64_t instance);
    /** @return The award rows of a character (stage 1.8c). */
    [[nodiscard]] std::vector<QuestAwardRow> list_quest_awards(const std::string& characterId);
    /** Writes the award row of a task (the amount replaces the amount of the row). */
    [[nodiscard]] bool write_quest_award(const std::string& characterId, const QuestAwardRow& row);
    /** Removes the award rows of an objective of a character (an abandon). */
    [[nodiscard]] bool delete_quest_awards(const std::string& characterId, const std::string& objective);
    /** Stage 1.8d: @return The repeat rows of a character. */
    [[nodiscard]] std::vector<QuestRepeatRow> list_quest_repeats(const std::string& characterId);
    /** Stage 1.8d: writes (inserts or replaces) the repeat row of an objective. In a batch: a savepoint of its transaction. */
    [[nodiscard]] bool write_quest_repeat(const std::string& characterId, const QuestRepeatRow& row);
    /** Stage 1.8d: removes the repeat row of an objective (its reset). */
    [[nodiscard]] bool delete_quest_repeat(const std::string& characterId, const std::string& objective);
    /** @return The largest instance id of the saved quests of all characters (0: none). */
    [[nodiscard]] std::uint64_t largest_quest_instance();
    /** Removes achievements of a character (stage 1.8a: the lock of a quest task). A row that is not there changes nothing. */
    [[nodiscard]] bool remove_achievements(const std::string& characterId, const std::vector<std::string>& ids,
                                           std::vector<std::string>* removed = nullptr);

    [[nodiscard]] std::vector<CraftCooldownRow> list_craft_cooldowns(const std::string& characterId);
    /** Writes (inserts or replaces) the cooldown of a recipe of a character. In a batch: a savepoint of its transaction. */
    [[nodiscard]] bool write_craft_cooldown(const std::string& characterId, const CraftCooldownRow& row);
    /** @return The depleted gathering nodes of the world (stage 3.6a, table `gathering_nodes`), in the sequence of their keys. */
    [[nodiscard]] std::vector<GatheringNodeRow> list_gathering_nodes();
    /** Writes the row of a depleted node (in a batch: a savepoint; the items and the experience of a gather commit with it). */
    [[nodiscard]] bool write_gathering_node(const GatheringNodeRow& row);
    /** Removes the row of a node (its respawn). @return False: no change. */
    [[nodiscard]] bool delete_gathering_node(const std::string& key);

private:
    /** @return The first column of each row of a query with one parameter (the owner key). */
    [[nodiscard]] std::vector<std::string> list_texts(const char* sql, const std::string& owner);
    /** Runs an insert with the parameters owner, text, date (and source, when the statement has a fourth one) for each text, in one write. */
    [[nodiscard]] bool insert_texts(const char* sql,
                                    const std::string& owner,
                                    const std::vector<std::string>& texts,
                                    const std::string& source,
                                    std::vector<std::string>* added);
    /** Starts a write: a transaction, or a savepoint in a batch. */
    /** Inserts or replaces a grant row (no transaction of its own: the caller has one). */
    [[nodiscard]] bool insert_quest_grant(const std::string& characterId, const QuestGrantRow& grant);
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
    std::atomic<std::uint64_t> m_itemQueries{0};
};

/** @return A random version-4 UUID in lower-case text. */
[[nodiscard]] std::string new_uuid();

/** @return The current UTC time as ISO 8601 text with milliseconds. */
[[nodiscard]] std::string now_iso8601();

} // namespace soulwarden::backend::state

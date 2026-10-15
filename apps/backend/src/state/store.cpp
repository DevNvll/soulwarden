#include "state/store.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <random>

#include <sqlite3.h>

#include "core/logging/log.h"
#include "state/character_name.h"

namespace soulwarden::backend::state {
namespace {

constexpr int kSchemaVersion = 7;

constexpr const char* kMigration1 = R"sql(
CREATE TABLE IF NOT EXISTS backend_metadata(
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS characters(
    character_id TEXT PRIMARY KEY,
    persona_id TEXT NOT NULL,
    world_id TEXT NOT NULL,
    name TEXT NOT NULL,
    created_date TEXT NOT NULL,
    modified_date TEXT NOT NULL,
    ftue_completed INTEGER NOT NULL DEFAULT 0,
    creation_params TEXT NOT NULL DEFAULT '',
    deleted_date TEXT
);
CREATE UNIQUE INDEX IF NOT EXISTS characters_active_name
    ON characters(name COLLATE NOCASE) WHERE deleted_date IS NULL;
)sql";

/** Version 2: the last position of a character in the world. Null until the first save. */
constexpr const char* kMigration2 = R"sql(
ALTER TABLE characters ADD COLUMN position_x REAL;
ALTER TABLE characters ADD COLUMN position_y REAL;
ALTER TABLE characters ADD COLUMN position_z REAL;
)sql";

/**
 * Version 3: player accounts. The Steam id of a login selects the account, and the persona id
 * of the account is the owner key of a character (characters.persona_id). A database from
 * before this version has one persona with a constant id and no Steam id: it becomes an
 * account that the first login takes. The statement does not name that id: it takes the
 * persona of the characters that the database has (the oldest one).
 */
constexpr const char* kMigration3 = R"sql(
CREATE TABLE IF NOT EXISTS accounts(
    persona_id TEXT PRIMARY KEY,
    account_id TEXT NOT NULL UNIQUE,
    steam_id TEXT UNIQUE,
    persona_name TEXT NOT NULL DEFAULT '',
    created_date TEXT NOT NULL,
    last_login_date TEXT NOT NULL DEFAULT ''
);
INSERT OR IGNORE INTO accounts(persona_id, account_id, created_date)
    SELECT persona_id, 'account-0001', MIN(created_date)
    FROM characters GROUP BY persona_id ORDER BY MIN(created_date), persona_id LIMIT 1;
CREATE INDEX IF NOT EXISTS characters_persona ON characters(persona_id);
)sql";

/**
 * Version 4: the archetype that the user selected in the character creation of the client
 * (the backstory name of the creation data). Empty for a character that the backend seeded.
 */
constexpr const char* kMigration4 = R"sql(
ALTER TABLE characters ADD COLUMN archetype TEXT NOT NULL DEFAULT '';
)sql";

/**
 * Version 5: the items and the currencies of a character. One row is one item instance (one
 * stack in one slot of a container), with the character id as the owner key. A slot of a
 * container has one item at most. The container 1 is the bag, and the container 2 is the
 * paperdoll (the equipment, from stage 1.4 on: the same table, thus no new version).
 */
constexpr const char* kMigration5 = R"sql(
CREATE TABLE IF NOT EXISTS items(
    instance_id TEXT PRIMARY KEY,
    character_id TEXT NOT NULL,
    container INTEGER NOT NULL,
    slot INTEGER NOT NULL,
    item_id TEXT NOT NULL,
    quantity INTEGER NOT NULL,
    gear_score INTEGER NOT NULL DEFAULT 0,
    durability INTEGER NOT NULL DEFAULT 0,
    max_durability INTEGER NOT NULL DEFAULT 0,
    flags INTEGER NOT NULL DEFAULT 0,
    chrono INTEGER NOT NULL DEFAULT 0
);
CREATE UNIQUE INDEX IF NOT EXISTS items_place ON items(character_id, container, slot);
CREATE TABLE IF NOT EXISTS currencies(
    character_id TEXT NOT NULL,
    currency_id TEXT NOT NULL,
    amount INTEGER NOT NULL,
    PRIMARY KEY(character_id, currency_id)
);
)sql";

/**
 * Version 6: the progression of a character. One row of `character_progress` has the level,
 * the experience, and the points that the character spent on each attribute. The other tables
 * have the weapon masteries and the trade skills (`progressions`), the nodes of the weapon
 * trees (`ability_nodes`), and the ability bars (`ability_bars`). Each row has the character
 * id as its owner key. An id is the id on the wire (the CRC-32 of the lower-case id text of
 * the game table); the column `name` has that text for a reader of the database.
 */
constexpr const char* kMigration6 = R"sql(
CREATE TABLE IF NOT EXISTS character_progress(
    character_id TEXT PRIMARY KEY,
    level INTEGER NOT NULL DEFAULT 1,
    experience INTEGER NOT NULL DEFAULT 0,
    constitution INTEGER NOT NULL DEFAULT 0,
    focus INTEGER NOT NULL DEFAULT 0,
    strength INTEGER NOT NULL DEFAULT 0,
    dexterity INTEGER NOT NULL DEFAULT 0,
    intelligence INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS progressions(
    character_id TEXT NOT NULL,
    progression_id INTEGER NOT NULL,
    name TEXT NOT NULL DEFAULT '',
    rank INTEGER NOT NULL DEFAULT 0,
    points INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(character_id, progression_id)
);
CREATE TABLE IF NOT EXISTS ability_nodes(
    character_id TEXT NOT NULL,
    pool_id INTEGER NOT NULL,
    node_id INTEGER NOT NULL,
    name TEXT NOT NULL DEFAULT '',
    level INTEGER NOT NULL DEFAULT 1,
    PRIMARY KEY(character_id, pool_id, node_id)
);
CREATE TABLE IF NOT EXISTS ability_bars(
    character_id TEXT NOT NULL,
    table_id INTEGER NOT NULL,
    slot INTEGER NOT NULL,
    ability_id INTEGER NOT NULL,
    name TEXT NOT NULL DEFAULT '',
    PRIMARY KEY(character_id, table_id, slot)
);
)sql";

/**
 * Version 7 (stage 1.3b): the perks of an item instance. One column for each of the five perk places of
 * the instance on the wire, with the PerkID text of the perk (not the index on the wire, which comes
 * from the perk data at each load); an empty text is no perk. A row of an older version has no perk.
 */
constexpr const char* kMigration7 = R"sql(
ALTER TABLE items ADD COLUMN perk1 TEXT NOT NULL DEFAULT '';
ALTER TABLE items ADD COLUMN perk2 TEXT NOT NULL DEFAULT '';
ALTER TABLE items ADD COLUMN perk3 TEXT NOT NULL DEFAULT '';
ALTER TABLE items ADD COLUMN perk4 TEXT NOT NULL DEFAULT '';
ALTER TABLE items ADD COLUMN perk5 TEXT NOT NULL DEFAULT '';
)sql";

/** The tables whose rows have a character id as the owner key: the rows go with the character. */
constexpr const char* kCharacterTables[] = {"items", "currencies", "character_progress", "progressions",
                                            "ability_nodes", "ability_bars"};

struct Statement {
    sqlite3_stmt* handle{nullptr};
    Statement(sqlite3* database, const char* sql) {
        sqlite3_prepare_v2(database, sql, -1, &handle, nullptr);
    }
    ~Statement() {
        sqlite3_finalize(handle);
    }
    void bind(int index, const std::string& value) const {
        sqlite3_bind_text(handle, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
    }
};

std::string column_text(sqlite3_stmt* statement, int index) {
    const unsigned char* text = sqlite3_column_text(statement, index);
    return text != nullptr ? reinterpret_cast<const char*>(text) : std::string{};
}

Character read_character(sqlite3_stmt* statement) {
    Character character;
    character.characterId = column_text(statement, 0);
    character.personaId = column_text(statement, 1);
    character.worldId = column_text(statement, 2);
    character.name = column_text(statement, 3);
    character.createdDate = column_text(statement, 4);
    character.modifiedDate = column_text(statement, 5);
    character.ftueCompleted = sqlite3_column_int(statement, 6) != 0;
    character.creationParams = column_text(statement, 7);
    if (sqlite3_column_type(statement, 8) != SQLITE_NULL) {
        character.position = WorldPosition{static_cast<float>(sqlite3_column_double(statement, 8)),
                                           static_cast<float>(sqlite3_column_double(statement, 9)),
                                           static_cast<float>(sqlite3_column_double(statement, 10))};
    }
    character.archetype = column_text(statement, 11);
    return character;
}

constexpr const char* kCharacterColumns =
    "character_id, persona_id, world_id, name, created_date, modified_date, ftue_completed, creation_params, "
    "position_x, position_y, position_z, archetype";

Account read_account(sqlite3_stmt* statement) {
    Account account;
    account.personaId = column_text(statement, 0);
    account.accountId = column_text(statement, 1);
    account.steamId = column_text(statement, 2);
    account.personaName = column_text(statement, 3);
    account.createdDate = column_text(statement, 4);
    account.lastLoginDate = column_text(statement, 5);
    return account;
}

constexpr const char* kAccountColumns =
    "persona_id, account_id, steam_id, persona_name, created_date, last_login_date";

} // namespace

Store::~Store() {
    if (m_database != nullptr) {
        sqlite3_close(m_database);
    }
}

bool Store::execute(const char* sql) {
    char* message = nullptr;
    if (sqlite3_exec(m_database, sql, nullptr, nullptr, &message) != SQLITE_OK) {
        log::write("store: SQL failed: %s", message != nullptr ? message : "unknown");
        sqlite3_free(message);
        return false;
    }
    return true;
}

bool Store::begin_write() {
    // In a batch the write is a savepoint of its transaction (Store::Batch).
    return execute(m_batch > 0 ? "SAVEPOINT store_write" : "BEGIN IMMEDIATE");
}

bool Store::end_write(bool ok) {
    if (m_batch > 0) {
        if (ok && execute("RELEASE store_write")) {
            return true;
        }
        (void)execute("ROLLBACK TO store_write");
        (void)execute("RELEASE store_write");
        return false;
    }
    if (ok && execute("COMMIT")) {
        return true;
    }
    (void)execute("ROLLBACK");
    return false;
}

Store::Batch::Batch(Store& store) : m_store(store) {
    m_store.m_mutex.lock();
    m_open = m_store.m_database != nullptr && m_store.execute("BEGIN IMMEDIATE");
    if (m_open) {
        ++m_store.m_batch;
    }
}

Store::Batch::~Batch() {
    if (m_open && !m_done) {
        --m_store.m_batch;
        (void)m_store.execute("ROLLBACK");
    }
    m_store.m_mutex.unlock();
}

bool Store::Batch::commit() {
    if (!m_open || m_done) {
        return false;
    }
    m_done = true;
    --m_store.m_batch;
    if (m_store.execute("COMMIT")) {
        return true;
    }
    (void)m_store.execute("ROLLBACK");
    return false;
}

bool Store::open(const std::string& path) {
    std::lock_guard lock(m_mutex);
    if (sqlite3_open(path.c_str(), &m_database) != SQLITE_OK) {
        log::write("store: cannot open %s", path.c_str());
        return false;
    }
    sqlite3_busy_timeout(m_database, 5000);
    return execute("PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA foreign_keys=ON;") && migrate();
}

bool Store::migrate() {
    int version = 0;
    {
        Statement statement(m_database, "PRAGMA user_version");
        if (sqlite3_step(statement.handle) == SQLITE_ROW) {
            version = sqlite3_column_int(statement.handle, 0);
        }
    }
    if (version > kSchemaVersion) {
        log::write("store: database schema %d is newer than this backend (%d)", version, kSchemaVersion);
        return false;
    }
    if (version < 1) {
        if (!execute("BEGIN IMMEDIATE") || !execute(kMigration1) || !execute("PRAGMA user_version=1")
            || !execute("COMMIT")) {
            (void)execute("ROLLBACK");
            return false;
        }
    }
    if (version < 2) {
        if (!execute("BEGIN IMMEDIATE") || !execute(kMigration2) || !execute("PRAGMA user_version=2")
            || !execute("COMMIT")) {
            (void)execute("ROLLBACK");
            return false;
        }
    }
    if (version < 3) {
        if (!execute("BEGIN IMMEDIATE") || !execute(kMigration3) || !execute("PRAGMA user_version=3")
            || !execute("COMMIT")) {
            (void)execute("ROLLBACK");
            return false;
        }
    }
    if (version < 4) {
        if (!execute("BEGIN IMMEDIATE") || !execute(kMigration4) || !execute("PRAGMA user_version=4")
            || !execute("COMMIT")) {
            (void)execute("ROLLBACK");
            return false;
        }
    }
    if (version < 5) {
        if (!execute("BEGIN IMMEDIATE") || !execute(kMigration5) || !execute("PRAGMA user_version=5")
            || !execute("COMMIT")) {
            (void)execute("ROLLBACK");
            return false;
        }
    }
    if (version < 6) {
        if (!execute("BEGIN IMMEDIATE") || !execute(kMigration6) || !execute("PRAGMA user_version=6")
            || !execute("COMMIT")) {
            (void)execute("ROLLBACK");
            return false;
        }
    }
    if (version < 7) {
        if (!execute("BEGIN IMMEDIATE") || !execute(kMigration7) || !execute("PRAGMA user_version=7")
            || !execute("COMMIT")) {
            (void)execute("ROLLBACK");
            return false;
        }
    }
    if (version < kSchemaVersion) {
        log::write("store: database schema changed from version %d to version %d", version, kSchemaVersion);
    }
    return true;
}

std::optional<Account> Store::login_account(const std::string& steamId,
                                            const std::string& personaName,
                                            bool adoptOldAccount,
                                            AccountOrigin& origin) {
    std::lock_guard lock(m_mutex);
    if (!execute("BEGIN IMMEDIATE")) {
        return std::nullopt;
    }
    origin = AccountOrigin::found;
    bool ok = true;
    bool exists = false;
    {
        Statement statement(m_database, "SELECT 1 FROM accounts WHERE steam_id=?1");
        statement.bind(1, steamId);
        exists = sqlite3_step(statement.handle) == SQLITE_ROW;
    }
    if (!exists && adoptOldAccount) {
        Statement statement(m_database,
                            "UPDATE accounts SET steam_id=?1 WHERE persona_id="
                            "(SELECT persona_id FROM accounts WHERE steam_id IS NULL ORDER BY created_date LIMIT 1)");
        statement.bind(1, steamId);
        ok = sqlite3_step(statement.handle) == SQLITE_DONE;
        if (ok && sqlite3_changes(m_database) == 1) {
            origin = AccountOrigin::adopted;
            exists = true;
        }
    }
    if (ok && !exists) {
        // The ids of a new account are random: the Steam id is then not in a service path.
        Statement statement(m_database,
                            "INSERT INTO accounts(persona_id, account_id, steam_id, created_date) "
                            "VALUES(?1,?2,?3,?4)");
        statement.bind(1, new_uuid());
        statement.bind(2, new_uuid());
        statement.bind(3, steamId);
        statement.bind(4, now_iso8601());
        ok = sqlite3_step(statement.handle) == SQLITE_DONE;
        origin = AccountOrigin::created;
    }
    if (ok) {
        Statement statement(m_database,
                            "UPDATE accounts SET persona_name=CASE WHEN ?2='' THEN persona_name ELSE ?2 END, "
                            "last_login_date=?3 WHERE steam_id=?1");
        statement.bind(1, steamId);
        statement.bind(2, personaName);
        statement.bind(3, now_iso8601());
        ok = sqlite3_step(statement.handle) == SQLITE_DONE;
    }
    std::optional<Account> account;
    if (ok) {
        const std::string sql = std::string("SELECT ") + kAccountColumns + " FROM accounts WHERE steam_id=?1";
        Statement statement(m_database, sql.c_str());
        statement.bind(1, steamId);
        if (sqlite3_step(statement.handle) == SQLITE_ROW) {
            account = read_account(statement.handle);
        }
    }
    if (!account || !execute("COMMIT")) {
        (void)execute("ROLLBACK");
        return std::nullopt;
    }
    return account;
}

std::optional<Account> Store::find_account(const std::string& personaId) {
    std::lock_guard lock(m_mutex);
    const std::string sql = std::string("SELECT ") + kAccountColumns + " FROM accounts WHERE persona_id=?1";
    Statement statement(m_database, sql.c_str());
    statement.bind(1, personaId);
    if (sqlite3_step(statement.handle) == SQLITE_ROW) {
        return read_account(statement.handle);
    }
    return std::nullopt;
}

std::optional<Account> Store::find_account_of_steam_id(const std::string& steamId) {
    if (steamId.empty()) {
        return std::nullopt;
    }
    std::lock_guard lock(m_mutex);
    const std::string sql = std::string("SELECT ") + kAccountColumns + " FROM accounts WHERE steam_id=?1";
    Statement statement(m_database, sql.c_str());
    statement.bind(1, steamId);
    if (sqlite3_step(statement.handle) == SQLITE_ROW) {
        return read_account(statement.handle);
    }
    return std::nullopt;
}

std::vector<Character> Store::list_characters(const std::string& personaId) {
    std::lock_guard lock(m_mutex);
    std::vector<Character> characters;
    const std::string sql = std::string("SELECT ") + kCharacterColumns
                            + " FROM characters WHERE persona_id=?1 AND deleted_date IS NULL ORDER BY created_date";
    Statement statement(m_database, sql.c_str());
    statement.bind(1, personaId);
    while (sqlite3_step(statement.handle) == SQLITE_ROW) {
        characters.push_back(read_character(statement.handle));
    }
    return characters;
}

std::optional<Character> Store::find_character(const std::string& characterId) {
    std::lock_guard lock(m_mutex);
    const std::string sql = std::string("SELECT ") + kCharacterColumns
                            + " FROM characters WHERE character_id=?1 AND deleted_date IS NULL";
    Statement statement(m_database, sql.c_str());
    statement.bind(1, characterId);
    if (sqlite3_step(statement.handle) == SQLITE_ROW) {
        return read_character(statement.handle);
    }
    return std::nullopt;
}

bool Store::name_in_use(const std::string& name) {
    // The compare form is made in the backend: SQLite changes the case of ASCII letters only.
    const std::string form = name_compare_form(name);
    Statement statement(m_database, "SELECT name FROM characters WHERE deleted_date IS NULL");
    while (sqlite3_step(statement.handle) == SQLITE_ROW) {
        if (name_compare_form(column_text(statement.handle, 0)) == form) {
            return true;
        }
    }
    return false;
}

bool Store::character_name_exists(const std::string& name) {
    std::lock_guard lock(m_mutex);
    return name_in_use(name);
}

InsertResult Store::insert_character(const Character& character, const std::vector<ItemRow>& items) {
    std::lock_guard lock(m_mutex);
    if (!execute("BEGIN IMMEDIATE")) {
        return InsertResult::failed;
    }
    InsertResult result = name_in_use(character.name) ? InsertResult::nameInUse : InsertResult::inserted;
    if (result == InsertResult::inserted) {
        Statement statement(m_database,
                            "INSERT INTO characters(character_id, persona_id, world_id, name, created_date, "
                            "modified_date, ftue_completed, creation_params, archetype) "
                            "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9)");
        statement.bind(1, character.characterId);
        statement.bind(2, character.personaId);
        statement.bind(3, character.worldId);
        statement.bind(4, character.name);
        statement.bind(5, character.createdDate);
        statement.bind(6, character.modifiedDate);
        sqlite3_bind_int(statement.handle, 7, character.ftueCompleted ? 1 : 0);
        statement.bind(8, character.creationParams);
        statement.bind(9, character.archetype);
        if (sqlite3_step(statement.handle) != SQLITE_DONE) {
            // The index characters_active_name refuses a name that is in use.
            if (sqlite3_extended_errcode(m_database) == SQLITE_CONSTRAINT_UNIQUE) {
                result = InsertResult::nameInUse;
            } else {
                result = InsertResult::failed;
                log::write("store: no insert of a character: %s", sqlite3_errmsg(m_database));
            }
        }
    }
    // The items that the character starts with: a character with a part of them does not stay.
    for (const ItemRow& row : items) {
        if (result != InsertResult::inserted) {
            break;
        }
        if (!insert_item(character.characterId, row)) {
            result = InsertResult::failed;
        }
    }
    if (result == InsertResult::inserted && !execute("COMMIT")) {
        result = InsertResult::failed;
    }
    if (result != InsertResult::inserted) {
        (void)execute("ROLLBACK");
    }
    return result;
}

DeleteResult Store::delete_character(const std::string& characterId) {
    std::lock_guard lock(m_mutex);
    if (!execute("BEGIN IMMEDIATE")) {
        return DeleteResult::failed;
    }
    DeleteResult result = DeleteResult::deleted;
    {
        Statement statement(m_database, "DELETE FROM characters WHERE character_id=?1 AND deleted_date IS NULL");
        statement.bind(1, characterId);
        if (sqlite3_step(statement.handle) != SQLITE_DONE) {
            result = DeleteResult::failed;
        } else if (sqlite3_changes(m_database) != 1) {
            result = DeleteResult::notFound;
        }
    }
    // The rows that have the character as their owner key go with it.
    for (const char* table : kCharacterTables) {
        if (result != DeleteResult::deleted) {
            break;
        }
        const std::string sql = std::string("DELETE FROM ") + table + " WHERE character_id=?1";
        Statement statement(m_database, sql.c_str());
        statement.bind(1, characterId);
        if (sqlite3_step(statement.handle) != SQLITE_DONE) {
            result = DeleteResult::failed;
        }
    }
    if (result == DeleteResult::failed) {
        log::write("store: no delete of character %s: %s", characterId.c_str(), sqlite3_errmsg(m_database));
    }
    if (result == DeleteResult::deleted && !execute("COMMIT")) {
        result = DeleteResult::failed;
    }
    if (result != DeleteResult::deleted) {
        (void)execute("ROLLBACK");
    }
    return result;
}

bool Store::set_ftue_completed(const std::string& characterId, bool completed) {
    std::lock_guard lock(m_mutex);
    Statement statement(m_database,
                        "UPDATE characters SET ftue_completed=?2, modified_date=?3 WHERE character_id=?1 "
                        "AND deleted_date IS NULL");
    statement.bind(1, characterId);
    sqlite3_bind_int(statement.handle, 2, completed ? 1 : 0);
    statement.bind(3, now_iso8601());
    return sqlite3_step(statement.handle) == SQLITE_DONE && sqlite3_changes(m_database) == 1;
}

bool Store::set_character_position(const std::string& characterId, const WorldPosition& position) {
    std::lock_guard lock(m_mutex);
    Statement statement(m_database,
                        "UPDATE characters SET position_x=?2, position_y=?3, position_z=?4 WHERE character_id=?1 "
                        "AND deleted_date IS NULL");
    statement.bind(1, characterId);
    sqlite3_bind_double(statement.handle, 2, position.x);
    sqlite3_bind_double(statement.handle, 3, position.y);
    sqlite3_bind_double(statement.handle, 4, position.z);
    return sqlite3_step(statement.handle) == SQLITE_DONE && sqlite3_changes(m_database) == 1;
}

std::vector<ItemRow> Store::list_items(const std::string& characterId) {
    std::lock_guard lock(m_mutex);
    std::vector<ItemRow> rows;
    Statement statement(m_database,
                        "SELECT instance_id, container, slot, item_id, quantity, gear_score, durability, "
                        "max_durability, flags, chrono, perk1, perk2, perk3, perk4, perk5 FROM items "
                        "WHERE character_id=?1 ORDER BY container, slot");
    statement.bind(1, characterId);
    while (sqlite3_step(statement.handle) == SQLITE_ROW) {
        ItemRow row;
        row.instanceId = column_text(statement.handle, 0);
        row.container = sqlite3_column_int(statement.handle, 1);
        row.slot = static_cast<std::uint32_t>(sqlite3_column_int64(statement.handle, 2));
        row.itemId = column_text(statement.handle, 3);
        row.quantity = static_cast<std::uint32_t>(sqlite3_column_int64(statement.handle, 4));
        row.gearScore = static_cast<std::uint32_t>(sqlite3_column_int64(statement.handle, 5));
        row.durability = static_cast<std::uint32_t>(sqlite3_column_int64(statement.handle, 6));
        row.maxDurability = static_cast<std::uint32_t>(sqlite3_column_int64(statement.handle, 7));
        row.flags = static_cast<std::uint32_t>(sqlite3_column_int64(statement.handle, 8));
        row.chrono = static_cast<std::uint32_t>(sqlite3_column_int64(statement.handle, 9));
        for (std::size_t place = 0; place < row.perks.size(); ++place) {
            row.perks[place] = column_text(statement.handle, static_cast<int>(10 + place));
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

bool Store::write_items(const std::string& characterId,
                        const std::vector<std::string>& removed,
                        const std::vector<ItemRow>& rows,
                        std::optional<std::uint64_t> coin) {
    std::lock_guard lock(m_mutex);
    if (!begin_write()) {
        return false;
    }
    bool ok = true;
    // All rows that change go out first: a slot of a container has one row at most, and two
    // items that change places must not be in one slot at the same time.
    const auto remove = [&](const std::string& instanceId) {
        Statement statement(m_database, "DELETE FROM items WHERE instance_id=?1 AND character_id=?2");
        statement.bind(1, instanceId);
        statement.bind(2, characterId);
        ok = ok && sqlite3_step(statement.handle) == SQLITE_DONE;
    };
    for (const std::string& instanceId : removed) {
        remove(instanceId);
    }
    for (const ItemRow& row : rows) {
        remove(row.instanceId);
    }
    for (const ItemRow& row : rows) {
        ok = ok && insert_item(characterId, row);
    }
    if (ok && coin) {
        Statement statement(m_database,
                            "INSERT INTO currencies(character_id, currency_id, amount) VALUES(?1,?2,?3) "
                            "ON CONFLICT(character_id, currency_id) DO UPDATE SET amount=excluded.amount");
        statement.bind(1, characterId);
        statement.bind(2, kCurrencyCoin);
        sqlite3_bind_int64(statement.handle, 3, static_cast<sqlite3_int64>(*coin));
        ok = sqlite3_step(statement.handle) == SQLITE_DONE;
    }
    if (!end_write(ok)) {
        return false;
    }
    return true;
}

bool Store::insert_item(const std::string& characterId, const ItemRow& row) {
    Statement statement(m_database,
                        "INSERT INTO items(instance_id, character_id, container, slot, item_id, quantity, "
                        "gear_score, durability, max_durability, flags, chrono, perk1, perk2, perk3, perk4, perk5) "
                        "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16)");
    statement.bind(1, row.instanceId);
    statement.bind(2, characterId);
    sqlite3_bind_int(statement.handle, 3, row.container);
    sqlite3_bind_int64(statement.handle, 4, row.slot);
    statement.bind(5, row.itemId);
    sqlite3_bind_int64(statement.handle, 6, row.quantity);
    sqlite3_bind_int64(statement.handle, 7, row.gearScore);
    sqlite3_bind_int64(statement.handle, 8, row.durability);
    sqlite3_bind_int64(statement.handle, 9, row.maxDurability);
    sqlite3_bind_int64(statement.handle, 10, row.flags);
    sqlite3_bind_int64(statement.handle, 11, row.chrono);
    for (std::size_t place = 0; place < row.perks.size(); ++place) {
        statement.bind(static_cast<int>(12 + place), row.perks[place]);
    }
    if (sqlite3_step(statement.handle) != SQLITE_DONE) {
        // An instance id of a different character, or a slot that has an item.
        log::write("store: no write of an item of character %s: %s", characterId.c_str(), sqlite3_errmsg(m_database));
        return false;
    }
    return true;
}

std::uint64_t Store::currency(const std::string& characterId, const std::string& currencyId) {
    std::lock_guard lock(m_mutex);
    Statement statement(m_database, "SELECT amount FROM currencies WHERE character_id=?1 AND currency_id=?2");
    statement.bind(1, characterId);
    statement.bind(2, currencyId);
    if (sqlite3_step(statement.handle) == SQLITE_ROW) {
        const sqlite3_int64 amount = sqlite3_column_int64(statement.handle, 0);
        return amount < 0 ? 0 : static_cast<std::uint64_t>(amount);
    }
    return 0;
}

bool Store::set_currency(const std::string& characterId, const std::string& currencyId, std::uint64_t amount) {
    std::lock_guard lock(m_mutex);
    Statement statement(m_database,
                        "INSERT INTO currencies(character_id, currency_id, amount) VALUES(?1,?2,?3) "
                        "ON CONFLICT(character_id, currency_id) DO UPDATE SET amount=excluded.amount");
    statement.bind(1, characterId);
    statement.bind(2, currencyId);
    sqlite3_bind_int64(statement.handle, 3, static_cast<sqlite3_int64>(amount));
    return sqlite3_step(statement.handle) == SQLITE_DONE;
}

ProgressRow Store::progress(const std::string& characterId) {
    std::lock_guard lock(m_mutex);
    ProgressRow row;
    Statement statement(m_database,
                        "SELECT level, experience, constitution, focus, strength, dexterity, intelligence "
                        "FROM character_progress WHERE character_id=?1");
    statement.bind(1, characterId);
    if (sqlite3_step(statement.handle) == SQLITE_ROW) {
        const auto number = [&](int column) {
            const sqlite3_int64 value = sqlite3_column_int64(statement.handle, column);
            return value < 0 ? 0U : static_cast<std::uint32_t>(std::min<sqlite3_int64>(value, 0xFFFFFFFFLL));
        };
        row.level = std::max(number(0), 1U);
        row.experience = number(1);
        for (std::size_t type = 0; type < row.spent.size(); ++type) {
            row.spent[type] = number(static_cast<int>(type) + 2);
        }
    }
    return row;
}

bool Store::set_progress(const std::string& characterId, const ProgressRow& row, std::optional<std::uint64_t> coin) {
    std::lock_guard lock(m_mutex);
    if (!begin_write()) {
        return false;
    }
    bool ok = true;
    {
        Statement statement(m_database,
                            "INSERT INTO character_progress(character_id, level, experience, constitution, focus, "
                            "strength, dexterity, intelligence) VALUES(?1,?2,?3,?4,?5,?6,?7,?8) "
                            "ON CONFLICT(character_id) DO UPDATE SET level=excluded.level, "
                            "experience=excluded.experience, constitution=excluded.constitution, focus=excluded.focus, "
                            "strength=excluded.strength, dexterity=excluded.dexterity, "
                            "intelligence=excluded.intelligence");
        statement.bind(1, characterId);
        sqlite3_bind_int64(statement.handle, 2, row.level);
        sqlite3_bind_int64(statement.handle, 3, row.experience);
        for (std::size_t type = 0; type < row.spent.size(); ++type) {
            sqlite3_bind_int64(statement.handle, static_cast<int>(type) + 4, row.spent[type]);
        }
        ok = sqlite3_step(statement.handle) == SQLITE_DONE;
    }
    if (ok && coin) {
        Statement statement(m_database,
                            "INSERT INTO currencies(character_id, currency_id, amount) VALUES(?1,?2,?3) "
                            "ON CONFLICT(character_id, currency_id) DO UPDATE SET amount=excluded.amount");
        statement.bind(1, characterId);
        statement.bind(2, kCurrencyCoin);
        sqlite3_bind_int64(statement.handle, 3, static_cast<sqlite3_int64>(*coin));
        ok = sqlite3_step(statement.handle) == SQLITE_DONE;
    }
    if (!ok) {
        log::write("store: no write of the progress of character %s: %s", characterId.c_str(), sqlite3_errmsg(m_database));
    }
    if (!end_write(ok)) {
        return false;
    }
    return true;
}

SkillRows Store::list_skills(const std::string& characterId) {
    std::lock_guard lock(m_mutex);
    SkillRows rows;
    const auto number = [](sqlite3_stmt* statement, int column) {
        return static_cast<std::uint32_t>(sqlite3_column_int64(statement, column));
    };
    {
        Statement statement(m_database,
                            "SELECT progression_id, name, rank, points FROM progressions WHERE character_id=?1 "
                            "ORDER BY progression_id");
        statement.bind(1, characterId);
        while (sqlite3_step(statement.handle) == SQLITE_ROW) {
            SkillRows::Rank rank;
            rank.id = number(statement.handle, 0);
            rank.name = column_text(statement.handle, 1);
            rank.rank = number(statement.handle, 2);
            const sqlite3_int64 points = sqlite3_column_int64(statement.handle, 3);
            rank.points = points < 0 ? 0 : static_cast<std::uint64_t>(points);
            rows.ranks.push_back(std::move(rank));
        }
    }
    {
        Statement statement(m_database,
                            "SELECT pool_id, node_id, name, level FROM ability_nodes WHERE character_id=?1 "
                            "ORDER BY pool_id, node_id");
        statement.bind(1, characterId);
        while (sqlite3_step(statement.handle) == SQLITE_ROW) {
            SkillRows::Node node;
            node.pool = number(statement.handle, 0);
            node.node = number(statement.handle, 1);
            node.name = column_text(statement.handle, 2);
            node.level = number(statement.handle, 3);
            rows.nodes.push_back(std::move(node));
        }
    }
    {
        Statement statement(m_database,
                            "SELECT table_id, slot, ability_id, name FROM ability_bars WHERE character_id=?1 "
                            "ORDER BY table_id, slot");
        statement.bind(1, characterId);
        while (sqlite3_step(statement.handle) == SQLITE_ROW) {
            SkillRows::Bar bar;
            bar.table = number(statement.handle, 0);
            bar.slot = number(statement.handle, 1);
            bar.ability = number(statement.handle, 2);
            bar.name = column_text(statement.handle, 3);
            rows.bars.push_back(std::move(bar));
        }
    }
    return rows;
}

bool Store::write_skills(const std::string& characterId, const SkillRows& rows) {
    std::lock_guard lock(m_mutex);
    if (!begin_write()) {
        return false;
    }
    bool ok = true;
    for (const char* table : {"progressions", "ability_nodes", "ability_bars"}) {
        const std::string sql = std::string("DELETE FROM ") + table + " WHERE character_id=?1";
        Statement statement(m_database, sql.c_str());
        statement.bind(1, characterId);
        ok = ok && sqlite3_step(statement.handle) == SQLITE_DONE;
    }
    for (const SkillRows::Rank& rank : rows.ranks) {
        Statement statement(m_database,
                            "INSERT INTO progressions(character_id, progression_id, name, rank, points) "
                            "VALUES(?1,?2,?3,?4,?5)");
        statement.bind(1, characterId);
        sqlite3_bind_int64(statement.handle, 2, rank.id);
        statement.bind(3, rank.name);
        sqlite3_bind_int64(statement.handle, 4, rank.rank);
        sqlite3_bind_int64(statement.handle, 5, static_cast<sqlite3_int64>(rank.points));
        ok = ok && sqlite3_step(statement.handle) == SQLITE_DONE;
    }
    for (const SkillRows::Node& node : rows.nodes) {
        Statement statement(m_database,
                            "INSERT INTO ability_nodes(character_id, pool_id, node_id, name, level) "
                            "VALUES(?1,?2,?3,?4,?5)");
        statement.bind(1, characterId);
        sqlite3_bind_int64(statement.handle, 2, node.pool);
        sqlite3_bind_int64(statement.handle, 3, node.node);
        statement.bind(4, node.name);
        sqlite3_bind_int64(statement.handle, 5, node.level);
        ok = ok && sqlite3_step(statement.handle) == SQLITE_DONE;
    }
    for (const SkillRows::Bar& bar : rows.bars) {
        Statement statement(m_database,
                            "INSERT INTO ability_bars(character_id, table_id, slot, ability_id, name) "
                            "VALUES(?1,?2,?3,?4,?5)");
        statement.bind(1, characterId);
        sqlite3_bind_int64(statement.handle, 2, bar.table);
        sqlite3_bind_int64(statement.handle, 3, bar.slot);
        sqlite3_bind_int64(statement.handle, 4, bar.ability);
        statement.bind(5, bar.name);
        ok = ok && sqlite3_step(statement.handle) == SQLITE_DONE;
    }
    if (!ok) {
        log::write("store: no write of the skills of character %s: %s", characterId.c_str(), sqlite3_errmsg(m_database));
    }
    if (!end_write(ok)) {
        return false;
    }
    return true;
}

std::string new_uuid() {
    static std::mutex mutex;
    static std::mt19937_64 generator{std::random_device{}()};
    std::uint64_t high;
    std::uint64_t low;
    {
        std::lock_guard lock(mutex);
        high = generator();
        low = generator();
    }
    high = (high & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    low = (low & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
    char text[40];
    std::snprintf(text,
                  sizeof(text),
                  "%08x-%04x-%04x-%04x-%012llx",
                  static_cast<unsigned>(high >> 32),
                  static_cast<unsigned>((high >> 16) & 0xFFFF),
                  static_cast<unsigned>(high & 0xFFFF),
                  static_cast<unsigned>(low >> 48),
                  static_cast<unsigned long long>(low & 0xFFFFFFFFFFFFULL));
    return text;
}

std::string now_iso8601() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    char date[32];
    std::strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", &utc);
    char text[48];
    std::snprintf(text, sizeof(text), "%s.%03dZ", date, static_cast<int>(millis));
    return text;
}

} // namespace soulwarden::backend::state

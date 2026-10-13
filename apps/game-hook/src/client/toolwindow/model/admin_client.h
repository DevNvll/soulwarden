#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The data of the tool window. One worker thread sends the requests to the admin interface of
// the local backend (routes /admin/v1, protocol notes 6.4) and keeps a copy of the last
// replies. The layout code reads that copy and gives the worker its requests: the thread that
// draws makes no socket call.
//
// The launcher gives the hook the token of the admin interface in SOULWARDEN_ADMIN_TOKEN. The hook
// keeps it in memory, removes the variable from the environment of the process, and sends it
// only in the header X-Soulwarden-Admin of a request to the backend. It writes it to no log.
//
// A command names a character id (roadmap, rule MP-9). The backend gives the characters of
// the account of this start, and the window selects one of them.

namespace soulwarden::client::toolwindow::model {

struct Character {
    std::string id;
    std::string name;
    unsigned level{1};
    bool inWorld{false};
};

struct BagItem {
    unsigned slot{0};
    std::string item;
    unsigned quantity{0};
    unsigned gearScore{0};
    bool locked{false};
};

struct Attribute {
    unsigned value{0};
    unsigned spent{0};
};

/** The attributes in the sequence of the backend: constitution, focus, strength, dexterity, intelligence. */
inline constexpr unsigned kAttributeCount = 5;
inline constexpr const char* kAttributeNames[kAttributeCount] = {"constitution", "focus", "strength", "dexterity",
                                                                 "intelligence"};

struct Weapon {
    std::string id;
    /** The level of the screen: 1 to 20. */
    unsigned level{1};
    unsigned experience{0};
    unsigned nextLevel{0};
    unsigned freePoints{0};
    unsigned spentPoints{0};
};

struct TradeSkill {
    std::string id;
    unsigned level{0};
    unsigned experience{0};
    unsigned maxLevel{0};
};

struct CatalogItem {
    std::string id;
    std::string type;
    unsigned tier{0};
    unsigned maxStack{1};
};

/** One creature of the world near the selected character. */
struct Creature {
    /** The id of the creature: 16 hexadecimal digits. */
    std::string id;
    std::string kind;
    std::string name;
    unsigned level{1};
    unsigned health{0};
    unsigned healthMax{0};
    bool dead{false};
    /** True for the creature of a spawn point. False for a creature of a command. */
    bool spawnPoint{false};
    /** Distance from the selected character, in meters. */
    float distance{0.0F};
};

/** One kind of the creature data of the backend. */
struct CreatureKind {
    std::string id;
    std::string name;
    std::string family;
    unsigned level{1};
    unsigned health{0};
    unsigned spawnPoints{0};
};

/** The last replies of the backend. */
struct Snapshot {
    /** False: the launcher gave no token, and the window can do nothing. */
    bool configured{false};
    /** True when the last request got a reply of the backend. */
    bool connected{false};
    /** A text for the status line when something is not correct, or empty. */
    std::string problem;

    /** True when the backend has an account for this start (after the first login). */
    bool account{false};
    /** True when the backend has the item catalog and the progression tables. */
    bool catalog{false};
    bool tables{false};
    std::vector<Character> characters;
    /** The id of the character of the values below, or empty. */
    std::string selected;
    bool inWorld{false};
    unsigned level{1};
    unsigned experience{0};
    unsigned levelExperience{0};
    unsigned nextLevelExperience{0};
    unsigned freeAttributePoints{0};
    unsigned health{0};
    unsigned healthMax{0};
    Attribute attributes[kAttributeCount]{};
    /** Hundredths of a coin. */
    std::uint64_t coin{0};
    unsigned bagSlots{0};
    std::vector<BagItem> bag;
    std::vector<BagItem> equipment;
    std::vector<Weapon> weapons;
    std::vector<TradeSkill> tradeSkills;

    /** The result of the last item search, and its text. */
    std::string searchText;
    std::vector<CatalogItem> found;
    unsigned foundTotal{0};
    bool searchDone{false};

    /** True when the backend has the creature data. */
    bool creatureData{false};
    /** The creatures near the selected character, the nearest one first, and the number of all creatures of the world. */
    std::vector<Creature> creatures;
    unsigned creatureTotal{0};
    /** Spawn points whose creature died or was removed: they make no creature until a reset. */
    unsigned spawnPointsWaiting{0};
    /** The result of the last search of creature kinds. */
    std::vector<CreatureKind> kinds;
    unsigned kindTotal{0};
    bool kindSearchDone{false};

    /** True while a command is with the backend. */
    bool busy{false};
    /** The result of the last command, or empty. */
    std::string result;
    bool resultIsError{false};
};

/** Reads the token and starts the worker thread. Call it one time. */
void start() noexcept;

/** The window shows or does not show: the worker reads the state only while it shows. Any thread. */
void set_active(bool active) noexcept;

/**
 * Copies the last replies when they changed.
 * @param version In: the version that `snapshot` has. Out: the version of the copy.
 * @return True when `snapshot` got new values.
 */
bool read(Snapshot& snapshot, std::uint64_t& version) noexcept;

/** Selects the character of the next state requests. An empty id: the backend selects one. */
void select_character(const std::string& id) noexcept;

/** Asks for the items of the catalog that a text finds. */
void search(const std::string& text) noexcept;

/** Asks for the kinds of the creature data that a text finds. */
void search_kinds(const std::string& text) noexcept;

/**
 * Sends one command for a character. No effect while a command is with the backend.
 * @param character The id of the character that the window showed when the user gave the
 *        command (the member `selected` of the snapshot that the window drew). The command
 *        keeps this id until the worker sends it: a later change of the selected character
 *        does not change the target of the command.
 * @param name The command name of the admin interface, for example "give_item".
 * @param arguments The member "args" of the command as JSON text.
 * @param label A short text of the action for the result line, for example "Give 5 OreT1".
 */
void command(const std::string& character,
             const char* name,
             const std::string& arguments,
             const std::string& label) noexcept;

} // namespace soulwarden::client::toolwindow::model

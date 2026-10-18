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

/** One travel place of the backend: a place of the world to which the travel command moves a character. */
struct TravelPlace {
    std::string id;
    /** "shrine" or "inn". */
    std::string kind;
    std::string name;
    std::string territory;
    float position[3]{};
};

/** One variant of a build preset (stage B0): the name of the command and the text of the window ("pve", "PvE tree"). */
struct PresetVariant {
    std::string name;
    std::string label;
};

/** One build preset of the backend (stage B0, the route GET /admin/v1/catalog/presets). */
struct Preset {
    std::string name;
    std::string title;
    /** False: the check of the start of the backend disabled the preset (`cause` has the text). */
    bool enabled{false};
    std::string cause;
    std::vector<PresetVariant> variants;
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
    /** True: the character is dead (its client shows the death screen). */
    bool dead{false};
    /** True: the creatures of the world ignore the character (they do not attack it). */
    bool creaturesIgnore{false};
    /** False: the behavior of the creatures is off for this start of the backend. */
    bool creatureBehavior{true};
    /** True: each kill of the character drops loot and coin (the loot switch of the backend, stage 3.5a). */
    bool forceLoot{false};
    Attribute attributes[kAttributeCount]{};
    /** Hundredths of a coin. */
    std::uint64_t coin{0};
    /** Stage 3.7c: the Azoth balance and its cap (0: the backend has no crafting data, or is from before stage 3.7c). */
    std::uint64_t azoth{0};
    std::uint64_t azothCap{0};
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

    /** The position of the selected character in the world (meters: east, north, up). */
    bool hasPosition{false};
    float position[3]{};
    /** The travel places of the backend. `placesDone`: the backend gave its list (which is empty with no file). */
    std::vector<TravelPlace> places;
    bool placesLoaded{false};
    bool placesDone{false};

    /** The build presets of the backend (stage B0). `presetsDone`: the backend gave its list (a backend from before has none). */
    std::vector<Preset> presets;
    bool presetsDone{false};

    /**
     * The world time (stage 2.2, the member "worldTime" of the state): one value for the whole world. `hasWorldTime`: a backend from
     * before stage 2.2 has no such member.
     */
    bool hasWorldTime{false};
    /** The time of the day "HH:MM:SS", the phase, the next phase, and the seconds to it, at the time of the last state. */
    std::string worldTime;
    std::string phase;
    std::string nextPhase;
    unsigned secondsToNextPhase{0};
    /** The cycle: the real seconds of the day part and of the night part. */
    unsigned dayLength{0};
    unsigned nightLength{0};

    /** True while a command is with the backend. */
    bool busy{false};
    /** The result of the last command, or empty. */
    std::string result;
    bool resultIsError{false};
    /** The number of the last result: each reply of a command adds 1, also when its text is the same as the text before. */
    std::uint64_t resultNumber{0};
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

/** Asks for the travel places of the backend. The list does not change in one start: the window asks one time. */
void load_places() noexcept;

/** Asks for the build presets of the backend (stage B0). The list does not change in one start: the window asks one time. */
void load_presets() noexcept;

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

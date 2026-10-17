#include "client/toolwindow/model/admin_client.h"

#include <Windows.h>

#include <atomic>
#include <utility>

#include <nlohmann/json.hpp>

#include "client/network/backend_http.h"
#include "core/logging/log.h"

namespace soulwarden::client::toolwindow::model {
namespace {

using nlohmann::json;

/** The state request interval while the window shows. */
constexpr DWORD kPollMilliseconds = 1000;
/** Rows of one item search. */
constexpr unsigned kSearchRows = 100;
/** A token has this number of characters or more (the backend has the same rule). */
constexpr std::size_t kTokenMinimum = 32;

SRWLOCK g_lock = SRWLOCK_INIT;
// Protected by the lock.
Snapshot g_snapshot;
std::uint64_t g_version = 1;
std::string g_wantedCharacter;
std::string g_wantedSearch;
bool g_searchPending = false;
std::string g_wantedKindSearch;
bool g_kindSearchPending = false;
bool g_placesPending = false;
struct PendingCommand {
    /** The character that the window showed when the user gave the command. */
    std::string character;
    std::string name;
    std::string arguments;
    std::string label;
};
bool g_commandPending = false;
PendingCommand g_command;

std::atomic<bool> g_active{false};
HANDLE g_wake = nullptr;
/** "X-Soulwarden-Admin: <token>". Written one time before the worker starts. */
std::string g_tokenHeader;

class Guard {
public:
    Guard() noexcept {
        AcquireSRWLockExclusive(&g_lock);
    }
    ~Guard() {
        ReleaseSRWLockExclusive(&g_lock);
    }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
};

/** Readers of a JSON member that give a default for a member that is not there or has a different type. */
unsigned number_of(const json& object, const char* name, unsigned fallback = 0) {
    const auto member = object.is_object() ? object.find(name) : object.end();
    if (member == object.end()) {
        return fallback;
    }
    if (member->is_number_unsigned()) {
        return member->get<unsigned>();
    }
    // The health of a character is a number with a fraction.
    if (member->is_number_float() && member->get<double>() >= 0.0 && member->get<double>() < 4.0e9) {
        return static_cast<unsigned>(member->get<double>() + 0.5);
    }
    return fallback;
}

float real_of(const json& object, const char* name) {
    const auto member = object.is_object() ? object.find(name) : object.end();
    return member != object.end() && member->is_number() ? member->get<float>() : 0.0F;
}

std::uint64_t wide_number_of(const json& object, const char* name) {
    const auto member = object.is_object() ? object.find(name) : object.end();
    return member != object.end() && member->is_number_unsigned() ? member->get<std::uint64_t>() : 0;
}

std::string text_of(const json& object, const char* name) {
    const auto member = object.is_object() ? object.find(name) : object.end();
    return member != object.end() && member->is_string() ? member->get<std::string>() : std::string{};
}

bool flag_of(const json& object, const char* name) {
    const auto member = object.is_object() ? object.find(name) : object.end();
    return member != object.end() && member->is_boolean() && member->get<bool>();
}

/** @return The member of an object, or an empty JSON value. */
const json& member_of(const json& object, const char* name) {
    static const json kNone;
    const auto member = object.is_object() ? object.find(name) : object.end();
    return member != object.end() ? *member : kNone;
}

void read_items(const json& list, std::vector<BagItem>& items) {
    items.clear();
    if (!list.is_array()) {
        return;
    }
    for (const json& entry : list) {
        BagItem item;
        item.slot = number_of(entry, "slot");
        item.item = text_of(entry, "item");
        item.quantity = number_of(entry, "quantity");
        item.gearScore = number_of(entry, "gearScore");
        item.locked = flag_of(entry, "locked");
        items.push_back(std::move(item));
    }
}

/** Puts the state reply of the backend into a snapshot. */
void read_state(const json& reply, Snapshot& snapshot) {
    snapshot.account = flag_of(reply, "account");
    snapshot.catalog = flag_of(reply, "catalog");
    snapshot.characters.clear();
    const json& characters = member_of(reply, "characters");
    if (characters.is_array()) {
        for (const json& entry : characters) {
            Character character;
            character.id = text_of(entry, "id");
            character.name = text_of(entry, "name");
            character.level = number_of(entry, "level", 1);
            character.inWorld = flag_of(entry, "inWorld");
            snapshot.characters.push_back(std::move(character));
        }
    }
    snapshot.selected = text_of(reply, "selected");
    const json& items = member_of(reply, "items");
    const json& progress = member_of(reply, "progress");
    snapshot.inWorld = flag_of(progress, "inWorld");
    snapshot.tables = flag_of(progress, "tables");
    snapshot.level = number_of(progress, "level", 1);
    snapshot.experience = number_of(progress, "experience");
    snapshot.levelExperience = number_of(progress, "levelExperience");
    snapshot.nextLevelExperience = number_of(progress, "nextLevelExperience");
    snapshot.freeAttributePoints = number_of(progress, "freeAttributePoints");
    snapshot.health = number_of(progress, "health");
    snapshot.healthMax = number_of(progress, "healthMax");
    // The life state of the character, and if the creatures ignore it (a backend from before
    // stage 3.4b has no such member: the values then are the values of a new character).
    const json& health = member_of(reply, "health");
    snapshot.dead = flag_of(health, "dead");
    snapshot.creaturesIgnore = flag_of(health, "creaturesIgnore");
    snapshot.creatureBehavior = !health.is_object() || !health.contains("creatureBehavior") || flag_of(health, "creatureBehavior");
    // A backend from before stage 3.5a has no loot switch: the value is false.
    snapshot.forceLoot = flag_of(health, "forceLoot");
    const json& attributes = member_of(progress, "attributes");
    for (unsigned index = 0; index < kAttributeCount; ++index) {
        const json& attribute = member_of(attributes, kAttributeNames[index]);
        snapshot.attributes[index].value = number_of(attribute, "value");
        snapshot.attributes[index].spent = number_of(attribute, "spent");
    }
    snapshot.coin = wide_number_of(items, "coin");
    snapshot.bagSlots = number_of(items, "slots");
    read_items(member_of(items, "items"), snapshot.bag);
    read_items(member_of(items, "equipment"), snapshot.equipment);
    snapshot.weapons.clear();
    const json& weapons = member_of(progress, "weapons");
    if (weapons.is_array()) {
        for (const json& entry : weapons) {
            Weapon weapon;
            weapon.id = text_of(entry, "id");
            weapon.level = number_of(entry, "level", 1);
            weapon.experience = number_of(entry, "experience");
            weapon.nextLevel = number_of(entry, "nextLevel");
            weapon.freePoints = number_of(entry, "freePoints");
            weapon.spentPoints = number_of(entry, "spentPoints");
            snapshot.weapons.push_back(std::move(weapon));
        }
    }
    // The position of the character (the member is there only for a character that is in the world).
    const json& position = member_of(reply, "position");
    snapshot.hasPosition = position.is_array() && position.size() == 3 && position[0].is_number() && position[1].is_number()
                           && position[2].is_number();
    for (std::size_t axis = 0; axis < 3; ++axis) {
        snapshot.position[axis] = snapshot.hasPosition ? position[axis].get<float>() : 0.0F;
    }
    // The world time (stage 2.2): a backend from before stage 2.2 has no such member.
    const json& worldTime = member_of(reply, "worldTime");
    snapshot.hasWorldTime = worldTime.is_object() && !text_of(worldTime, "time").empty();
    snapshot.worldTime = text_of(worldTime, "time");
    snapshot.phase = text_of(worldTime, "phase");
    snapshot.nextPhase = text_of(worldTime, "nextPhase");
    snapshot.secondsToNextPhase = number_of(worldTime, "secondsToNextPhase");
    const json& cycle = member_of(worldTime, "cycle");
    snapshot.dayLength = number_of(cycle, "dayLength");
    snapshot.nightLength = number_of(cycle, "nightLength");
    // The creatures near the character (the member is there only for a character with a state).
    const json& creatures = member_of(reply, "creatures");
    snapshot.creatureData = flag_of(creatures, "data");
    snapshot.creatureTotal = number_of(creatures, "total");
    snapshot.spawnPointsWaiting = number_of(creatures, "spawnPointsWaiting");
    snapshot.creatures.clear();
    const json& creatureList = member_of(creatures, "creatures");
    if (creatureList.is_array()) {
        for (const json& entry : creatureList) {
            Creature creature;
            creature.id = text_of(entry, "id");
            creature.kind = text_of(entry, "kind");
            creature.name = text_of(entry, "name");
            creature.level = number_of(entry, "level", 1);
            creature.health = number_of(entry, "health");
            creature.healthMax = number_of(entry, "healthMax");
            creature.dead = flag_of(entry, "dead");
            creature.spawnPoint = flag_of(entry, "spawnPoint");
            creature.distance = real_of(entry, "distance");
            snapshot.creatures.push_back(std::move(creature));
        }
    }
    snapshot.tradeSkills.clear();
    const json& tradeSkills = member_of(progress, "tradeSkills");
    if (tradeSkills.is_array()) {
        for (const json& entry : tradeSkills) {
            TradeSkill skill;
            skill.id = text_of(entry, "id");
            skill.level = number_of(entry, "level");
            skill.experience = number_of(entry, "experience");
            skill.maxLevel = number_of(entry, "maxLevel");
            snapshot.tradeSkills.push_back(std::move(skill));
        }
    }
}

/** @return A text for a reply that is not a result: the backend gave no reply, or refused the token. */
std::string problem_of(const network::BackendReply& reply) {
    if (reply.status == 0) {
        return "The backend gives no reply.";
    }
    if (reply.status == 401 || reply.status == 403) {
        return "The backend refuses the token of this start.";
    }
    if (reply.status == 404) {
        return "This backend has no admin interface.";
    }
    return {};
}

network::BackendReply request(const char* method, const std::string& path, const std::string& body) {
    network::BackendReply reply;
    (void)network::backend_request(method, path, g_tokenHeader.c_str(), body, reply);
    return reply;
}

void refresh_state() {
    std::string wanted;
    {
        const Guard guard;
        wanted = g_wantedCharacter;
    }
    network::BackendReply reply =
        request("GET", wanted.empty() ? "/admin/v1/state" : "/admin/v1/state?character=" + network::url_encode(wanted), {});
    if (reply.status == 404 && !wanted.empty()) {
        // The selected character is not there (deleted): the backend selects one.
        {
            const Guard guard;
            g_wantedCharacter.clear();
        }
        reply = request("GET", "/admin/v1/state", {});
    }
    const json document = reply.status == 200 ? json::parse(reply.body, nullptr, false) : json();
    const Guard guard;
    g_snapshot.connected = reply.status != 0;
    g_snapshot.problem = problem_of(reply);
    if (document.is_object()) {
        read_state(document, g_snapshot);
    }
    ++g_version;
}

void run_search(const std::string& text) {
    const network::BackendReply reply = request(
        "GET", "/admin/v1/catalog/items?limit=" + std::to_string(kSearchRows) + "&q=" + network::url_encode(text), {});
    const json document = reply.status == 200 ? json::parse(reply.body, nullptr, false) : json();
    const Guard guard;
    g_snapshot.searchText = text;
    g_snapshot.searchDone = true;
    g_snapshot.found.clear();
    g_snapshot.foundTotal = number_of(document, "total");
    const json& items = member_of(document, "items");
    if (items.is_array()) {
        for (const json& entry : items) {
            CatalogItem item;
            item.id = text_of(entry, "id");
            item.type = text_of(entry, "type");
            item.tier = number_of(entry, "tier");
            item.maxStack = number_of(entry, "maxStack", 1);
            g_snapshot.found.push_back(std::move(item));
        }
    }
    ++g_version;
}

void run_kind_search(const std::string& text) {
    const network::BackendReply reply = request(
        "GET", "/admin/v1/catalog/creatures?limit=" + std::to_string(kSearchRows) + "&q=" + network::url_encode(text), {});
    const json document = reply.status == 200 ? json::parse(reply.body, nullptr, false) : json();
    const Guard guard;
    g_snapshot.kindSearchDone = true;
    g_snapshot.kinds.clear();
    g_snapshot.kindTotal = number_of(document, "total");
    const json& kinds = member_of(document, "kinds");
    if (kinds.is_array()) {
        for (const json& entry : kinds) {
            CreatureKind kind;
            kind.id = text_of(entry, "id");
            kind.name = text_of(entry, "name");
            kind.family = text_of(entry, "family");
            kind.level = number_of(entry, "level", 1);
            kind.health = number_of(entry, "health");
            kind.spawnPoints = number_of(entry, "spawnPoints");
            g_snapshot.kinds.push_back(std::move(kind));
        }
    }
    ++g_version;
}

void run_places() {
    const network::BackendReply reply = request("GET", "/admin/v1/catalog/places", {});
    const json document = reply.status == 200 ? json::parse(reply.body, nullptr, false) : json();
    const Guard guard;
    g_snapshot.places.clear();
    g_snapshot.placesLoaded = flag_of(document, "loaded");
    // A reply that is not a list (no backend, an older backend): the window asks again later.
    g_snapshot.placesDone = document.is_object();
    const json& places = member_of(document, "places");
    if (places.is_array()) {
        for (const json& entry : places) {
            const json& position = member_of(entry, "position");
            if (!position.is_array() || position.size() != 3 || !position[0].is_number() || !position[1].is_number()
                || !position[2].is_number()) {
                continue;
            }
            TravelPlace place;
            place.id = text_of(entry, "id");
            place.kind = text_of(entry, "kind");
            place.name = text_of(entry, "name");
            place.territory = text_of(entry, "territory");
            for (std::size_t axis = 0; axis < 3; ++axis) {
                place.position[axis] = position[axis].get<float>();
            }
            g_snapshot.places.push_back(std::move(place));
        }
    }
    ++g_version;
}

void run_command(const PendingCommand& command) {
    // The character is the character of the command, not the selected character of this
    // moment: a state reply for a different character can come while the command waits.
    const json arguments = json::parse(command.arguments, nullptr, false);
    const json body = {{"command", command.name},
                       {"character", command.character},
                       {"args", arguments.is_object() ? arguments : json::object()}};
    const network::BackendReply reply = request("POST", "/admin/v1/commands", body.dump());
    const json document = json::parse(reply.body, nullptr, false);
    std::string text = problem_of(reply);
    if (reply.status == 200) {
        text = text_of(document, "applied") == "stored" ? "done (the character is not in the world: its rows changed)" : "done";
    } else if (text.empty() || reply.status == 404) {
        const std::string message = text_of(document, "message");
        text = message.empty() ? "the backend gave the status " + std::to_string(reply.status) : message;
    }
    // The log names the command and the result. It has no token: the token is only in the request header.
    core::log::write("tool window: command %s: status %d", command.name.c_str(), reply.status);
    const Guard guard;
    g_snapshot.busy = false;
    g_snapshot.result = command.label + ": " + text;
    g_snapshot.resultIsError = reply.status != 200;
    ++g_version;
}

DWORD WINAPI worker(LPVOID) noexcept {
    for (;;) {
        WaitForSingleObject(g_wake, g_active.load() ? kPollMilliseconds : INFINITE);
        // A request must never stop the worker: an error of the JSON library ends only the step.
        try {
            PendingCommand command;
            std::string searchText;
            std::string kindSearchText;
            bool hasCommand = false;
            bool hasSearch = false;
            bool hasKindSearch = false;
            bool hasPlaces = false;
            {
                const Guard guard;
                hasCommand = std::exchange(g_commandPending, false);
                if (hasCommand) {
                    command = g_command;
                }
                hasSearch = std::exchange(g_searchPending, false);
                if (hasSearch) {
                    searchText = g_wantedSearch;
                }
                hasKindSearch = std::exchange(g_kindSearchPending, false);
                if (hasKindSearch) {
                    kindSearchText = g_wantedKindSearch;
                }
                hasPlaces = std::exchange(g_placesPending, false);
            }
            if (hasCommand) {
                run_command(command);
            }
            if (hasSearch) {
                run_search(searchText);
            }
            if (hasKindSearch) {
                run_kind_search(kindSearchText);
            }
            if (hasPlaces) {
                run_places();
            }
            if (g_active.load() || hasCommand) {
                refresh_state();
            }
        } catch (...) {
            const Guard guard;
            g_snapshot.busy = false;
            g_snapshot.problem = "A reply of the backend did not have the expected form.";
            ++g_version;
        }
    }
}

} // namespace

void start() noexcept {
    char token[256]{};
    const DWORD length = GetEnvironmentVariableA("SOULWARDEN_ADMIN_TOKEN", token, sizeof(token));
    // The token stays only in the memory of the hook: a process that the game starts does not get it.
    SetEnvironmentVariableA("SOULWARDEN_ADMIN_TOKEN", nullptr);
    {
        const Guard guard;
        g_snapshot.configured = length >= kTokenMinimum && length < sizeof(token);
        if (!g_snapshot.configured) {
            g_snapshot.problem = "The launcher gave no token for the admin interface of the backend.";
        }
    }
    if (length < kTokenMinimum || length >= sizeof(token)) {
        core::log::write("tool window: no token of the admin interface (SOULWARDEN_ADMIN_TOKEN): the window shows no data");
        return;
    }
    g_tokenHeader = std::string("X-Soulwarden-Admin: ") + token;
    SecureZeroMemory(token, sizeof(token));
    g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    const HANDLE thread = g_wake != nullptr ? CreateThread(nullptr, 0, worker, nullptr, 0, nullptr) : nullptr;
    if (thread == nullptr) {
        const Guard guard;
        g_snapshot.configured = false;
        g_snapshot.problem = "The worker of the tool window did not start.";
        return;
    }
    CloseHandle(thread);
}

void set_active(bool active) noexcept {
    g_active = active;
    if (active && g_wake != nullptr) {
        SetEvent(g_wake);
    }
}

bool read(Snapshot& snapshot, std::uint64_t& version) noexcept {
    const Guard guard;
    if (version == g_version) {
        return false;
    }
    snapshot = g_snapshot;
    version = g_version;
    return true;
}

void select_character(const std::string& id) noexcept {
    {
        const Guard guard;
        g_wantedCharacter = id;
    }
    if (g_wake != nullptr) {
        SetEvent(g_wake);
    }
}

void search(const std::string& text) noexcept {
    {
        const Guard guard;
        g_wantedSearch = text;
        g_searchPending = true;
    }
    if (g_wake != nullptr) {
        SetEvent(g_wake);
    }
}

void search_kinds(const std::string& text) noexcept {
    {
        const Guard guard;
        g_wantedKindSearch = text;
        g_kindSearchPending = true;
    }
    if (g_wake != nullptr) {
        SetEvent(g_wake);
    }
}

void load_places() noexcept {
    {
        const Guard guard;
        g_placesPending = true;
    }
    if (g_wake != nullptr) {
        SetEvent(g_wake);
    }
}

void command(const std::string& character,
             const char* name,
             const std::string& arguments,
             const std::string& label) noexcept {
    {
        const Guard guard;
        if (g_snapshot.busy || character.empty() || g_wake == nullptr) {
            return;
        }
        g_snapshot.busy = true;
        g_command = {character, name, arguments, label};
        g_commandPending = true;
        ++g_version;
    }
    SetEvent(g_wake);
}

} // namespace soulwarden::client::toolwindow::model

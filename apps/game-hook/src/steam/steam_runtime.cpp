#include "steam/steam_runtime.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <utility>
#include <vector>

#include "core/logging/log.h"

// Process-local replacement for the Steam client interfaces that the game uses.
// The game is built with MSVC, so each interface is a hand-built table of
// Microsoft x64 functions with the object pointer in the first argument.

namespace soulwarden::steam {
namespace {

using Slot = std::uint64_t (*)(void*, std::uint64_t, std::uint64_t, std::uint64_t);

enum InterfaceId : int { kUser, kFriends, kUtils, kApps, kInterfaceCount };

constexpr const char* kInterfaceNames[kInterfaceCount] = {"User", "Friends", "Utils", "Apps"};
constexpr std::size_t kSlotCount = 96;

std::atomic<std::uint32_t> g_reported[kInterfaceCount][kSlotCount]{};

/** Records the first calls to a method that has no implementation. */
std::uint64_t report_unknown(int interfaceId, int slot, std::uint64_t a, std::uint64_t b) noexcept {
    if (g_reported[interfaceId][slot].fetch_add(1) < 3) {
        core::log::write("steam: unimplemented %s slot %d (a=0x%llx b=0x%llx)",
                         kInterfaceNames[interfaceId],
                         slot,
                         static_cast<unsigned long long>(a),
                         static_cast<unsigned long long>(b));
    }
    return 0;
}

template <int Interface, int Index>
std::uint64_t unknown_slot(void*, std::uint64_t a, std::uint64_t b, std::uint64_t) noexcept {
    return report_unknown(Interface, Index, a, b);
}

template <int Interface, std::size_t... Index>
constexpr std::array<Slot, sizeof...(Index)> make_table(std::index_sequence<Index...>) noexcept {
    return {&unknown_slot<Interface, static_cast<int>(Index)>...};
}

struct Interface {
    Slot* table;
};

std::array<Slot, kSlotCount> g_tables[kInterfaceCount] = {
    make_table<kUser>(std::make_index_sequence<kSlotCount>{}),
    make_table<kFriends>(std::make_index_sequence<kSlotCount>{}),
    make_table<kUtils>(std::make_index_sequence<kSlotCount>{}),
    make_table<kApps>(std::make_index_sequence<kSlotCount>{}),
};
Interface g_interfaces[kInterfaceCount] = {
    {g_tables[kUser].data()},
    {g_tables[kFriends].data()},
    {g_tables[kUtils].data()},
    {g_tables[kApps].data()},
};

template <typename Function> void set_slot(int interfaceId, int slot, Function function) noexcept {
    g_tables[interfaceId][static_cast<std::size_t>(slot)] = reinterpret_cast<Slot>(function);
}

// ---- callbacks -------------------------------------------------------------------------------

struct Registration {
    void* object;
    int callbackId;
};

struct Pending {
    int callbackId;
    std::vector<std::uint8_t> payload;
};

std::mutex g_mutex;
std::vector<Registration> g_registrations;
std::vector<Pending> g_pending;
std::atomic<std::uintptr_t> g_generation{0};
std::atomic<std::uint32_t> g_nextTicket{1};

// ---- identity --------------------------------------------------------------------------------

/** High 32 bits of the Steam id of an individual account in the public universe. */
constexpr std::uint64_t kIndividualAccountBits = 0x01100001ULL;
/** Decimal digits of a Steam id 64 of an individual account. */
constexpr DWORD kSteamIdDigits = 17;
/** A persona name of Steam has 32 characters at most, and a character has 4 bytes at most in UTF-8. */
constexpr std::size_t kPersonaNameCapacity = 32 * 4 + 1;

std::uint64_t g_steamId = kLocalSteamId;
char g_personaName[kPersonaNameCapacity] = "Soulwarden";

/**
 * Reads the Steam account that the launcher supplies in SOULWARDEN_STEAM_ID (decimal text) and
 * SOULWARDEN_PERSONA_NAME. Without a correct Steam id the fallback identity stays. The log gets
 * only the last 4 digits of the Steam id and the size of the name.
 */
void read_identity() noexcept {
    const char* source = "fallback identity";
    char digits[32]{};
    if (GetEnvironmentVariableA("SOULWARDEN_STEAM_ID", digits, sizeof(digits)) == kSteamIdDigits) {
        char* end = nullptr;
        const std::uint64_t value = std::strtoull(digits, &end, 10);
        if (*end == '\0' && (value >> 32) == kIndividualAccountBits) {
            g_steamId = value;
            source = "launcher";
        }
    }
    // The client reads a persona name as UTF-8 text.
    wchar_t wide[kPersonaNameCapacity]{};
    const DWORD length = GetEnvironmentVariableW(L"SOULWARDEN_PERSONA_NAME", wide, kPersonaNameCapacity);
    char name[kPersonaNameCapacity]{};
    if (length != 0 && length < kPersonaNameCapacity
        && WideCharToMultiByte(CP_UTF8, 0, wide, -1, name, sizeof(name), nullptr, nullptr) > 1) {
        std::memcpy(g_personaName, name, sizeof(name));
    }
    core::log::write("steam: identity Steam id ...%04u, persona name of %u bytes (%s)",
                     static_cast<unsigned>(g_steamId % 10000),
                     static_cast<unsigned>(std::strlen(g_personaName)),
                     source);
}

// ---- ISteamUser023 ---------------------------------------------------------------------------

constexpr int kCallbackGetAuthSessionTicketResponse = 163;
constexpr int kCallbackGetTicketForWebApiResponse = 168;
constexpr char kTicketMarker[] = "SOULWARDEN-LOCAL-TICKET";
constexpr int kTicketVersion = 1;
/** Marker, version, Steam id, 3 separators, and a persona name of 128 bytes in hexadecimal text. */
constexpr std::uint32_t kLocalTicketCapacity = 320;

/**
 * Writes the local ticket: "SOULWARDEN-LOCAL-TICKET;1;<Steam id>;<persona name in hexadecimal text>".
 * The client sends it to the backend, and the backend reads the account from it. It carries
 * no Steam credential.
 * @return The ticket size. A ticket that is larger than the capacity is cut.
 */
std::uint32_t write_local_ticket(std::uint8_t* output, std::uint32_t capacity) noexcept {
    char ticket[kLocalTicketCapacity];
    int size = snprintf(ticket,
                        sizeof(ticket),
                        "%s;%d;%llu;",
                        kTicketMarker,
                        kTicketVersion,
                        static_cast<unsigned long long>(g_steamId));
    for (const char* character = g_personaName; *character != '\0'; ++character) {
        size += snprintf(ticket + size,
                         sizeof(ticket) - static_cast<std::size_t>(size),
                         "%02x",
                         static_cast<unsigned>(static_cast<unsigned char>(*character)));
    }
    const std::uint32_t length = std::min(static_cast<std::uint32_t>(size), capacity);
    std::memcpy(output, ticket, length);
    return length;
}

std::int32_t user_get_handle(void*) noexcept {
    return 1;
}

bool user_logged_on(void*) noexcept {
    return true;
}

std::uint64_t* user_get_steam_id(void*, std::uint64_t* output) noexcept {
    *output = g_steamId;
    return output;
}

std::uint32_t user_get_auth_session_ticket(void*,
                                           void* ticket,
                                           int capacity,
                                           std::uint32_t* size,
                                           const void*) noexcept {
    const std::uint32_t handle = g_nextTicket.fetch_add(1);
    std::uint32_t length = 0;
    if (ticket != nullptr && capacity > 0) {
        length = write_local_ticket(static_cast<std::uint8_t*>(ticket), static_cast<std::uint32_t>(capacity));
    }
    if (size != nullptr) {
        *size = length;
    }
    struct Response {
        std::uint32_t handle;
        std::int32_t result;
    } response{handle, 1};
    post_callback(kCallbackGetAuthSessionTicketResponse, &response, sizeof(response));
    core::log::write("steam: GetAuthSessionTicket -> handle %u, %u bytes", handle, length);
    return handle;
}

std::uint32_t user_get_auth_ticket_for_web_api(void*, const char* identity) noexcept {
    const std::uint32_t handle = g_nextTicket.fetch_add(1);
    struct Response {
        std::uint32_t handle;
        std::int32_t result;
        std::int32_t size;
        std::uint8_t ticket[2560];
    } response{};
    response.handle = handle;
    response.result = 1;
    response.size = static_cast<std::int32_t>(write_local_ticket(response.ticket, sizeof(response.ticket)));
    post_callback(kCallbackGetTicketForWebApiResponse, &response, sizeof(response));
    core::log::write("steam: GetAuthTicketForWebApi(%s) -> handle %u, %d bytes",
                     identity != nullptr ? identity : "",
                     handle,
                     response.size);
    return handle;
}

void user_cancel_auth_ticket(void*, std::uint32_t handle) noexcept {
    core::log::write("steam: CancelAuthTicket(%u)", handle);
}

std::int32_t user_has_license(void*, std::uint64_t, std::uint32_t) noexcept {
    return 0;
}

// ---- ISteamFriends017 ------------------------------------------------------------------------

const char* friends_get_persona_name(void*) noexcept {
    return g_personaName;
}

std::int32_t friends_get_persona_state(void*) noexcept {
    return 1;
}

// ---- ISteamUtils010 --------------------------------------------------------------------------

std::int32_t utils_get_connected_universe(void*) noexcept {
    return 1;
}

std::uint32_t utils_get_server_real_time(void*) noexcept {
    return static_cast<std::uint32_t>(std::time(nullptr));
}

const char* utils_get_ip_country(void*) noexcept {
    return "US";
}

std::uint32_t utils_get_app_id(void*) noexcept {
    return kAppId;
}

const char* utils_get_ui_language(void*) noexcept {
    return "english";
}

// ---- ISteamApps008 ---------------------------------------------------------------------------

bool apps_true(void*) noexcept {
    return true;
}

bool apps_false(void*) noexcept {
    return false;
}

const char* apps_get_language(void*) noexcept {
    return "english";
}

bool apps_is_subscribed_app(void*, std::uint32_t) noexcept {
    return true;
}

std::int32_t apps_get_build_id(void*) noexcept {
    return 22469132;
}

void install_methods() noexcept {
    set_slot(kUser, 0, user_get_handle);
    set_slot(kUser, 1, user_logged_on);
    set_slot(kUser, 2, user_get_steam_id);
    set_slot(kUser, 13, user_get_auth_session_ticket);
    set_slot(kUser, 14, user_get_auth_ticket_for_web_api);
    set_slot(kUser, 17, user_cancel_auth_ticket);
    set_slot(kUser, 18, user_has_license);

    set_slot(kFriends, 0, friends_get_persona_name);
    set_slot(kFriends, 2, friends_get_persona_state);

    set_slot(kUtils, 2, utils_get_connected_universe);
    set_slot(kUtils, 3, utils_get_server_real_time);
    set_slot(kUtils, 4, utils_get_ip_country);
    set_slot(kUtils, 9, utils_get_app_id);
    set_slot(kUtils, 23, utils_get_ui_language);

    set_slot(kApps, 0, apps_true);
    set_slot(kApps, 1, apps_false);
    set_slot(kApps, 2, apps_false);
    set_slot(kApps, 3, apps_false);
    set_slot(kApps, 4, apps_get_language);
    set_slot(kApps, 5, apps_get_language);
    set_slot(kApps, 6, apps_is_subscribed_app);
    set_slot(kApps, 7, apps_is_subscribed_app);
    set_slot(kApps, 23, apps_get_build_id);
}

} // namespace

bool initialize() noexcept {
    read_identity();
    install_methods();
    g_generation.fetch_add(1);
    core::log::write("steam: runtime started, app %u", kAppId);
    return true;
}

void shutdown() noexcept {
    std::lock_guard lock(g_mutex);
    g_registrations.clear();
    g_pending.clear();
    core::log::write("steam: runtime stopped");
}

UserHandle user_handle() noexcept {
    return 1;
}

void register_callback(void* callback, int callbackId) noexcept {
    core::log::write("steam: register callback id %d object %p", callbackId, callback);
    std::lock_guard lock(g_mutex);
    g_registrations.push_back({callback, callbackId});
}

void unregister_callback(void* callback) noexcept {
    std::lock_guard lock(g_mutex);
    std::erase_if(g_registrations,
                  [callback](const Registration& entry) { return entry.object == callback; });
}

void post_callback(int callbackId, const void* payload, std::uint32_t size) noexcept {
    Pending pending{callbackId, {}};
    pending.payload.assign(static_cast<const std::uint8_t*>(payload),
                           static_cast<const std::uint8_t*>(payload) + size);
    std::lock_guard lock(g_mutex);
    g_pending.push_back(std::move(pending));
}

void run_callbacks() noexcept {
    std::vector<Pending> pending;
    std::vector<Registration> registrations;
    {
        std::lock_guard lock(g_mutex);
        if (g_pending.empty()) {
            return;
        }
        pending.swap(g_pending);
        registrations = g_registrations;
    }
    for (Pending& entry : pending) {
        for (const Registration& registration : registrations) {
            if (registration.callbackId != entry.callbackId) {
                continue;
            }
            // MSVC orders the two Run overloads in reverse: slot 1 is Run(void*).
            using Run = void (*)(void*, void*);
            Run* table = *static_cast<Run**>(registration.object);
            table[1](registration.object, entry.payload.data());
        }
    }
}

void* find_interface(const char* version) noexcept {
    struct Known {
        const char* prefix;
        InterfaceId id;
    };
    static constexpr Known kKnown[] = {
        {"SteamUser", kUser},
        {"SteamFriends", kFriends},
        {"SteamUtils", kUtils},
        {"STEAMAPPS_INTERFACE_VERSION", kApps},
    };
    for (const Known& known : kKnown) {
        if (std::strncmp(version, known.prefix, std::strlen(known.prefix)) == 0) {
            core::log::write("steam: interface %s", version);
            return &g_interfaces[known.id];
        }
    }
    core::log::write("steam: unknown interface %s", version);
    return nullptr;
}

void* context_init(void* contextInitData) noexcept {
    // Layout: initializer function, generation counter, context storage.
    auto** data = static_cast<void**>(contextInitData);
    const std::uintptr_t generation = g_generation.load();
    if (reinterpret_cast<std::uintptr_t>(data[1]) != generation) {
        reinterpret_cast<void (*)(void*)>(data[0])(&data[2]);
        data[1] = reinterpret_cast<void*>(generation);
    }
    return &data[2];
}

} // namespace soulwarden::steam

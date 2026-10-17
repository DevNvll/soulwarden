#pragma once

// The fixture of the interaction cases (stage 2.5a, tests/interaction_tests.cpp; used by the cases of stage 2.5b and of the review of
// stage 2.5a): made-up objects of the form of the file of tools/gamedata/interactions.py, a host with two characters near them, and the
// readers of the replies and of the states. All keys, ids, positions, and achievement bits are made up (the bits 2032 and 3519 have the
// byte places of the bits of the research).

#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "gamedata/interaction_data.h"
#include "server/rep/client_input.h"
#include "support/check.h"
#include "support/fake_client.h"
#include "support/server_host.h"
#include "support/state_decoder.h"
#include "support/test_players.h"

namespace soulwarden::backend::tests::interaction {

using nlohmann::json;
using gamedata::InteractionKind;

constexpr const char* kCharacterA = "1a7e0000-0000-4000-8000-0000000000a1";
constexpr const char* kCharacterB = "1a7e0000-0000-4000-8000-0000000000b1";
constexpr std::uint32_t kInteractorSlot = 26;
constexpr std::uint32_t kInteractorType = 3752;
constexpr std::uint32_t kInteractType = 2930;
constexpr std::uint32_t kAchievementSlot = 66;
constexpr std::uint32_t kAchievementType = 3210;
constexpr std::uint32_t kReply = 3743;
constexpr std::uint32_t kServerCancel = 3746;
constexpr std::uint32_t kCancelMessage = 2473;
constexpr std::uint32_t kPageBit = 2032;
constexpr std::uint32_t kPageNeighborBit = 2033;
constexpr std::uint32_t kSecondPageBit = 3519;
constexpr state::WorldPosition kHere{100.0F, 200.0F, 30.0F};

/** The option rows of the cases, in the sequence of their index (the rows of the user interface database). */
enum OptionIndex : std::uint32_t { kReadLore, kAddLore, kConversation, kChair, kStorage, kPet };

inline gamedata::InteractionOption option_row(const char* name, std::uint8_t action, bool committed, std::uint32_t level = 0) {
    gamedata::InteractionOption row;
    row.name = name;
    row.id = gamedata::interaction_option_id(name);
    row.action = action;
    row.committed = committed;
    row.hold = 0.5F;
    row.requiredLevel = level;
    return row;
}

/** The objects of the cases, by a number. */
enum ObjectTag : std::uint8_t { kPage = 1, kSecondPage, kPerson, kChairObject, kShed, kPetObject, kUnresolvedPage, kFarPage, kLowPage };

inline std::array<std::uint8_t, 16> actor_of(std::uint8_t tag) {
    // A managed actor id (the low 3 bits of the first byte are 3: the rule of the placement ids, research Q1).
    return {static_cast<std::uint8_t>(0x03 | (tag << 3)), 0x11, 0x22, 0x33, 'O', 'B', 'J', 'T', 0, 0, 0, 0, 0, 0, 0, 0};
}

inline std::uint64_t group_of(const std::array<std::uint8_t, 16>& actor) {
    std::uint64_t group = 0;
    for (int index = 7; index >= 0; --index) {
        group = (group << 8) | actor[static_cast<std::size_t>(index)];
    }
    return group;
}

inline gamedata::InteractionObject object_of(std::uint8_t tag, InteractionKind kind, state::WorldPosition place,
                                      std::vector<std::uint32_t> options, std::int32_t lore = -1, std::int32_t capacity = -1) {
    gamedata::InteractionObject object;
    object.key = "0b1ec700-0000-4000-8000-0000000000" + std::string(tag < 16 ? "0" : "") + [&] {
        char digits[3];
        std::snprintf(digits, sizeof(digits), "%x", tag);
        return std::string(digits);
    }();
    object.kind = kind;
    object.slice = "slices/test/object_" + std::to_string(tag) + ".dynamicslice";
    object.asset = {0xA5, 0x5E, 0x70, tag, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01};
    object.sub = 2;
    object.actor = actor_of(tag);
    object.group = group_of(object.actor);
    object.region = "r_+00_+00";
    object.position = place;
    gamedata::InteractionBound bound;
    bound.slot = 2;
    bound.component = 0xC000000000000000ULL + tag;
    bound.target = 0x7100000000000000ULL + tag;
    bound.position = place;
    bound.rangeSq = 25.0F;
    bound.capacity = capacity;
    bound.arc = 180.0F;
    for (const std::uint32_t option : options) {
        bound.options.push_back(gamedata::BoundOption{option, object.group ^ 0x40ULL});
    }
    bound.lore = lore;
    object.bounds.push_back(bound);
    return object;
}

struct DataParts {
    std::vector<gamedata::InteractionOption> options;
    std::vector<gamedata::InteractionAchievement> achievements;
    std::vector<gamedata::InteractionObject> objects;
};

inline DataParts data_parts() {
    DataParts parts;
    parts.options = {option_row("ReadLore", 47, true), option_row("AddLoreToJournal", 50, false), option_row("OpenConversation", 63, true),
                     option_row("ChairInteractionAnimation", 64, true), option_row("OpenStorageOption", 26, true),
                     option_row("PlayPetAnimation", 64, true, 40)};
    parts.achievements = {{"Lore_unlock_Test_Page_01", kPageBit}, {"Lore_unlock_Test_Page_02", kSecondPageBit},
                          {"Lore_unlock_Test_Page_03", kPageNeighborBit}};
    parts.objects = {object_of(kPage, InteractionKind::lorePage, {100.0F, 202.0F, 30.0F}, {kReadLore, kAddLore}, 0),
                     object_of(kSecondPage, InteractionKind::lorePage, {103.0F, 200.0F, 30.0F}, {kReadLore, kAddLore}, 1),
                     object_of(kPerson, InteractionKind::npc, {98.0F, 201.0F, 30.0F}, {kConversation}),
                     object_of(kChairObject, InteractionKind::other, {101.0F, 198.0F, 30.0F}, {kChair}, -1, 1),
                     object_of(kShed, InteractionKind::storage, {99.0F, 199.0F, 30.0F}, {kStorage}),
                     object_of(kPetObject, InteractionKind::other, {102.0F, 202.0F, 30.0F}, {kPet}),
                     object_of(kUnresolvedPage, InteractionKind::lorePage, {100.0F, 197.0F, 30.0F}, {kReadLore, kAddLore}),
                     object_of(kFarPage, InteractionKind::lorePage, {100.0F, 260.0F, 30.0F}, {kReadLore, kAddLore}, 1),
                     // 2 m to the north and 6 m below the floor of the characters (a page in a cellar): 3D 6.3 m, horizontal 2 m.
                     object_of(kLowPage, InteractionKind::lorePage, {100.0F, 202.0F, 24.0F}, {kReadLore, kAddLore}, 1)};
    parts.objects[kUnresolvedPage - 1].bounds[0].unresolved = {"lore Test_Missing"};
    return parts;
}

inline bool set_data(gamedata::InteractionData& data) {
    DataParts parts = data_parts();
    return data.set(std::move(parts.options), std::move(parts.achievements), std::move(parts.objects));
}

inline const gamedata::InteractionObject& object_tagged(const gamedata::InteractionData& data, std::uint8_t tag) {
    return data.objects()[tag - 1U];
}

/** @return The block of a request of an option of an object, as the client makes it (live check L1). */
inline InteractionBlock block_for(const gamedata::InteractionData& data, std::uint8_t tag, std::uint32_t option) {
    const gamedata::InteractionObject& object = object_tagged(data, tag);
    const gamedata::InteractionOption& row = data.options()[option];
    InteractionBlock block;
    block.actor = object.actor;
    block.group = object.group;
    block.target = object.bounds[0].target;
    block.action = row.action;
    block.baseAction = row.committed ? row.action : 0;
    block.optionEntity = object.group ^ 0x40ULL;
    block.committed = row.committed;
    block.optionId = row.id;
    block.text88 = "@Test_Object";
    return block;
}

/** @param slice The ids of the player slice of the sessions (stage 2.2: the target of the Mana facet). Null: none. */
inline service::RepServerOptions observing(const gamedata::SliceIds* slice = nullptr) {
    service::RepServerOptions options;
    options.serverVersion = "test-version";
    options.observePlayers = true;
    options.playerSlice = slice;
    return options;
}

/** @return The player of an account and a character that the database has already (a new start of the backend). */
inline Player player_of(state::Store& store, const char* steamId, const char* personaName, const char* characterId) {
    Player player;
    player.steamId = steamId;
    player.personaName = personaName;
    player.characterId = characterId;
    state::AccountOrigin origin{};
    const auto account = store.login_account(steamId, personaName, false, origin);
    if (!account) {
        throw std::runtime_error("interaction test: no account");
    }
    player.account = *account;
    return player;
}

/** A host with the data, the players A and B near the objects, and their clients. */
struct Shop {
    ServerHost host;
    Player a;
    Player b;
    FakeClient clientA{27000};
    FakeClient clientB{27001};
    std::uint16_t sequenceA{1};
    std::uint16_t sequenceB{1};
    state::WorldPosition placeA{kHere};
    state::WorldPosition placeB{101.0F, 200.0F, 30.0F};

    explicit Shop(bool data = true, const std::string& database = ":memory:", bool withB = true, bool existing = false,
                  const gamedata::SliceIds* slice = nullptr)
        : host(observing(slice), database),
          a(existing ? player_of(host.store, kSteamIdA, "Tester A", kCharacterA)
                     : add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Interone", kHere)),
          b(!withB     ? Player{}
            : existing ? player_of(host.store, kSteamIdB, "Tester B", kCharacterB)
                       : add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Intertwo", state::WorldPosition{101.0F, 200.0F, 30.0F})) {
        if (data && !set_data(host.interactions)) {
            throw std::runtime_error("interaction test: the data was not set");
        }
    }

    void enter(bool both = true) {
        host.enter(clientA, a.registration());
        if (both) {
            host.enter(clientB, b.registration());
        }
        // The first reports: the exact position of each character, and the camera (the interest scans start).
        for (int step = 0; step < 3; ++step) {
            report(clientA, std::nullopt);
            if (both) {
                report(clientB, std::nullopt);
            }
        }
        if (both) {
            host.steps(60, {&clientA, &clientB});
        } else {
            host.steps(60, {&clientA});
        }
    }

    /** One input report of a client at its place, with a block or none. */
    void report(FakeClient& client, const std::optional<InteractionBlock>& block) {
        const bool isA = &client == &clientA;
        std::uint16_t& sequence = isA ? sequenceA : sequenceB;
        const state::WorldPosition& place = isA ? placeA : placeB;
        InputEntry entry = report_entry(sequence++, place.x, place.y, place.z);
        entry.interaction = block;
        host.datagram(client.peer, client.input_report(entry));
    }

    /** One report with a block and some steps of the world: the reply goes out. */
    void request(FakeClient& client, const InteractionBlock& block, int steps = 3) {
        report(client, block);
        host.steps(steps, {&clientA, &clientB});
    }

    void end_block(FakeClient& client, int steps = 3) {
        report(client, std::nullopt);
        host.steps(steps, {&clientA, &clientB});
    }

    [[nodiscard]] world::Entity* entity(const char* character) {
        return host.world.find_character(character);
    }
};

/** @return The replies 3743 of a client from a message index on: (action, result). */
inline std::vector<std::pair<unsigned, unsigned>> replies(const FakeClient& client, std::size_t from = 0) {
    std::vector<std::pair<unsigned, unsigned>> found;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        const Sent& sent = client.got[index];
        if (sent.type == kReply && sent.body.size() == 71) {
            found.emplace_back(sent.body[48], sent.body[49]);
        }
    }
    return found;
}

inline std::size_t count_type(const FakeClient& client, std::uint32_t type, std::size_t from = 0) {
    std::size_t count = 0;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        count += client.got[index].type == type ? 1 : 0;
    }
    return count;
}

/** One fragment of a record that a client got: the interest, the fragment. */
struct Seen {
    std::uint32_t interest{0};
    Fragment fragment;
};

inline std::vector<Seen> fragments_of(const FakeClient& client, std::uint32_t slot, std::uint32_t type, std::size_t from = 0) {
    std::vector<Seen> found;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        for (const Record& record : read_bundle(client.got[index].body).records) {
            for (const Fragment& fragment : record.fragments) {
                if (fragment.slot == slot && fragment.type == type) {
                    found.push_back(Seen{record.interest, fragment});
                }
            }
        }
    }
    return found;
}

inline std::uint64_t u64_of(std::span<const std::uint8_t> value) {
    std::uint64_t number = 0;
    for (const std::uint8_t byte : value) {
        number = (number << 8) | byte;
    }
    return number;
}

/** @return The (enabled, users) of the Interact states of a client for an object, from a message index on. */
inline std::vector<std::pair<bool, std::uint32_t>> object_states(const FakeClient& client, std::size_t from = 0) {
    std::vector<std::pair<bool, std::uint32_t>> found;
    for (const Seen& seen : fragments_of(client, 2, kInteractType, from)) {
        const auto enabled = field_of(seen.fragment, 0, 0);
        const auto users = field_of(seen.fragment, 0, 1);
        if (enabled.size() == 1 && users.size() == 4) {
            found.emplace_back(enabled[0] != 0, static_cast<std::uint32_t>(u64_of(users)));
        }
    }
    return found;
}

/** @return The G values of the Interactor states that a client got for an interest (1: its own character). */
inline std::vector<std::uint64_t> interactor_values(const FakeClient& client, std::uint32_t interest, std::size_t from = 0) {
    std::vector<std::uint64_t> found;
    for (const Seen& seen : fragments_of(client, kInteractorSlot, kInteractorType, from)) {
        if (seen.interest == interest) {
            found.push_back(u64_of(field_of(seen.fragment, 0, 0)));
        }
    }
    return found;
}

/** @return The bit indexes of the last achievement vector that a client got (no value: none). */
inline std::optional<std::vector<std::uint32_t>> achievement_bits(const FakeClient& client, std::size_t from = 0) {
    std::optional<std::vector<std::uint32_t>> bits;
    for (const Seen& seen : fragments_of(client, kAchievementSlot, kAchievementType, from)) {
        server::hub::Reader reader(field_of(seen.fragment, 0, 0));
        reader.size();
        if (reader.u8() == 1) {
            reader.size64();
        }
        const std::uint32_t count = reader.size();
        std::vector<std::uint32_t> found;
        for (std::uint32_t position = 0; position < count; ++position) {
            const std::uint8_t byte = reader.u8();
            for (unsigned bit = 0; bit < 8; ++bit) {
                if ((byte & (1U << bit)) != 0) {
                    found.push_back(position * 8 + bit);
                }
            }
        }
        bits = found;
    }
    return bits;
}

inline bool has(const std::vector<std::string>& list, const std::string& text) {
    return std::find(list.begin(), list.end(), text) != list.end();
}

inline constexpr std::uint64_t kNoObject = 0x00000000FFFFFFFFULL;


} // namespace soulwarden::backend::tests::interaction

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "server/hub/wire.h"

// Messages of a client, made from the layouts of the protocol notes. No capture file is a
// source of these bytes: a captured registration has the Steam id and the persona name of an
// identity. All ids in the tests are made up.
//
// This file and fake_client.h use only the wire, the carrier, and the store headers of the
// backend, so that a program can run the same scenario with an older tree of the backend.

namespace soulwarden::backend::tests {

/** Type indexes of the client messages that the tests build (protocol notes 3.4 and 4.7). */
inline constexpr std::uint32_t kRegistrationRequestV3 = 19;
inline constexpr std::uint32_t kClientInput = 1589;

/** Made-up Steam ids of individual accounts. */
inline constexpr const char* kSteamIdA = "76561202255233008";
inline constexpr const char* kSteamIdB = "76561202255233009";
inline constexpr const char* kWorldId = "a2071063-7300-4a2e-9a01-000000000001";

/** Fields of a registration request that the backend reads (protocol notes 3.4). */
struct Registration {
    std::string characterId;
    std::string personaId;
    std::string steamId;
    std::string personaName;
    std::string ticketId{"11111111-2222-4333-8444-555555555555"};
};

/**
 * @return The ticket text of the client data: "steam|" and the hexadecimal text of the local
 *         ticket of the game hook (middleware/steam/local_ticket.h).
 */
inline std::string local_ticket_text(const std::string& steamId, const std::string& personaName) {
    static const char* digits = "0123456789abcdef";
    const auto hex = [](const std::string& text) {
        std::string out;
        for (const unsigned char c : text) {
            out += digits[c >> 4];
            out += digits[c & 15];
        }
        return out;
    };
    return "steam|" + hex("SOULWARDEN-LOCAL-TICKET;1;" + steamId + ";" + hex(personaName));
}

/** Body of RegistrationRequestV3Msg: version map, login token, client data, and the tail. */
inline server::hub::Bytes registration_body(const Registration& fields) {
    server::hub::Writer w;
    w.u32(1);
    w.size(6); // Version map.
    for (std::uint32_t key = 0; key < 6; ++key) {
        w.u32(key);
        w.string("1.400.6031.6004151");
    }
    w.string("");
    // Login token.
    w.string("signature");
    w.string("127.0.0.1:32210");
    w.string(kWorldId);
    w.string(fields.characterId);
    w.string(fields.personaId);
    w.u32(1063730);
    w.string(fields.steamId);
    w.string("channel");
    w.string(fields.ticketId);
    w.string("");
    w.string("");
    w.u32(100); // Issue time.
    w.u32(100); // Generate time.
    w.u32(0);   // Account age.
    w.string("");
    w.u32(10); // Token version.
    w.u8(0);
    w.string("");
    w.u8(1);
    w.u8(0);
    w.string("");
    w.string("");
    w.string("");
    // Client data.
    w.u32(1);
    w.u8(1);
    w.u32(1063730);
    w.string(fields.steamId);
    w.string(local_ticket_text(fields.steamId, fields.personaName));
    w.string("");
    w.u32(0);
    for (int index = 0; index < 5; ++index) {
        w.string("");
    }
    // Two identities in the text form and one flag. The backend does not read them.
    w.u8(0);
    w.string("");
    w.u8(0);
    w.string("");
    w.u8(0);
    return w.data();
}

/** Client frame: crc32 size payload, payload := route:16 bytes, envelope. */
inline server::hub::Bytes client_frame(std::uint32_t type, std::span<const std::uint8_t> body, std::uint8_t route = 0xB1) {
    server::hub::Writer payload;
    for (int index = 0; index < 16; ++index) {
        payload.u8(route);
    }
    payload.u8(0); // Envelope flags.
    payload.u8(1); // A message is present.
    payload.size(type);
    payload.raw(body);
    return server::hub::encode_frame(payload.data());
}

/** Client frame with a route of 16 bytes: the actor id of the receiver. */
inline server::hub::Bytes client_frame_to(std::span<const std::uint8_t> route, std::uint32_t type,
                                          std::span<const std::uint8_t> body) {
    server::hub::Writer payload;
    payload.raw(route);
    payload.u8(0); // Envelope flags.
    payload.u8(1); // A message is present.
    payload.size(type);
    payload.raw(body);
    return server::hub::encode_frame(payload.data());
}

/** The height of a world position on the wire: 16 bits of the range -100 m to 1000 m. The client cuts the value off. */
inline std::uint16_t wire_height(float height) {
    const float clamped = std::min(std::max(height, -100.0F), 1000.0F);
    return static_cast<std::uint16_t>((clamped + 100.0F) / 1100.0F * 65535.0F);
}

/** The height that a reader gets from the 16 bits of the wire. */
inline float height_of_wire(std::uint16_t value) {
    return -100.0F + static_cast<float>(value) / 65535.0F * 1100.0F;
}

/** Flags of an input entry of a client in the world that stands on the ground (live captures). */
inline constexpr std::uint64_t kInputFlagsWorld = 0x20004F0ULL;
/** The flag that the game hook sets: the entry has the position of the character. */
inline constexpr std::uint64_t kInputFlagPositionBit = 0x8000ULL;

/** Bits of the action words of an input entry: the attack inputs and the weapon keys (protocol notes 4.7). */
inline constexpr unsigned kActionAttack = 555;
inline constexpr unsigned kActionAttackHold = 191;
inline constexpr unsigned kActionAttackRelease = 567;
inline constexpr unsigned kActionWeapon1 = 9;
inline constexpr unsigned kActionWeapon2 = 355;
/** The press of the three keys of the ability bar (Q, R, F), their virtual bits, and the hold and release bits of Q (stage 3.3a, live check L1). */
inline constexpr unsigned kActionAbility1 = 402;
inline constexpr unsigned kActionAbility2 = 115;
inline constexpr unsigned kActionAbility3 = 139;
inline constexpr unsigned kActionAbility1Virtual = 479;
inline constexpr unsigned kActionAbility1Hold = 39;
inline constexpr unsigned kActionAbility1Release = 211;

/**
 * One shot of an input entry: an item of its second list (192 bytes, protocol notes 4.27): the origin (0), the direction
 * (12), the point of a hit near the muzzle (36), its receiver (92: an actor id, an entity group id, a facet target), the
 * charge (172), the ammunition (176), the attack (180), and the row (184). The other bytes are 0.
 */
struct InputShotItem {
    std::array<float, 3> origin{};
    std::array<float, 3> direction{-1.0F, 0.0F, 0.0F};
    std::uint32_t attack{0};
    std::array<float, 3> point{};
    std::array<std::uint8_t, 16> receiverActor{};
    std::uint64_t receiver{0xFFFFFFFFULL};
    std::uint64_t facet{0};
    float charge{0.0F};
    std::uint32_t ammo{0};
    std::uint32_t row{0};
};

/** One melee contact of an input entry: an item of its third list (96 bytes). */
struct InputContactItem {
    std::uint32_t provider{0xFFFFFFFFU};
    /** The entity group of the receiver, and the receiver: an actor id, an entity group id, and a facet target. */
    std::array<std::uint8_t, 16> groupActor{};
    std::uint64_t group{0};
    std::array<std::uint8_t, 16> actor{};
    std::uint64_t entity{0};
    std::uint64_t facet{0};
    std::array<float, 3> point{};
    std::array<float, 3> normal{1.0F, 0.0F, 0.0F};
    std::array<float, 3> direction{-1.0F, 0.0F, 0.0F};
};

/**
 * The interaction block of an input entry (mask bit 8; stage 2.5a; server/rep/client_input.h, InputInteraction): the fields that
 * the backend reads, and the parts of a test of the reader. The other values are 0 and the other texts are empty.
 */
struct InteractionBlock {
    std::array<std::uint8_t, 16> actor{};
    std::uint64_t group{0};
    std::uint64_t target{0};
    std::uint8_t action{0};
    std::uint8_t baseAction{0};
    std::uint64_t optionEntity{0};
    bool committed{false};
    std::uint32_t optionId{0};
    /** The text +0x88 (the client sends the display key of the object there, live check L1). */
    std::string text88;
    bool vectors{false};
    /** A value that is not 0 or 1 for the committed flag (a test of the reader). */
    std::uint8_t committedByte{0xFF};
    /** The text +0x48 (a test of the limit of a text). */
    std::string text48;
};

/** Writes a present interaction block (client writer at image RVA 0x35E67E0). */
inline void write_interaction_block(server::hub::Writer& out, const InteractionBlock& block) {
    out.u8(1);
    out.raw(block.actor);
    out.u64(block.group);
    out.u64(block.target);
    out.u8(block.action);
    out.u8(block.baseAction);
    out.u64(block.optionEntity);
    out.u32(0);
    out.string(block.text48);
    out.u32(0);
    out.u32(0);
    out.f32(0.0F);
    out.u8(block.committedByte != 0xFF ? block.committedByte : block.committed ? 1 : 0);
    out.string(block.text88);
    out.string("");
    out.string("");
    out.string("");
    out.f32(0.0F); // holdTime
    out.u8(block.vectors ? 1 : 0);
    if (block.vectors) {
        out.zeros(24);
    }
    out.u8(0);           // +0x150
    out.u32(0);          // +0x154
    out.u8(0);           // +0x158 requiredItemCount
    out.u32(block.optionId);
    out.zeros(16);       // +0x128, +0x44, +0x160, +0x164
    out.size64(0);       // requiredCurrency
    out.u32(0);          // +0x170
    out.size(0);         // requiredLevel
    out.string("");      // +0x178
    out.zeros(5);        // +0x1A4, +0x1A8
    out.u8(0);           // +0x1A9
    out.u32(0);          // +0x1E8
    out.string("");      // +0x1C0
    out.zeros(12);       // +0x1F0, +0x1F8
    out.u32(0);          // +0x200
    out.u8(0);           // +0x204
}

/**
 * The fields of one input entry that a test sets (server/rep/client_input.h, protocol notes
 * 4.7). The default is the entry of the older tests: the look direction and the camera
 * position, no flags, and no position of the character.
 */
struct InputEntry {
    std::uint16_t sequence{1};
    /** Flags of the entry. A value that is not 0 puts the flags field in the entry. */
    std::uint64_t flags{0};
    /** Movement permissions (entry +8), 0 to 255. A negative value: the field is not in the entry. */
    int permissions{-1};
    /** The 16 bits of the distance to the ground (entry +0x2C). A negative value: not in the entry. */
    int groundDistance{-1};
    /** Number of action words (the live client has 19). */
    unsigned actionWords{0};
    /** The action bits that are 1 (bit 0 is the lowest bit of the first word). A bit above the words is not sent. */
    std::vector<unsigned> actionBits;
    /** Step time in seconds (entry +0xC0). 0: the field is not in the entry. */
    float stepTime{0.0F};
    /** True: the entry has the look direction. A camera that looks north with no pitch has none. */
    bool hasLook{true};
    bool hasCamera{true};
    float yaw{0.0F};
    float pitch{0.0F};
    float cameraX{0.0F};
    float cameraY{0.0F};
    float cameraZ{0.0F};
    /** Kind of the target data. 3: none. A different kind adds 17 bytes. */
    std::uint8_t targetKind{3};
    /**
     * The object of mask bit 8 (entry +0x140). -1: the bit is not set. 0: the object is not
     * present (one byte). 1: a present object with empty texts (131 bytes). 2: the same with
     * its two vectors (155 bytes). 3: a present flag that is not 0 or 1.
     */
    int object140{-1};
    /**
     * The interaction block of mask bit 8 (stage 2.5a): with a value, the entry has the bit and this block (it replaces
     * `object140`). `noInteraction`: the entry has the bit and the block with the flag 0.
     */
    std::optional<InteractionBlock> interaction;
    bool noInteraction{false};
    /** The entry has the world position of flag bit 11, the two fields of flag bit 8, and the value of flag bit 14. */
    bool position360{false};
    bool positionPair{false};
    bool value3F8{false};
    /** Value of entry +0x380. No value: the field is not in the entry. A value that is not 0 adds two fields. */
    int value380{-1};
    /** True: the entry has the two fields of a value of entry +0x380 that is not 0. */
    bool fields380{false};
    /**
     * Number of items of the first and of the second list (104 and 192 bytes each, all zero),
     * and the items of the third list.
     */
    std::uint32_t projectileHits{0};
    std::uint32_t shots{0};
    /** Shots with their values: they come after the `shots` items of zeros. */
    std::vector<InputShotItem> shotItems;
    std::vector<InputContactItem> contacts;
    /** A value that is not negative replaces the count of the third list: the entry then has no item for it. */
    int contactCount{-1};
    /** True: the entry has the position of the character (it needs the flag kInputFlagPositionBit). */
    bool hasCharacter{false};
    float characterX{0.0F};
    float characterY{0.0F};
    float characterZ{0.0F};
    /** A value that is not negative replaces the 16 bits of the height of the character. */
    int characterHeight{-1};
    /** True: the entry has the byte of entry +0x470 after the position (the live client sends it). */
    bool value470{false};
    /** Number of bytes that the body loses at its end (a body that is cut). */
    std::size_t cut{0};
};

/**
 * Writes one entry of an input report. An entry is a difference from the entry before it in
 * the report: it sends its flags only when they are not the flags of that entry.
 * @param before The entry before it in the report, or null for the first one.
 */
inline void write_input_entry(server::hub::Writer& w, const InputEntry& entry, const InputEntry* before) {
    const auto position = [](server::hub::Writer& out, float x, float y, std::uint16_t height) {
        out.f32(x);
        out.f32(y);
        out.u16(height);
    };
    std::uint32_t mask = 0;
    server::hub::Writer fields;
    if (entry.flags != (before != nullptr ? before->flags : 0)) {
        mask |= 0x01;
        fields.size64(entry.flags);
    }
    if (entry.permissions >= 0) {
        mask |= 0x02;
        fields.u8(static_cast<std::uint8_t>(entry.permissions));
    }
    if (entry.groundDistance >= 0) {
        mask |= 0x10;
        fields.u16(static_cast<std::uint16_t>(entry.groundDistance));
    }
    // Action words: in pairs, and one word alone when the count is odd.
    std::vector<std::uint32_t> words(entry.actionWords, 0);
    for (const unsigned bit : entry.actionBits) {
        if (bit / 32 < words.size()) {
            words[bit / 32] |= 1U << (bit % 32);
        }
    }
    fields.u8(static_cast<std::uint8_t>(entry.actionWords));
    for (unsigned index = 0; index < entry.actionWords; index += 2) {
        const std::uint64_t high = index + 1 < entry.actionWords ? words[index + 1] : 0;
        fields.size64(words[index] | (high << 32));
    }
    for (int axis = 0; axis < 4; ++axis) {
        fields.u8(0x80);
    }
    if (entry.stepTime != 0.0F) {
        mask |= 0x20;
        fields.f32(entry.stepTime);
    }
    if (entry.hasLook) {
        mask |= 0x40;
        fields.f32(entry.yaw);
        fields.f32(entry.pitch);
    }
    if (entry.hasCamera) {
        mask |= 0x80;
        const float clamped = std::min(std::max(entry.cameraZ, -100.0F), 1000.0F);
        position(fields, entry.cameraX, entry.cameraY,
                 static_cast<std::uint16_t>(std::lround((clamped + 100.0F) / 1100.0F * 65535.0F)));
    }
    fields.u8(entry.targetKind);
    if (entry.targetKind != 3) {
        fields.zeros(17);
    }
    if (entry.interaction || entry.noInteraction) {
        mask |= 0x100;
        if (entry.interaction) {
            write_interaction_block(fields, *entry.interaction);
        } else {
            fields.u8(0);
        }
    } else if (entry.object140 >= 0) {
        mask |= 0x100;
        // present, then (client reader at image RVA 0x35EBC30): 32 + 14 bytes, a text, 8
        // bytes, f32, flag, four texts, f32, the flag of the vectors, [24 bytes], 10 + 16
        // bytes, a number, u32, a number, a text, 5 bytes, flag, u32, a text, 12 bytes, u32, flag.
        fields.u8(entry.object140 == 3 ? 2 : entry.object140 != 0 ? 1 : 0);
        if (entry.object140 == 1 || entry.object140 == 2) {
            fields.zeros(32 + 14 + 1 + 8 + 4 + 1 + 4 + 4);
            fields.u8(entry.object140 == 2 ? 1 : 0);
            if (entry.object140 == 2) {
                fields.zeros(24);
            }
            fields.zeros(10 + 16 + 1 + 4 + 1 + 1 + 5 + 1 + 4 + 1 + 12 + 4 + 1);
        }
    }
    if ((entry.flags & 0x1) != 0) {
        fields.size64(0);
        fields.size64(0);
        fields.size64(0);
    }
    unsigned bit = 9;
    if ((entry.flags & 0x800) != 0) {
        if (entry.position360) {
            mask |= 1U << bit;
            position(fields, 1.0F, 2.0F, 3);
        }
        ++bit;
    }
    if ((entry.flags & 0x100) != 0) {
        if (entry.positionPair) {
            mask |= 3U << bit;
            position(fields, 4.0F, 5.0F, 6);
            fields.zeros(16);
            fields.u64(7);
        }
        bit += 2;
    }
    if (entry.value380 >= 0) {
        mask |= 1U << bit;
        fields.u8(static_cast<std::uint8_t>(entry.value380));
    }
    ++bit;
    // An entry that does not send the value has the value of the entry before it.
    const int value380 = entry.value380 >= 0 ? entry.value380 : before != nullptr ? std::max(before->value380, 0) : 0;
    if (value380 != 0) {
        if (entry.fields380) {
            mask |= 3U << bit;
            position(fields, 8.0F, 9.0F, 10);
            fields.zeros(32);
        }
        bit += 2;
    }
    if ((entry.flags & 0x4000) != 0) {
        if (entry.value3F8) {
            mask |= 1U << bit;
            fields.zeros(8);
        }
        ++bit;
    }
    fields.size(entry.projectileHits);
    fields.zeros(entry.projectileHits * 104);
    fields.size(entry.shots + static_cast<std::uint32_t>(entry.shotItems.size()));
    fields.zeros(entry.shots * 192);
    for (const InputShotItem& shot : entry.shotItems) {
        for (const auto& vector : {shot.origin, shot.direction, std::array<float, 3>{}, shot.point}) {
            for (const float value : vector) {
                fields.f32(value);
            }
        }
        fields.zeros(16 + 4 + 24); // The identifier, its u32, and the owner reference.
        fields.raw(shot.receiverActor);
        fields.u64(shot.receiver);
        fields.u64(shot.facet);
        fields.zeros(24 + 8 + 8 + 4 + 4); // The group, the token, a u64, and two floats.
        fields.f32(shot.charge);
        fields.u32(shot.ammo);
        fields.u32(shot.attack);
        fields.u32(shot.row);
        fields.zeros(4);
    }
    fields.size(entry.contactCount >= 0 ? static_cast<std::uint32_t>(entry.contactCount)
                                        : static_cast<std::uint32_t>(entry.contacts.size()));
    for (const InputContactItem& contact : entry.contacts) {
        fields.u32(contact.provider);
        fields.raw(contact.groupActor);
        fields.u64(contact.group);
        fields.raw(contact.actor);
        fields.u64(contact.entity);
        fields.u64(contact.facet);
        for (const auto& vector : {contact.point, contact.normal, contact.direction}) {
            for (const float value : vector) {
                fields.f32(value);
            }
        }
    }
    if ((entry.flags & kInputFlagPositionBit) != 0) {
        if (entry.hasCharacter) {
            mask |= 1U << bit;
            position(fields, entry.characterX, entry.characterY,
                     entry.characterHeight >= 0 ? static_cast<std::uint16_t>(entry.characterHeight)
                                                : wire_height(entry.characterZ));
        }
        ++bit;
    }
    // The inherited velocity has the next mask bit. It is not in the entry.
    if (entry.value470) {
        mask |= 1U << (bit + 1);
        fields.u8(1);
    }
    w.u8(static_cast<std::uint8_t>(mask));
    w.u8(static_cast<std::uint8_t>(mask >> 8));
    w.u8(static_cast<std::uint8_t>(mask >> 16));
    w.u16(entry.sequence);
    w.raw(fields.data());
}

/**
 * Body of an input report with these entries, the newest one first.
 * @param target The facet target of the action list component, and the entity group id.
 */
inline server::hub::Bytes input_entries_body(const std::vector<InputEntry>& entries,
                                             std::uint64_t target = 0x1111111111111111ULL,
                                             std::uint64_t entity = 0x5050505050505050ULL) {
    server::hub::Writer w;
    w.u64(target);
    w.u64(entity);
    // Header: 4 bytes, 3 numbers, u8, optional flag, u64, u8, 3 numbers, u8, pair count, 4 numbers.
    w.zeros(4);
    w.size64(0);
    w.size64(0);
    w.size64(0);
    w.u8(0);
    w.u8(0);
    w.u64(0);
    w.u8(0);
    w.size64(0);
    w.size64(0);
    w.size64(0);
    w.u8(0);
    w.size64(0);
    w.size64(0);
    w.size64(0);
    w.size64(0);
    w.size64(0);
    w.u8(static_cast<std::uint8_t>(entries.size()));
    for (std::size_t index = 0; index < entries.size(); ++index) {
        write_input_entry(w, entries[index], index == 0 ? nullptr : &entries[index - 1]);
    }
    server::hub::Bytes body = w.data();
    if (!entries.empty()) {
        body.resize(body.size() - std::min(entries.back().cut, body.size()));
    }
    return body;
}

/** Body of an input report with one entry. */
inline server::hub::Bytes input_entry_body(const InputEntry& entry) {
    return input_entries_body({entry});
}

/**
 * Body of an input report with one entry that has the look direction and the camera position
 * (server/rep/client_input.h), and no position of the character: the report of a client with
 * no position report of the game hook. With yaw 0 and pitch 0, a camera at
 * (x + 0.5, y - 4, z + 1.65) is the camera of a character at (x, y, z).
 */
inline server::hub::Bytes input_body(std::uint16_t sequence, float cameraX, float cameraY, float cameraZ, float yaw = 0.0F,
                                     float pitch = 0.0F) {
    InputEntry entry;
    entry.sequence = sequence;
    entry.yaw = yaw;
    entry.pitch = pitch;
    entry.cameraX = cameraX;
    entry.cameraY = cameraY;
    entry.cameraZ = cameraZ;
    return input_entry_body(entry);
}

/**
 * @return The entry of a client with the position report of the game hook, for a character at
 *         (x, y, z): the flags and the fields of a live client that stands, the camera 4 m
 *         behind the character (camera to the north, no pitch), and the position.
 */
inline InputEntry report_entry(std::uint16_t sequence, float x, float y, float z) {
    InputEntry entry;
    entry.sequence = sequence;
    entry.flags = kInputFlagsWorld | kInputFlagPositionBit;
    entry.permissions = 15;
    entry.groundDistance = 180;
    entry.actionWords = 19;
    entry.stepTime = 1.0F / 30.0F;
    entry.hasLook = false;
    entry.cameraX = x + 0.5F;
    entry.cameraY = y - 4.0F;
    entry.cameraZ = z + 1.65F;
    entry.hasCharacter = true;
    entry.characterX = x;
    entry.characterY = y;
    entry.characterZ = z;
    entry.value470 = true;
    return entry;
}

} // namespace soulwarden::backend::tests

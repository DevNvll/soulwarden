#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "server/hub/wire.h"
#include "support/item_decoder.h"
#include "support/paperdoll_decoder.h"
#include "support/progress_decoder.h"

// Decoder of a state bundle from the formats of the protocol notes (4.1 to 4.3, 4.10 to 4.13). It
// reads each field of the state types that the backend sends for the local player, so that a
// test can compare a field and prove that a record has no unknown byte.

namespace soulwarden::backend::tests {

using server::hub::Bytes;

struct Fragment {
    std::uint32_t slot{};
    std::uint32_t type{};
    /** (group, field) to the bytes of the value. */
    std::map<std::pair<unsigned, unsigned>, Bytes> fields;
};

struct Record {
    std::uint32_t interest{};
    std::vector<Fragment> fragments;
};

struct Bundle {
    std::uint64_t sequence{};
    std::uint8_t instance{};
    std::vector<Record> records;
    /** Interest numbers of the control block: the client removes these entities. */
    std::vector<std::uint32_t> stops;
    /** Numbers of the control block after the stop numbers ("pause"). The backend sends none. */
    std::vector<std::uint32_t> pauses;
    /** True when the decoder read the bundle to its last byte. */
    bool complete{false};
};

namespace detail {

/** @return The size of a compressed rotation (0x87B250): a header byte whose bits 0x01, 0x02, 0x04 or 0x08, 0x10, 0x20 give x, y, z; a u16 for each other one. */
inline int rotation_size(std::span<const std::uint8_t> rest) {
    if (rest.empty()) {
        return -1;
    }
    int size = 1;
    for (unsigned axis = 0; axis < 3; ++axis) {
        if ((rest[0] & ((1U << axis) | (8U << axis))) == 0) {
            size += 2;
        }
    }
    return size;
}

/** @return The size of the value of one field, or -1 when the decoder does not know the field. */
inline int value_size(std::uint32_t type, unsigned group, unsigned field, std::span<const std::uint8_t> rest) {
    const auto text_size = [&](std::size_t offset) -> int {
        if (rest.size() <= offset) {
            return -1;
        }
        server::hub::Reader reader(rest.subspan(offset));
        const std::uint32_t length = reader.size();
        return static_cast<int>(offset + reader.position() + length);
    };
    switch (type) {
    case 10: // GdeMetadata: asset id (16 bytes and sub id), actor id (16 bytes).
        return group == 0 && field == 0 ? 20 : group == 0 && field == 1 ? 16 : -1;
    case 3935: // PlayerComponent, group 1 (stage 3.3b-S1): the wall clock pair (fields 6 and 7, u64 each); group 2: character
               // id (flags, text), name (text), connected (flag).
        if (group == 1) {
            return field == 6 || field == 7 ? 8 : -1;
        }
        if (group != 2) {
            return -1;
        }
        return field == 0 ? text_size(1) : field == 1 ? text_size(0) : field == 3 ? 1 : -1;
    case 11: // Movement state.
        if (group != 0) {
            return -1;
        }
        switch (field) {
        case 0:
        case 25:
            return 1; // idRel and idAbs: the low and the high 8 bits of the id of the state.
        case 11:
        case 12: {
            // Compressed quaternion: the header, and one byte for each component that is not
            // the largest one and not zero (bits 1 to 4: x, y, z, w is zero; bits 5 and 6: the
            // largest component).
            if (rest.empty()) {
                return -1;
            }
            int size = 1;
            for (unsigned component = 0; component < 4; ++component) {
                const bool largest = ((rest[0] >> 5) & 3U) == component;
                const bool zero = (rest[0] & (2U << component)) != 0;
                size += !largest && !zero ? 1 : 0;
            }
            return size;
        }
        case 28:
        case 38:
        case 49:
        case 50:
            return 1; // One byte of the time of scope 0.
        case 29:
        case 33: {
            // A blob: its byte count in the size code, then the bytes.
            if (rest.empty()) {
                return -1;
            }
            server::hub::Reader reader(rest);
            const std::uint32_t length = reader.size();
            return reader.ok() ? static_cast<int>(reader.position() + length) : -1;
        }
        case 32: {
            if (rest.empty()) {
                return -1;
            }
            server::hub::Reader reader(rest);
            reader.size();
            return reader.ok() ? static_cast<int>(reader.position()) : -1;
        }
        case 34:
            return 12; // The tag state of the fragments.
        case 13:
        case 15: {
            // State id and sequence id of action layer 0: a number in the size code.
            if (rest.empty()) {
                return -1;
            }
            server::hub::Reader reader(rest);
            reader.size();
            return reader.ok() ? static_cast<int>(reader.position()) : -1;
        }
        case 14:
            return 2;
        case 47:
            return 1;
        case 27:
            return 10;
        case 31:
        case 41:
        case 42:
            return 1;
        case 40:
            return 8;
        default:
            return -1;
        }
    case 15: // Vitals.
        if (group == 0) {
            switch (field) {
            case 0:
            case 1:
            case 4:
            case 6:
            case 8:
            case 9:
            case 10:
                return 4;
            case 5:
            case 7:
            case 13:
                return 2;
            case 2:
                return 18; // vitalsData: flags, two times, a count.
            case 3:
            case 11:
            case 12:
                return 1;
            default:
                return -1;
            }
        }
        return group == 1 ? (field == 0 || field == 3 ? 4 : field == 4 ? 2 : -1) : -1;
    case 5399: // Marker: enabled.
        return group == 0 && field == 0 ? 1 : -1;
    case 3133: // Entitlement (stage 1.1b): the byte vector, the balance map (no entry), and the mark "received".
    case 3210: // Achievement (stage 1.1b): the byte vector.
        if (group != 0 || (type == 3210 && field != 0) || field > 2) {
            return -1;
        }
        if (field == 2) {
            return 1;
        }
        {
            // The full form of a container: changes (0), the version (has, sequence), the count, the elements (a byte of a
            // vector; the map of the balances has no element).
            server::hub::Reader reader(rest);
            if (reader.size() != 0 || !reader.ok()) {
                return -1;
            }
            const std::uint8_t has = reader.u8();
            if (has == 1) {
                reader.size64();
            }
            const std::uint32_t count = reader.size();
            if (!reader.ok() || has > 1 || (field == 1 && count != 0)) {
                return -1;
            }
            const std::size_t size = reader.position() + count;
            return size <= rest.size() ? static_cast<int>(size) : -1;
        }
    case 1652: // Mana: current, maximum, regeneration delay, regeneration rate (32-bit floats).
        return group == 0 && field < 4 ? 4 : -1;
    case 4297: // Stamina (stage 3.4f-B): cur, max, winded, regen, multMax, multRegen (32-bit floats).
        return group == 0 && field < 6 ? 4 : -1;
    case 1195: // Appearance: 12 fields of one byte.
        return group == 0 && field < 12 ? 1 : -1;
    case 899: // Progression: level and bonus level (group 0), experience and rested experience (group 1).
        return (group == 0 || group == 1) && field < 2 ? 4 : -1;
    case 3786: // Currency: the amount, 64 bits.
        return group == 0 && field == 0 ? 8 : -1;
    case 1755: { // Container: the vector of the slots.
        if (group != 0 || field != 0) {
            return -1;
        }
        const WireContainer container = read_container(rest);
        return container.ok ? static_cast<int>(container.size) : -1;
    }
    case 3183: // Paperdoll: the lists and the maps of the equipment.
        return paperdoll_value_size(group, field, rest);
    case 129: // Attribute: the values, the two bonus containers, the points.
        return progress_wire::attribute_value_size(group, field, rest);
    case 911: // Categorical progression: the three lists of the masteries and the trade skills.
        return progress_wire::categorical_value_size(group, field, rest);
    case 3681: // Progression points: the pools and the nodes of the weapon trees.
        return progress_wire::points_value_size(group, field, rest);
    case 185: // Ability: the bar of each weapon.
        return progress_wire::ability_value_size(group, field, rest);
    case 2932: // CooldownTimers (stage 3.3a): the three maps of the places of the bar, and the item map (stage 1.3b), 20 bytes for each entry.
        return group == 0 && field < 4 ? progress_wire::list_size(rest, 20, 1000) : -1;
    case 5257: // Charge (stage 1.3b): the percent of the heart rune charge, one byte.
        return group == 0 && field == 0 ? 1 : -1;
    case 4236: // StatusEffects (stages 3.3a and 3.3b-S1): group 3, the full map (14 bytes) and the compact map (3 bytes);
               // group 1, the full map (14), the lightweight map (10), and the tray (17).
        if (group == 3 && field < 2) {
            return progress_wire::list_size(rest, field == 0 ? 14 : 3, 1000);
        }
        if (group == 1 && field < 3) {
            return progress_wire::list_size(rest, field == 0 ? 14 : field == 1 ? 10 : 17, 1000);
        }
        return -1;
    case 1525: // StatMultiplierTable (stage 3.3b-S2a): group 1 field 0, a full table of entries of 6 bytes.
        if (group == 1 && field == 0) {
            server::hub::Reader reader(rest);
            if (reader.size() != 0) {
                return -1; // a delta form: the backend sends none
            }
            if (reader.u8() != 0) {
                (void)reader.size64();
            }
            const std::uint32_t count = reader.size();
            const std::size_t total = reader.position() + static_cast<std::size_t>(count) * 6;
            return total <= rest.size() ? static_cast<int>(total) : -1;
        }
        return -1;
    case 2912: { // Spell (stage 3.3b-S2a): the key, two positions, two rotations, three references, the time, the count, the scale.
        if (group != 0) {
            return -1;
        }
        const auto rotation = [&]() { return rotation_size(rest); };
        switch (field) {
        case 0:
        case 9:
        case 10:
            return 4;
        case 1:
        case 3:
            return 10;
        case 2:
        case 4:
            return rotation();
        case 5:
        case 6:
        case 7:
            return 24;
        case 8:
            return 8;
        default:
            return -1;
        }
    }
    case 12: // GatherableController (stage 3.6a): the node state (u8: Healthy 4, Depleted 8).
        return group == 0 && field == 0 ? 1 : -1;
    case 3139: // DelayedEvent (stage 3.3b-S2a): the remaining time (a half float) and the completed flag.
        return group == 0 && field == 0 ? 2 : group == 0 && field == 1 ? 1 : -1;
    case 13: // PositionInTheWorld (stage 3.3b-S2b, the trap): the position (x, y, a u16 height) and the rotation.
        return group == 0 && field == 0 ? 10 : group == 0 && field == 1 ? rotation_size(rest) : -1;
    case 3752: // Interactor (stage 2.5a): the cached object G (u64), and the enabled flag of group 1.
        return group == 0 && field == 0 ? 8 : group == 1 && field == 0 ? 1 : -1;
    case 3152: // Faction (stage QG7): group 1 field 3 the time of the last change (u64), field 4 the count (u16); group 2 field 0 the faction.
        return group == 1 && field == 3 ? 8 : group == 1 && field == 4 ? 2 : group == 2 && field == 0 ? 1 : -1;
    case 4176: // Social (stage QG7): the title (u32), the pronoun (u8), the chatting state (u32); the four maps of group 1, each an empty
               // full snapshot (00 01 01 00: the backend sends no other form).
        if (group == 0) {
            return field == 0 || field == 2 ? 4 : field == 1 ? 1 : -1;
        }
        if (group == 1 && (field == 0 || field == 4 || field == 5 || field == 6)) {
            return rest.size() >= 4 && rest[0] == 0x00 && rest[1] == 0x01 && rest[2] == 0x01 && rest[3] == 0x00 ? 4 : -1;
        }
        return -1;
    case 2930: // Interact (stage 2.5a): Enabled (a flag) and HasInteractors (u32); CooldownUpdates is not sent.
        return group == 0 && field == 0 ? 1 : group == 0 && field == 1 ? 4 : -1;
    case 81: { // Gathering (stage 3.5b.1): group 1, nine fields (status, remaining, speed, num, ref, water ref, active, G, sync ref).
        static constexpr int kWidths[] = {1, 4, 4, 1, 4, 4, 1, 8, 4};
        return group == 1 && field < 9 ? kWidths[field] : -1;
    }
    case 3857: { // Objectives (stage 1.8a): group 1, field 0 the timer map (17-byte entries), 1 tracked (8), 2 completed (8, stage 1.8b),
                 // 3 active (56), 4 tasks (18).
        if (group != 1 || field > 5) {
            return -1;
        }
        server::hub::Reader reader(rest);
        if (reader.size() != 0) {
            return -1;
        }
        if (reader.u8() == 1) {
            reader.size64();
        }
        const std::uint32_t count = reader.size();
        if (!reader.ok()) {
            return -1;
        }
        // Stage QG5c2: field 5, the encounter roots of the character (8-byte G).
        const std::uint32_t element = field == 0 ? 17U : field == 1 || field == 2 || field == 5 ? 8U : field == 3 ? 56U : 18U;
        return static_cast<int>(reader.position() + element * count);
    }
    case 5606: // SeasonsRewardsTrackedStat (stage 1.8a): group 0 field 0, initialized (one byte).
        return group == 0 && field == 0 ? 1 : -1;
    case 3829: { // ObjectiveInteractor (stage QG6a): the provider (16), the expiration (u64), the mission vector (52-byte elements with
                 // no ingredient), the town map (territory:u16 seed:u32 count:size u32* count:size u32*).
        if (group != 0 || field > 3) {
            return -1;
        }
        if (field == 0 || field == 1) {
            return field == 0 ? 16 : 8;
        }
        server::hub::Reader reader(rest);
        if (field == 2) {
            if (reader.size() != 0) {
                return -1;
            }
            if (reader.u8() == 1) {
                reader.size64();
            }
            const std::uint32_t count = reader.size();
            return reader.ok() ? static_cast<int>(reader.position() + 52U * count) : -1;
        }
        const std::uint32_t entries = reader.size();
        for (std::uint32_t entry = 0; entry < entries && reader.ok(); ++entry) {
            reader.u16();
            reader.u32();
            for (int list = 0; list < 2; ++list) {
                const std::uint32_t values = reader.size();
                reader.raw(4U * values);
            }
        }
        return reader.ok() ? static_cast<int>(reader.position()) : -1;
    }
    case 497: { // GameEvent (stage QG6b): group 0 field 1 dailyBonusesUsed, a full vector of {modifier:u32 used:u8} (5-byte elements).
        if (group != 0 || field != 1) {
            return -1;
        }
        server::hub::Reader reader(rest);
        if (reader.size() != 0) {
            return -1;
        }
        if (reader.u8() == 1) {
            reader.size64();
        }
        const std::uint32_t count = reader.size();
        return reader.ok() ? static_cast<int>(reader.position() + 5U * count) : -1;
    }
    case 1746: { // TerritoryInterface (stage QG6a): group 1 field 0, the progression data: the active map (17-byte entries), the completed
                 // projects (12), the default project (16), the status effects (12).
        if (group != 1 || field != 0) {
            return -1;
        }
        server::hub::Reader reader(rest);
        reader.raw(17U * reader.size());
        reader.raw(12U * reader.size());
        reader.raw(16);
        reader.raw(12U * reader.size());
        return reader.ok() ? static_cast<int>(reader.position()) : -1;
    }
    case 3086: { // LandClaimManager (stage QG6a): full vectors (00 hasVersion [version] count) of the claims: field 2 the keys (u16),
                 // 3, 9, 10, 11, 17 one byte, 4, 5, 7, 13 a time point (u64), 6 a u32, 12 the governance (18 bytes), 15 the
                 // progression summaries, 16 the owners (texts inside).
        static constexpr int kElement[18] = {-1, -1, 2, 1, 8, 8, 4, 8, -1, 1, 1, 1, 18, 8, -1, 0, 0, 1};
        if (group != 0 || field > 17 || kElement[field] < 0) {
            return -1;
        }
        server::hub::Reader reader(rest);
        if (reader.size() != 0) {
            return -1;
        }
        if (reader.u8() == 1) {
            reader.size64();
        }
        const std::uint32_t count = reader.size();
        for (std::uint32_t element = 0; element < count && reader.ok(); ++element) {
            if (field == 15) {
                reader.raw(5U * reader.size());
                reader.raw(9U * reader.size());
                reader.u8();
            } else if (field == 16) {
                reader.raw(16);
                reader.raw(reader.size());
                reader.raw(2 + 16 + 2 + 16);
                reader.raw(reader.size());
                reader.raw(1 + 8);
            } else {
                reader.raw(static_cast<std::size_t>(kElement[field]));
            }
        }
        return reader.ok() ? static_cast<int>(reader.position()) : -1;
    }
    case 6786: // The manager state of an encounter root (stage QG5c2): group 0 field 0, the status (u32).
        return group == 0 && field == 0 ? 4 : -1;
    case 14: // VariationData (stage QG5c2): group 0 field 0, the variant index (u8); field 1, the variant index (u16).
        return group == 0 && field == 0 ? 1 : group == 0 && field == 1 ? 2 : -1;
    case 2133: { // The stage state of an encounter root (stage QG5c2): group 0 field 0, count:size then (sequence:u32 status:u32) each.
        if (group != 0 || field != 0) {
            return -1;
        }
        server::hub::Reader reader(rest);
        const std::uint32_t count = reader.size();
        if (!reader.ok()) {
            return -1;
        }
        return static_cast<int>(reader.position() + 8U * count);
    }
    case 205: { // Crafting (stage 3.7b): field 0, the cooldown map (13-byte entries); field 1, the gear score bonus map (3-byte entries).
        if (group != 0 || field > 1) {
            return -1;
        }
        server::hub::Reader reader(rest);
        if (reader.size() != 0) {
            return -1;
        }
        if (reader.u8() == 1) {
            reader.size64();
        }
        const std::uint32_t count = reader.size();
        if (!reader.ok()) {
            return -1;
        }
        return static_cast<int>(reader.position() + (field == 0 ? 13U : 3U) * count);
    }
    case 982: { // LootTracker (stage 3.5b.1): field 0, the full form of the map (changes 0, no version, count, 58-byte entries).
        if (group != 0 || field != 0) {
            return -1;
        }
        server::hub::Reader reader(rest);
        if (reader.size() != 0) {
            return -1;
        }
        if (reader.u8() == 1) {
            reader.size64();
        }
        const std::uint32_t count = reader.size();
        if (!reader.ok()) {
            return -1;
        }
        return static_cast<int>(reader.position() + 58U * count);
    }
    case 2938: { // GlobalStorage (stage 3.5b.2): field 0, the full map of an actor (16 bytes) to an item vector (count, items).
        if (group != 0 || field != 0) {
            return -1;
        }
        server::hub::Reader reader(rest);
        if (reader.size() != 0) {
            return -1;
        }
        if (reader.u8() == 1) {
            reader.size64();
        }
        const std::uint32_t keys = reader.size();
        for (std::uint32_t key = 0; key < keys && reader.ok(); ++key) {
            reader.raw(16);
            const std::uint32_t items = reader.size();
            for (std::uint32_t item = 0; item < items && reader.ok(); ++item) {
                WireItem value;
                if (!read_item(reader, value)) {
                    return -1;
                }
            }
        }
        return reader.ok() ? static_cast<int>(reader.position()) : -1;
    }
    case 2027: // LootDrop (stage 3.5b.2): m_rarityLevel u32.
        return group == 0 && field == 0 ? 4 : -1;
    case 3312: { // GameModeParticipant (stage QG9a): field 0 the full map activeGameModes (56-byte entries), field 1 the six bytes of flags.
        if (group != 0 || field > 1) {
            return -1;
        }
        if (field == 1) {
            return 6;
        }
        server::hub::Reader reader(rest);
        if (reader.size() != 0) {
            return -1;
        }
        if (reader.u8() == 1) {
            reader.size64();
        }
        const std::uint32_t count = reader.size();
        if (count > 64) {
            return -1;
        }
        reader.raw(56U * count);
        return reader.ok() ? static_cast<int>(reader.position()) : -1;
    }
    case 1594: // BuildableController (stage 2.5b): currentState (one byte); the other fields are not sent.
        return group == 0 && field == 0 ? 1 : -1;
    case 3217: // Ownership (stage 2.5b): currentPermissionRule (field 4, one byte); the other fields are not sent.
        return group == 0 && field == 4 ? 1 : -1;
    case 16: { // Projectile (stage 3.4f-A): the twenty fields of the research (the fields 11 and 13 are size codes).
        if (group != 0) {
            return -1;
        }
        const auto code = [&]() -> int {
            if (rest.empty()) {
                return -1;
            }
            const std::uint8_t first = rest[0];
            return first < 0x80 ? 1 : first < 0xC0 ? 2 : first < 0xE0 ? 3 : first < 0xF0 ? 4 : 5;
        };
        switch (field) {
        case 0:
            return 10;
        case 1:
        case 3:
        case 5:
        case 6:
        case 15:
            return 2;
        case 2:
        case 18:
            return 6;
        case 4:
        case 7:
        case 10:
        case 17:
            return 8;
        case 8:
        case 9:
        case 19:
            return 1;
        case 11:
        case 13:
            return code();
        case 12:
        case 14:
            return 4;
        default:
            return -1;
        }
    }
    default:
        return -1;
    }
}

inline unsigned group_count(std::uint32_t type) {
    switch (type) {
    case 10:
    case 12:
    case 5399:
    case 1652:
    case 4297:
    case 1195:
    case 3786:
    case 1755:
    case 129:
    case 911:
    case 3681:
    case 185:
    case 2932:
    case 5257:
    case 2912:
    case 3139:
    case 13:
    case 3133:
    case 3210:
    case 2930:
    case 1594:
    case 3217:
    case 16:
    case 982:
    case 2938:
    case 2027:
    case 205:
    case 5606:
    case 6786:
    case 2133:
    case 14:
    case 3829:
    case 3086:
    case 497:
    case 3312:
        return 1;
    case 3752:
    case 81:
    case 3857:
    case 4176:
        return 2;
    case 4236:
    case 3152:
        return 4;
    case 3935:
    case 3183:
    case 1525:
    case 1746:
        return 3;
    case 11:
    case 15:
    case 899:
        return 2;
    default:
        return 0;
    }
}

inline bool read_field(std::span<const std::uint8_t> data, std::size_t& position, Fragment& fragment, unsigned group,
                       unsigned field) {
    const int size = value_size(fragment.type, group, field, data.subspan(position));
    if (size < 0 || position + static_cast<std::size_t>(size) > data.size()) {
        return false;
    }
    fragment.fields[{group, field}] = Bytes(data.begin() + static_cast<std::ptrdiff_t>(position),
                                            data.begin() + static_cast<std::ptrdiff_t>(position + size));
    position += static_cast<std::size_t>(size);
    return true;
}

inline bool read_state(std::span<const std::uint8_t> data, std::size_t& position, Fragment& fragment) {
    const unsigned groups = group_count(fragment.type);
    if (groups == 0 || position >= data.size()) {
        return false;
    }
    const std::uint8_t groupMask = data[position++];
    for (unsigned group = 0; group < groups; ++group) {
        if ((groupMask & (1U << group)) == 0) {
            continue;
        }
        if (fragment.type == 11) {
            // Wide form: one number with one bit for each field.
            server::hub::Reader reader(data.subspan(position));
            const std::uint64_t mask = reader.size64();
            position += reader.position();
            for (unsigned field = 0; field < 64; ++field) {
                if (((mask >> field) & 1U) != 0 && !read_field(data, position, fragment, group, field)) {
                    return false;
                }
            }
            continue;
        }
        // Common form: mask bytes with 7 fields each. The values follow each mask byte.
        for (unsigned base = 0;; base += 7) {
            if (position >= data.size()) {
                return false;
            }
            const std::uint8_t mask = data[position++];
            for (unsigned bit = 0; bit < 7; ++bit) {
                if ((mask & (1U << bit)) != 0 && !read_field(data, position, fragment, group, base + bit)) {
                    return false;
                }
            }
            if ((mask & 0x80) == 0) {
                break;
            }
        }
    }
    return true;
}

} // namespace detail

/** Reads the body of a ReplicatedStateBundle message (type 8). */
inline Bundle read_bundle(std::span<const std::uint8_t> body) {
    Bundle bundle;
    server::hub::Reader reader(body);
    if (reader.u8() != 1) {
        return bundle;
    }
    bundle.sequence = reader.size64();
    bundle.instance = reader.u8();
    reader.u8();
    const std::uint8_t unreliable = reader.u8();
    const std::uint8_t control = reader.u8();
    if (unreliable > 1 || control > 1) {
        return bundle;
    }
    if (control == 1) {
        // Control block: stopCount:size count:size (interest:u16)*count.
        const std::uint32_t stopCount = reader.size();
        const std::uint32_t count = reader.size();
        if (stopCount > count || count > 100) {
            return bundle;
        }
        for (std::uint32_t index = 0; index < count; ++index) {
            (index < stopCount ? bundle.stops : bundle.pauses).push_back(reader.u16());
        }
    }
    const std::uint32_t size = reader.size();
    if (!reader.ok() || reader.position() + size != body.size()) {
        return bundle;
    }
    const auto payload = body.subspan(reader.position());
    std::size_t position = 0;
    while (position < payload.size()) {
        server::hub::Reader head(payload.subspan(position));
        Record record;
        record.interest = head.size();
        const unsigned count = head.u8();
        position += head.position();
        for (unsigned index = 0; index < count; ++index) {
            server::hub::Reader fragmentHead(payload.subspan(position));
            Fragment fragment;
            fragment.slot = fragmentHead.size();
            fragment.type = fragmentHead.size();
            position += fragmentHead.position();
            if (!detail::read_state(payload, position, fragment)) {
                return bundle;
            }
            record.fragments.push_back(std::move(fragment));
        }
        bundle.records.push_back(std::move(record));
    }
    bundle.complete = position == payload.size();
    return bundle;
}

/** @return The fragment of a record for a slot, or null. */
inline const Fragment* fragment_of(const Record& record, std::uint32_t slot) {
    for (const Fragment& fragment : record.fragments) {
        if (fragment.slot == slot) {
            return &fragment;
        }
    }
    return nullptr;
}

/** @return The value of a field of a fragment, or an empty value when the fragment does not have the field. */
inline std::span<const std::uint8_t> field_of(const Fragment& fragment, unsigned group, unsigned field) {
    const auto found = fragment.fields.find({group, field});
    return found == fragment.fields.end() ? std::span<const std::uint8_t>{} : std::span<const std::uint8_t>(found->second);
}

/** @return The attribute state of a fragment of slot 27 with its four fields. */
inline WireAttributes attributes_of(const Fragment& fragment) {
    return fragment.fields.size() == 4 ? read_attributes({field_of(fragment, 0, 0), field_of(fragment, 0, 1),
                                                           field_of(fragment, 0, 2), field_of(fragment, 0, 3)})
                                       : WireAttributes{};
}

/** @return The masteries and the trade skills of a fragment of slot 65 with its three fields. */
inline WireRanks ranks_of(const Fragment& fragment) {
    return fragment.fields.size() == 3
               ? read_ranks({field_of(fragment, 0, 0), field_of(fragment, 0, 1), field_of(fragment, 0, 2)})
               : WireRanks{};
}

/** @return The pools and the nodes of a fragment of slot 67 with its four fields. */
inline WirePools pools_of(const Fragment& fragment) {
    return fragment.fields.size() == 4 ? read_pools({field_of(fragment, 0, 0), field_of(fragment, 0, 1),
                                                     field_of(fragment, 0, 2), field_of(fragment, 0, 3)})
                                       : WirePools{};
}

/** @return The ability bars of a fragment of slot 46. */
inline WireBars bars_of(const Fragment& fragment) {
    return fragment.fields.size() == 1 ? read_bars(field_of(fragment, 0, 0)) : WireBars{};
}

/** @return The fragments of a record as text, for example "(0,10) (9,3935) ". */
inline std::string order_of(const Record& record) {
    std::string text;
    for (const Fragment& fragment : record.fragments) {
        text += "(" + std::to_string(fragment.slot) + "," + std::to_string(fragment.type) + ") ";
    }
    return text;
}

/** @return The text of a field value. `skip` is the number of bytes before the text. */
inline std::string text_of(const Bytes& value, std::size_t skip) {
    server::hub::Reader reader(std::span<const std::uint8_t>(value).subspan(skip));
    return reader.string();
}

inline float float_at(const Bytes& value, std::size_t offset) {
    server::hub::Reader reader(std::span<const std::uint8_t>(value).subspan(offset));
    return reader.f32();
}

} // namespace soulwarden::backend::tests
